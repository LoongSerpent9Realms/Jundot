/*  ai_tool_executor.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/
/* Copyright (c) 2024-present JunDot contributors.                        */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/**************************************************************************/

#include "ai_tool_executor.h"
#include "ai_tool_defs.h"
#include "ai_code_fetcher.h"
#include "ai_code_security_checker.h"
#include "ai_code_uploader.h"
#include "ai_feature_gate.h"
#include "ai_mcp_runtime.h"
#include "ai_restart_helper.h"
#include "ai_settings.h"
#include "ai_tool_registry.h"

#include "core/config/project_settings.h"
#include "core/error/error_macros.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/os/thread_safe.h"
#include "core/string/string_builder.h"
#include "editor/editor_node.h"

// ---- Async build state ----
struct BuildState {
	enum Status { IDLE, RUNNING, DONE, FAILED };

	Status status = IDLE;
	String std_out;
	int exit_code = -1;
	String extra_args; // snapshot of args for the running build
	bool has_result = false;

	Mutex mutex;
	Thread thread;

	void start(const String &p_extra_args) {
		MutexLock lock(mutex);
		if (status == RUNNING) {
			return; // already running
		}
		status = RUNNING;
		has_result = false;
		std_out.clear();
		exit_code = -1;
		extra_args = p_extra_args;
	}

	void complete(const String &p_std_out, int p_exit_code) {
		MutexLock lock(mutex);
		std_out = p_std_out;
		exit_code = p_exit_code;
		status = (p_exit_code == 0) ? DONE : FAILED;
		has_result = true;
	}

	Dictionary get_result() {
		MutexLock lock(mutex);
		Dictionary r;
		r["status"] = (status == RUNNING) ? "running" : (status == DONE ? "done" : (status == FAILED ? "failed" : "idle"));
		r["exit_code"] = exit_code;
		r["stdout"] = std_out;
		r["has_result"] = has_result;
		return r;
	}

	bool is_running() {
		MutexLock lock(mutex);
		return status == RUNNING;
	}

	// Must be called from the main thread after the build finishes,
	// to clean up the thread resource.
	void join_thread() {
		MutexLock lock(mutex);
		if (status == RUNNING || status == IDLE) {
			return; // thread is still running or never started, don't join
		}
		lock.temp_unlock();
		if (thread.is_started()) {
			thread.wait_to_finish();
		}
		lock.temp_relock();
	}
};

static BuildState build_state;

static String _find_sconstruct_root_from(String p_dir, int p_max_depth = 10) {
	for (int i = 0; i < p_max_depth && !p_dir.is_empty(); i++) {
		if (FileAccess::exists(p_dir.path_join("SConstruct"))) {
			return p_dir;
		}

		String parent = p_dir.get_base_dir();
		if (parent == p_dir) {
			break;
		}
		p_dir = parent;
	}

	return String();
}

static String _get_default_engine_source_cache_root() {
	String user_data_dir = OS::get_singleton()->get_user_data_dir();
	if (user_data_dir.is_empty()) {
		return String();
	}
	return user_data_dir.path_join("engine_source");
}

static String _get_configured_engine_source_cache_root(const AISettingsData &p_settings) {
	if (!p_settings.engine_source_cache_root.is_empty()) {
		return p_settings.engine_source_cache_root;
	}
	return _get_default_engine_source_cache_root();
}

static String _get_engine_build_root() {
	AISettingsData settings = AISettings::load();

	if (!settings.engine_source_root.is_empty() && FileAccess::exists(settings.engine_source_root.path_join("SConstruct"))) {
		return settings.engine_source_root;
	}

	String detected = AISettings::get_engine_source_root(settings);
	if (!detected.is_empty() && FileAccess::exists(detected.path_join("SConstruct"))) {
		return detected;
	}

	String exe_path = OS::get_singleton()->get_executable_path();
	if (!exe_path.is_empty()) {
		detected = _find_sconstruct_root_from(exe_path.get_base_dir());
		if (!detected.is_empty()) {
			return detected;
		}
	}

	String cache_root = _get_configured_engine_source_cache_root(settings);
	if (!cache_root.is_empty() && FileAccess::exists(cache_root.path_join("SConstruct"))) {
		return cache_root;
	}

	return _find_sconstruct_root_from(OS::get_singleton()->get_cwd());
}

static String _source_root_missing_message() {
	AISettingsData settings = AISettings::load();
	String cache_root = _get_configured_engine_source_cache_root(settings);
	String message = "No JunDot engine source checkout is configured. A packaged editor executable does not contain source code.";
	if (!cache_root.is_empty()) {
		message += " Clone/download the JunDot source into the default cache directory (" + cache_root + ") or set AI engine_source_root to another source checkout, then retry.";
	} else {
		message += " Clone/download the JunDot source into a separate directory, set AI engine_source_root to that directory, then retry.";
	}
	return message;
}

static void _build_thread_callback(void *p_userdata) {
	String extra_args = String();
	if (p_userdata) {
		extra_args = *(static_cast<String *>(p_userdata));
		delete static_cast<String *>(p_userdata);
	}

	String project_root = _get_engine_build_root();
	if (project_root.is_empty()) {
		build_state.complete("Failed to start build: no JunDot engine source root with SConstruct was found. A packaged editor executable does not contain the engine source. Clone/download the JunDot source into a separate directory, set AI engine_source_root to that directory, then retry run_build.", -1);
		return;
	}

	String previous_cwd = OS::get_singleton()->get_cwd();
	Error cwd_err = OS::get_singleton()->set_cwd(project_root);
	if (cwd_err != OK) {
		build_state.complete("Failed to start build: could not switch to project root: " + project_root, -1);
		return;
	}

	// Build scons arguments.
	List<String> scons_args;
	scons_args.push_back("platform=windows");
	scons_args.push_back("target=editor");
	scons_args.push_back("arch=x86_64");
	scons_args.push_back("debug_symbols=no");
	scons_args.push_back("-j4");
	scons_args.push_back("d3d12=no");
	scons_args.push_back("accesskit=no");
	scons_args.push_back("angle=no");

	if (!extra_args.is_empty()) {
		Vector<String> extras = extra_args.split(" ", false);
		for (int i = 0; i < extras.size(); i++) {
			scons_args.push_back(extras[i]);
		}
	}


	// Try python -m SCons first, then scons, then python3 -m SCons.
	String std_out;
	int exit_code = -1;

	// First try: python -m SCons
	List<String> py_args;
	py_args.push_back("-m");
	py_args.push_back("SCons");
	for (const String &arg : scons_args) {
		py_args.push_back(arg);
	}
	Error err = OS::get_singleton()->execute("python", py_args, &std_out, &exit_code, true);

	if (err != OK) {
		// Second try: scons directly
		err = OS::get_singleton()->execute("scons", scons_args, &std_out, &exit_code, true);
		if (err != OK) {
			// Third try: python3 -m SCons
			err = OS::get_singleton()->execute("python3", py_args, &std_out, &exit_code, true);
			if (err != OK) {
				std_out = "Failed to start build: neither 'python -m SCons', 'scons', nor 'python3 -m SCons' was found.";
				exit_code = -1;
			}
		}
	}

	OS::get_singleton()->set_cwd(previous_cwd);
	build_state.complete(std_out, exit_code);
}

Dictionary AIToolExecutor::execute(const Dictionary &p_tool_call) {
	const String tool_call_id = p_tool_call.get("id", String());
	const Dictionary fn_def = p_tool_call.get("function", Dictionary());
	const String name = fn_def.get("name", String());
	const String args_json = fn_def.get("arguments", "{}");

	Dictionary args;
	Variant parsed = JSON::parse_string(args_json);
	if (parsed.get_type() == Variant::DICTIONARY) {
		args = parsed;
	}

	Dictionary result;
	if (name == AIToolNames::READ_FILES) {
		result = _read_files(args);
	} else if (name == AIToolNames::WRITE_FILE) {
		result = _write_file(args);
	} else if (name == AIToolNames::SEARCH_FILES) {
		result = _search_files(args);
	} else if (name == AIToolNames::GREP_CODE) {
		result = _grep_code(args);
	} else if (name == AIToolNames::RUN_BUILD) {
		result = _run_build(args);
	} else if (name == AIToolNames::CHECK_BUILD_STATUS) {
		result = _check_build_status(args);
	} else if (name == AIToolNames::READ_BUILD_LOG) {
		result = _read_build_log(args);
	} else if (name == AIToolNames::FETCH_URL) {
		result = _fetch_url(args);
	} else if (name == AIToolNames::SHELL_COMMAND) {
		result = _shell_command(args);
	} else if (name == AIToolNames::UPLOAD_CODE) {
		result = _upload_code(args);
	} else if (name == AIToolNames::RESTART_ENGINE) {
		result = _restart_engine(args);
	} else if (name.find_char('.') >= 0) {
		// Tool names with a dot separator indicate MCP tools (e.g. "server_name.tool_name").
		int dot = name.find_char('.');
		String server_name = name.substr(0, dot);
		String tool_name = name.substr(dot + 1);
		result = _execute_mcp_tool(server_name, tool_name, args_json);
	} else {
		result = _make_result(vformat("Unknown tool: %s", name), true);
	}

	result["role"] = "tool";
	result["tool_call_id"] = tool_call_id;
	return result;
}

Dictionary AIToolExecutor::_make_result(const String &p_content, bool p_is_error) {
	Dictionary d;
	d["content"] = p_content;
	if (p_is_error) {
		d["is_error"] = true;
	}
	return d;
}

String AIToolExecutor::_get_project_root() {
	// In ENGINE mode, return the engine source directory from settings
	// (auto-detected via AISettings::get_engine_source_root).
	// In PROJECT mode, return the currently-opened project directory.
	AISettingsData s = AISettings::load();
	if (s.context_mode == AIContextMode::ENGINE) {
		String engine_root = _get_engine_build_root();
		if (!engine_root.is_empty()) {
			return engine_root;
		}
		return String();
	}

	if (ProjectSettings::get_singleton()) {
		String project_root = ProjectSettings::get_singleton()->get_resource_path();
		if (!project_root.is_empty() && FileAccess::exists(project_root.path_join("project.godot"))) {
			return project_root;
		}
	}

	// Project mode fallback: try the executable directory, otherwise fall back to cwd.
	String exe_path = OS::get_singleton()->get_executable_path();
	if (!exe_path.is_empty() && FileAccess::exists(exe_path.get_base_dir().path_join("project.godot"))) {
		return exe_path.get_base_dir();
	}

	return OS::get_singleton()->get_cwd();
}

// ---- Tool implementations ----

Dictionary AIToolExecutor::_read_files(const Dictionary &p_args) {
	Array paths = p_args.get("paths", Array());
	if (paths.is_empty()) {
		return _make_result("No file paths provided.", true);
	}

	String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result(_source_root_missing_message(), true);
	}
	StringBuilder result;

	for (int i = 0; i < paths.size(); i++) {
		String rel_path = paths[i];
		String full_path = project_root.path_join(rel_path);

		if (!FileAccess::exists(full_path)) {
			result += vformat("[%s] Error: file not found at %s\n", rel_path, full_path);
			continue;
		}

		Error err = OK;
		String content = FileAccess::get_file_as_string(full_path, &err);
		if (err != OK) {
			result += vformat("[%s] Error: failed to read file (err=%d)\n", rel_path, (int)err);
			continue;
		}

		// Truncate very large files to avoid blowing the API context.
		const int MAX_FILE_CHARS = 50000;
		if (content.length() > MAX_FILE_CHARS) {
			content = content.substr(0, MAX_FILE_CHARS) + vformat("\n\n[... truncated at %d characters]\n", MAX_FILE_CHARS);
		}

		result += vformat("=== %s ===\n%s\n", rel_path, content);
	}

	return _make_result(result.as_string());
}

Dictionary AIToolExecutor::_write_file(const Dictionary &p_args) {
	String path = p_args.get("path", String());
	String content = p_args.get("content", String());

	if (path.is_empty()) {
		return _make_result("No file path provided.", true);
	}

	// Validate content: reject Markdown code fences, which usually mean the
	// model returned a formatted snippet instead of raw file contents.
	if (content.contains("```")) {
		return _make_result(vformat("File write rejected: content contains Markdown code fences (```). Please call write_file again with raw file content only.\nPath: %s", path), true);
	}

	// Reject obviously truncated code (ends mid-statement).
	String trimmed = content.strip_edges();
	if (trimmed.ends_with("...") || trimmed.ends_with(",") || trimmed.ends_with("(") || trimmed.ends_with("=")) {
		return _make_result(vformat("File write rejected: content appears truncated (ends with '%s'). Please regenerate the complete content.\nPath: %s", String::chr(trimmed[trimmed.length() - 1]), path), true);
	}

	String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result(_source_root_missing_message(), true);
	}
	String full_path = project_root.path_join(path);

	// Create parent directories.
	Ref<DirAccess> da = DirAccess::create_for_path(full_path);
	if (da.is_valid()) {
		Error mkdir_err = da->make_dir_recursive(full_path.get_base_dir());
		if (mkdir_err != OK) {
			return _make_result(vformat("Failed to create directory for %s", path), true);
		}
	} else {
		return _make_result(vformat("Failed to access directory for %s", path), true);
	}

	// Write to a temporary file first so a failed write does not remove the
	// previous version.
	String tmp_path = full_path + ".ai_tmp";
	Error err = OK;
	Ref<FileAccess> file = FileAccess::open(tmp_path, FileAccess::WRITE, &err);
	if (err != OK || file.is_null()) {
		return _make_result(vformat("Failed to open temporary file for writing: %s", path), true);
	}

	file->store_string(content);
	file.unref();

	String bak_path = full_path + ".bak";
	if (FileAccess::exists(bak_path)) {
		Ref<DirAccess> bak_da = DirAccess::create_for_path(bak_path);
		if (bak_da.is_valid()) {
			bak_da->remove(bak_path);
		}
	}

	if (FileAccess::exists(full_path)) {
		Error backup_err = da->rename(full_path, bak_path);
		if (backup_err != OK) {
			da->remove(tmp_path);
			return _make_result(vformat("Failed to create backup before writing: %s", path), true);
		}
	}

	Error rename_err = da->rename(tmp_path, full_path);
	if (rename_err != OK) {
		if (FileAccess::exists(bak_path) && !FileAccess::exists(full_path)) {
			da->rename(bak_path, full_path);
		}
		da->remove(tmp_path);
		return _make_result(vformat("Failed to replace file after writing temporary content: %s", path), true);
	}

	return _make_result(vformat("Successfully wrote %s (%d bytes).", path, content.utf8().length()));
}

Dictionary AIToolExecutor::_search_files(const Dictionary &p_args) {
	String pattern = p_args.get("pattern", String());
	if (pattern.is_empty()) {
		return _make_result("No search pattern provided.", true);
	}

	String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result(_source_root_missing_message(), true);
	}

	// Use a simple recursive directory walk. Godot's DirAccess doesn't support
	// globs natively, so we do a manual pattern match.
	// For simplicity, we handle: **/*.cpp, **/*.h, src/**/*.cpp patterns.

	// Split pattern into base_dir and file_filter.
	String base_dir = project_root;
	String file_filter = pattern;

	// Extract base directory if pattern contains a path separator.
	int sep = pattern.find("/");
	if (sep >= 0) {
		String dir_part = pattern.substr(0, sep);
		if (dir_part == "**") {
			// ** pattern �?search from root.
			file_filter = pattern.substr(3).trim_prefix("/");
		} else {
			base_dir = project_root.path_join(dir_part);
			file_filter = pattern.substr(sep + 1);
		}
	}

	// Also handle src/**/*.cpp style.
	int star_slash = pattern.find("**/");
	if (star_slash >= 0) {
		String prefix = pattern.substr(0, star_slash);
		if (!prefix.is_empty()) {
			base_dir = project_root.path_join(prefix.trim_suffix("/"));
		}
		file_filter = pattern.substr(star_slash + 3);
		// If the filter starts with *, it's likely *.<ext>
		if (!file_filter.begins_with("*")) {
			file_filter = file_filter.trim_prefix("/");
		}
	}

	// Convert simple glob to suffix/prefix matching.
	String suffix;
	String prefix;
	if (file_filter.begins_with("*.")) {
		suffix = file_filter.substr(1); // e.g. ".cpp"
	} else if (file_filter.begins_with("*")) {
		suffix = file_filter.substr(1);
	} else if (file_filter.ends_with("*")) {
		prefix = file_filter.substr(0, file_filter.length() - 1);
	} else {
		// Exact match.
		suffix = file_filter;
	}

	// Recursively collect matching files.
	Vector<String> results;
	List<String> dirs;
	dirs.push_back(base_dir);

	const int MAX_RESULTS = 200;
	while (!dirs.is_empty() && results.size() < MAX_RESULTS) {
		String current_dir = dirs.front()->get();
		dirs.pop_front();

		Ref<DirAccess> da = DirAccess::open(current_dir);
		if (da.is_null()) {
			continue;
		}

		da->list_dir_begin();
		String name = da->get_next();
		while (!name.is_empty() && results.size() < MAX_RESULTS) {
			if (name == "." || name == "..") {
				name = da->get_next();
				continue;
			}

			String full = current_dir.path_join(name);
			if (da->current_is_dir()) {
				// Skip hidden directories and common build artifacts.
				if (!name.begins_with(".") && name != "bin" && name != "obj" && name != "__pycache__") {
					dirs.push_back(full);
				}
			} else {
				bool match = false;
				if (!suffix.is_empty() && name.ends_with(suffix)) {
					match = true;
				} else if (!prefix.is_empty() && name.begins_with(prefix)) {
					match = true;
				} else if (suffix.is_empty() && prefix.is_empty()) {
					match = (name == file_filter);
				}
				if (match) {
					// Convert to relative path.
					String rel = full.replace(project_root + "/", "");
					results.push_back(rel);
				}
			}

			name = da->get_next();
		}
		da->list_dir_end();
	}

	if (results.is_empty()) {
		return _make_result(vformat("No files matching '%s' found.", pattern));
	}

	StringBuilder sb;
	sb += vformat("Found %d files matching '%s':\n", results.size(), pattern);
	for (int i = 0; i < results.size(); i++) {
		sb += results[i] + "\n";
	}

	return _make_result(sb.as_string());
}

Dictionary AIToolExecutor::_grep_code(const Dictionary &p_args) {
	String pattern = p_args.get("pattern", String());
	String glob = p_args.get("glob", String());

	if (pattern.is_empty()) {
		return _make_result("No search pattern provided.", true);
	}

	String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result(_source_root_missing_message(), true);
	}

	// Determine which directories to search.
	Vector<String> search_dirs;
	search_dirs.push_back(project_root.path_join("editor"));
	search_dirs.push_back(project_root.path_join("modules"));
	search_dirs.push_back(project_root.path_join("scene"));
	search_dirs.push_back(project_root.path_join("servers"));
	search_dirs.push_back(project_root.path_join("core"));
	search_dirs.push_back(project_root.path_join("drivers"));
	search_dirs.push_back(project_root.path_join("main"));
	search_dirs.push_back(project_root.path_join("platform"));

	// Use the glob to filter file extension.
	String suffix_filter;
	if (!glob.is_empty()) {
		if (glob.begins_with("*.")) {
			suffix_filter = glob.substr(1);
		} else if (glob.begins_with("*")) {
			suffix_filter = glob.substr(1);
		} else {
			suffix_filter = glob;
		}
	}

	StringBuilder result;
	int total_matches = 0;
	const int MAX_MATCHES = 200;

	for (int d = 0; d < search_dirs.size() && total_matches < MAX_MATCHES; d++) {
		List<String> dirs;
		dirs.push_back(search_dirs[d]);

		while (!dirs.is_empty() && total_matches < MAX_MATCHES) {
			String current_dir = dirs.front()->get();
			dirs.pop_front();

			Ref<DirAccess> da = DirAccess::open(current_dir);
			if (da.is_null()) {
				continue;
			}

			da->list_dir_begin();
			String name = da->get_next();
			while (!name.is_empty() && total_matches < MAX_MATCHES) {
				if (name == "." || name == "..") {
					name = da->get_next();
					continue;
				}

				String full_path = current_dir.path_join(name);
				if (da->current_is_dir()) {
					if (!name.begins_with(".") && name != "bin" && name != "obj" && name != "__pycache__") {
						dirs.push_back(full_path);
					}
				} else {
					// Apply glob filter.
					if (!suffix_filter.is_empty() && !name.ends_with(suffix_filter)) {
						name = da->get_next();
						continue;
					}

					// Skip large binary-like files.
					if (name.ends_with(".obj") || name.ends_with(".lib") || name.ends_with(".dll") || name.ends_with(".exe")) {
						name = da->get_next();
						continue;
					}

					Error err = OK;
					String content = FileAccess::get_file_as_string(full_path, &err);
					if (err != OK) {
						name = da->get_next();
						continue;
					}

					// Simple line-by-line search.
					Vector<String> lines = content.split("\n");
					String rel_path = full_path.replace(project_root + "/", "");
					for (int ln = 0; ln < lines.size() && total_matches < MAX_MATCHES; ln++) {
						if (lines[ln].findn(pattern) >= 0) {
							result += vformat("%s:%d: %s\n", rel_path, ln + 1, lines[ln].strip_edges());
							total_matches++;
						}
					}
				}

				name = da->get_next();
			}
			da->list_dir_end();
		}
	}

	if (total_matches == 0) {
		return _make_result(vformat("No matches found for '%s'.", pattern));
	}

	String header = vformat("Found %d matches for '%s':\n", total_matches, pattern);
	return _make_result(header + result.as_string());
}

Dictionary AIToolExecutor::_run_build(const Dictionary &p_args) {
	String extra_args = p_args.get("extra_args", String());

	// Check if build is already running.
	if (build_state.is_running()) {
		return _make_result("A build is already running in the background. Use check_build_status to monitor progress.");
	}

	// Start the build in a background thread.
	build_state.start(extra_args);
	String *args_copy = new String(extra_args);
	build_state.thread.start(_build_thread_callback, args_copy);

	return _make_result("Build started in background. Use check_build_status to check progress and get results when complete.");
}

Dictionary AIToolExecutor::_check_build_status(const Dictionary &p_args) {
	if (build_state.is_running()) {
		return _make_result("Build is still running in the background. Check again later.");
	}

	Dictionary r = build_state.get_result();
	String status = r["status"];

	if (status == "idle") {
		return _make_result("No build has been started yet. Use run_build to start one.");
	}

	// Build is done �?retrieve the result.
	String std_out = r["stdout"];
	int exit_code = r["exit_code"];

	// Clean up the thread resource before returning.
	build_state.join_thread();

	StringBuilder result;
	result += vformat("Build status: %s\n", status);
	result += vformat("Exit code: %d\n", exit_code);
	if (!std_out.is_empty()) {
		result += "\n--- build output ---\n" + std_out;
	}

	if (result.as_string().length() > 10000) {
		String truncated = result.as_string().substr(0, 10000);
		truncated += vformat("\n\n[... truncated, full length = %d chars]\n", result.as_string().length());
		return _make_result(truncated);
	}

	return _make_result(result.as_string());
}

Dictionary AIToolExecutor::_read_build_log(const Dictionary &p_args) {
	String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result(_source_root_missing_message(), true);
	}
	String log_dir = project_root.path_join("artifacts").path_join("logs");

	Ref<DirAccess> da = DirAccess::open(log_dir);
	if (da.is_null()) {
		return _make_result("Build log directory not found: " + log_dir, true);
	}

	// Find the most recent .log file.
	String latest_log;
	uint64_t latest_mtime = 0;

	da->list_dir_begin();
	String name = da->get_next();
	while (!name.is_empty()) {
		if (name.ends_with(".log")) {
			String full = log_dir.path_join(name);
			uint64_t mtime = FileAccess::get_modified_time(full);
			if (mtime > latest_mtime) {
				latest_mtime = mtime;
				latest_log = full;
			}
		}
		name = da->get_next();
	}
	da->list_dir_end();

	if (latest_log.is_empty()) {
		return _make_result("No build log files found in " + log_dir, true);
	}

	Error err = OK;
	String content = FileAccess::get_file_as_string(latest_log, &err);
	if (err != OK) {
		return _make_result(vformat("Failed to read log file: %s", latest_log), true);
	}

	// Truncate if too large.
	const int MAX_LOG_CHARS = 30000;
	if (content.length() > MAX_LOG_CHARS) {
		// Prefer the end of the log (build errors are at the end).
		String tail = content.substr(content.length() - MAX_LOG_CHARS);
		// Also include any error-like lines from the beginning.
		String result = vformat("Log file: %s\n(full length: %d chars, showing last %d chars)\n\n%s",
				latest_log, content.length(), MAX_LOG_CHARS, tail);
		return _make_result(result);
	}

	return _make_result(vformat("Log file: %s\n\n%s", latest_log, content));
}

Dictionary AIToolExecutor::_fetch_url(const Dictionary &p_args) {
	String url = p_args.get("url", String());
	String dest_path = p_args.get("dest_path", String());

	if (url.is_empty()) {
		return _make_result("No URL provided.", true);
	}
	if (dest_path.is_empty()) {
		return _make_result("No destination path provided.", true);
	}

	String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result(_source_root_missing_message(), true);
	}
	String full_dest = project_root.path_join(dest_path);

	String sha256;
	Error err = AICodeFetcher::fetch_file(url, full_dest, sha256);
	if (err != OK) {
		return _make_result(vformat("Failed to download %s to %s (err=%d)", url, dest_path, (int)err), true);
	}

	return _make_result(vformat("Successfully downloaded %s to %s (SHA256: %s)", url, dest_path, sha256));
}

Dictionary AIToolExecutor::_shell_command(const Dictionary &p_args) {
	String command = p_args.get("command", String());
	if (command.is_empty()) {
		return _make_result("No command provided.", true);
	}

	String project_root = _get_project_root();
	if (project_root.is_empty()) {
		project_root = _get_configured_engine_source_cache_root(AISettings::load());
		if (project_root.is_empty()) {
			return _make_result("Project root not detected.", true);
		}
		Ref<DirAccess> da = DirAccess::create_for_path(project_root);
		if (da.is_null() || da->make_dir_recursive(project_root) != OK) {
			return _make_result("Project root not detected and failed to create engine source cache directory.", true);
		}
	}

	// Split command into program and arguments.
	Vector<String> parts = command.split(" ", false);
	if (parts.is_empty()) {
		return _make_result("Empty command.", true);
	}

	String program = parts[0];
	List<String> shell_args;
	for (int i = 1; i < parts.size(); i++) {
		shell_args.push_back(parts[i]);
	}

	String std_out;
	int exit_code = -1;

	String previous_cwd = OS::get_singleton()->get_cwd();
	Error cwd_err = OS::get_singleton()->set_cwd(project_root);
	if (cwd_err != OK) {
		return _make_result("Failed to switch command working directory to: " + project_root, true);
	}

#ifdef WINDOWS_ENABLED
	// On Windows, use cmd /c for shell commands.
	List<String> cmd_args;
	cmd_args.push_back("/c");
	cmd_args.push_back(command);
	Error err = OS::get_singleton()->execute("cmd", cmd_args, &std_out, &exit_code, true);
#else
	Error err = OS::get_singleton()->execute(program, shell_args, &std_out, &exit_code, true);
#endif

	OS::get_singleton()->set_cwd(previous_cwd);

	if (err != OK) {
		return _make_result(vformat("Failed to execute command: %s (err=%d)", command, (int)err), true);
	}

	StringBuilder result;
	result += vformat("Command: %s\n", command);
	result += vformat("Exit code: %d\n", exit_code);
	if (!std_out.is_empty()) {
		result += "\n--- stdout ---\n" + std_out;
	}

	// Truncate very large output.
	if (result.as_string().length() > 30000) {
		String truncated = result.as_string().substr(0, 30000);
		truncated += vformat("\n\n[... truncated at 30000 chars]\n");
		return _make_result(truncated);
	}

	return _make_result(result.as_string());
}

Dictionary AIToolExecutor::_restart_engine(const Dictionary &p_args) {
	// Save editor state before restart.
	Error err = AIRestartHelper::save_state();
	if (err != OK) {
		return _make_result("Failed to save editor state: " + itos((int)err), true);
	}

	// Trigger editor restart.
	EditorNode::get_singleton()->restart_editor(false);

	return _make_result("Engine restart initiated. The editor will save open files and relaunch with the new build.");
}

Dictionary AIToolExecutor::_upload_code(const Dictionary &p_args) {
	String file_path = p_args.get("file_path", String());
	String commit_message = p_args.get("commit_message", String());

	if (file_path.is_empty()) {
		return _make_result("file_path is required.", true);
	}
	if (commit_message.is_empty()) {
		return _make_result("commit_message is required.", true);
	}

	String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result("Project root not detected. Make sure you're in ENGINE mode with a valid godot.creator.json or SConstruct in the working directory.", true);
	}

	String full_path = project_root.path_join(file_path);

	// Step 1: Verify file exists.
	if (!FileAccess::exists(full_path)) {
		return _make_result("File not found: " + full_path, true);
	}

	// Step 2: Read file content.
	Error read_err;
	String code = FileAccess::get_file_as_string(full_path, &read_err);
	if (read_err != OK) {
		return _make_result("Failed to read file: " + full_path, true);
	}

	// Step 3: Security check - detect suspicious patterns.
	CodeSecurityReport security = AICodeSecurityChecker::check(code);
	if (!security.is_safe) {
		String warning_text = "Upload rejected: security check failed.\n";
		for (int i = 0; i < security.warnings.size(); i++) {
			warning_text += "  - " + security.warnings[i] + "\n";
		}
		warning_text += "\nPlease review the flagged code or use shell_command to push manually after review.";
		return _make_result(warning_text, true);
	}

	// Step 4: Universality estimate (simple heuristic based on code characteristics).
	double universality_score = 75.0; // default: assume generally useful
	if (code.length() < 50) {
		universality_score = 60.0; // tiny snippets are less general
	} else if (code.find("TODO") >= 0 || code.find("hardcoded") >= 0) {
		universality_score = 55.0; // code with explicit TODOs/hardcoded markers are lower
	}

	// Step 5: Check against AIFeatureGate threshold.
	AISettingsData settings = AISettings::load();
	if (universality_score < settings.feature_universality_threshold) {
		return _make_result(
				"Upload rejected: estimated universality " + String::num(universality_score, 1) +
						"% below threshold " + String::num(settings.feature_universality_threshold, 1) +
						"%. Code should be more generally useful before committing.",
				true);
	}

	// Step 6: Perform git add / commit / push.
	String error_msg;
	Error upload_err = AICodeUploader::upload(file_path, commit_message, project_root, &error_msg);
	if (upload_err != OK) {
		return _make_result("Upload failed: " + error_msg, true);
	}

	return _make_result(
			"Upload successful: " + file_path +
					"\n  Commit message: " + commit_message +
					"\n  Security: PASSED"
					"\n  Universality: " + String::num(universality_score, 1) + "%");
}

Dictionary AIToolExecutor::_execute_mcp_tool(const String &p_server_name, const String &p_tool_name, const String &p_args_json) {
	// Look up the MCP server configuration.
	Vector<AISkillEntry> skills;
	Vector<AIMCPServerEntry> mcp_servers;
	if (AIToolRegistry::load(skills, mcp_servers) != OK) {
		return _make_result(vformat("MCP server '%s' not found (registry load failed).", p_server_name), true);
	}

	AIMCPServerEntry target_server;
	bool found = false;
	for (const AIMCPServerEntry &server : mcp_servers) {
		if (server.name == p_server_name && server.enabled) {
			target_server = server;
			found = true;
			break;
		}
	}

	if (!found) {
		return _make_result(vformat("MCP server '%s' is not configured or not enabled.", p_server_name), true);
	}

	if (target_server.command.is_empty() && target_server.url.is_empty()) {
		return _make_result(vformat("MCP server '%s' has no command or URL configured.", p_server_name), true);
	}

	// Use MCPServerRuntime for interactive MCP communication
	MCPServerRuntime *runtime = MCPServerRuntime::get_singleton();

	// Lazy start: if runtime not running for this server, start it
	if (!runtime->is_alive()) {
		Error err = runtime->start(target_server);
		if (err != OK) {
			return _make_result(vformat("Failed to start MCP server '%s': %s", p_server_name, runtime->get_last_error()), true);
		}
	}

	// Parse arguments JSON
	Dictionary arguments;
	Variant parsed_args = JSON::parse_string(p_args_json);
	if (parsed_args.get_type() == Variant::DICTIONARY) {
		arguments = parsed_args;
	}

	// Call the tool via runtime
	Dictionary result = runtime->call_tool(p_tool_name, arguments);

	if (result.has("is_error") && result["is_error"]) {
		String error_content = result.get("content", "Unknown error");
		return _make_result(vformat("MCP tool '%s.%s' error: %s", p_server_name, p_tool_name, error_content), true);
	}

	String content = result.get("content", "");
	return _make_result(content);
}
