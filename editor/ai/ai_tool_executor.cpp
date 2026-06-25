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
#include "core/object/callable_mp.h"
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
	String executable_path; // editor binary produced by the successful build
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
		executable_path.clear();
	}

	void complete(const String &p_std_out, int p_exit_code, const String &p_executable_path = String()) {
		MutexLock lock(mutex);
		std_out = p_std_out;
		exit_code = p_exit_code;
		executable_path = p_executable_path;
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
		r["executable_path"] = executable_path;
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

static String _find_latest_built_editor_executable(const String &p_project_root, uint64_t p_build_started_at, bool p_mono_enabled) {
	const String bin_dir = p_project_root.path_join("bin");
	Ref<DirAccess> dir = DirAccess::open(bin_dir);
	if (dir.is_null()) {
		return String();
	}

	String latest_path;
	uint64_t latest_mtime = 0;
	dir->list_dir_begin();
	String name = dir->get_next();
	while (!name.is_empty()) {
		const String lower_name = name.to_lower();
		const bool is_mono = lower_name.contains(".mono.");
		if (!dir->current_is_dir() && lower_name.contains(".windows.editor") &&
				lower_name.ends_with(".exe") && !lower_name.ends_with(".console.exe") &&
				is_mono == p_mono_enabled) {
			const String candidate = bin_dir.path_join(name);
			const uint64_t mtime = FileAccess::get_modified_time(candidate);
			// Ignore stale editor binaries left by earlier build configurations.
			// Allow a small tolerance for filesystems with coarse timestamp precision.
			if (mtime + 2 >= p_build_started_at && mtime >= latest_mtime) {
				latest_mtime = mtime;
				latest_path = candidate;
			}
		}
		name = dir->get_next();
	}
	dir->list_dir_end();

	if (p_mono_enabled && !latest_path.is_empty()) {
		const String assemblies_dir = bin_dir.path_join("JundotSharp").path_join("Api").path_join("Debug");
		if (!DirAccess::dir_exists_absolute(assemblies_dir)) {
			return String();
		}
	}
	return latest_path;
}

static Error _run_prebuild_git_command(const List<String> &p_args, String &r_output, int &r_exit_code) {
	r_output.clear();
	r_exit_code = -1;
	const Error err = OS::get_singleton()->execute("git", p_args, &r_output, &r_exit_code, true);
	if (err != OK) {
		return err;
	}
	return r_exit_code == 0 ? OK : FAILED;
}

static void _restore_prebuild_stash_after_failed_update(const String &p_stash_ref, String &r_log) {
	if (p_stash_ref.is_empty()) {
		return;
	}

	String output;
	int exit_code = -1;
	List<String> abort_args;
	abort_args.push_back("merge");
	abort_args.push_back("--abort");
	_run_prebuild_git_command(abort_args, output, exit_code);

	List<String> apply_args;
	apply_args.push_back("stash");
	apply_args.push_back("apply");
	apply_args.push_back("--index");
	apply_args.push_back(p_stash_ref);
	Error apply_err = _run_prebuild_git_command(apply_args, output, exit_code);
	if (apply_err != OK) {
		List<String> conflicts_args;
		conflicts_args.push_back("diff");
		conflicts_args.push_back("--name-only");
		conflicts_args.push_back("--diff-filter=U");
		String conflicts;
		int conflict_exit = -1;
		_run_prebuild_git_command(conflicts_args, conflicts, conflict_exit);
		if (!conflicts.strip_edges().is_empty()) {
			List<String> theirs_args;
			theirs_args.push_back("checkout");
			theirs_args.push_back("--theirs");
			theirs_args.push_back("--");
			theirs_args.push_back(".");
			if (_run_prebuild_git_command(theirs_args, output, exit_code) == OK) {
				List<String> add_args;
				add_args.push_back("add");
				add_args.push_back("-A");
				apply_err = _run_prebuild_git_command(add_args, output, exit_code);
			}
		}
	}

	if (apply_err == OK) {
		List<String> drop_args;
		drop_args.push_back("stash");
		drop_args.push_back("drop");
		drop_args.push_back(p_stash_ref);
		_run_prebuild_git_command(drop_args, output, exit_code);
		r_log += "[Source Update] Restored local changes after the failed update attempt.\n";
	} else {
		r_log += "[Source Update] Automatic update failed; local changes remain recoverable in " + p_stash_ref + ".\n";
	}
}

static Error _auto_update_source_before_build(String &r_log) {
	String output;
	int exit_code = -1;

	List<String> inside_args;
	inside_args.push_back("rev-parse");
	inside_args.push_back("--is-inside-work-tree");
	if (_run_prebuild_git_command(inside_args, output, exit_code) != OK || output.strip_edges() != "true") {
		r_log += "[Source Update] Current engine source is not a Git worktree; skipping update check.\n";
		return OK;
	}

	List<String> upstream_args;
	upstream_args.push_back("rev-parse");
	upstream_args.push_back("--abbrev-ref");
	upstream_args.push_back("--symbolic-full-name");
	upstream_args.push_back("@{upstream}");
	if (_run_prebuild_git_command(upstream_args, output, exit_code) != OK || output.strip_edges().is_empty()) {
		r_log += "[Source Update] Current branch has no upstream; skipping automatic pull.\n";
		return OK;
	}
	const String upstream = output.strip_edges();

	List<String> fetch_args;
	fetch_args.push_back("fetch");
	fetch_args.push_back("--prune");
	if (_run_prebuild_git_command(fetch_args, output, exit_code) != OK) {
		r_log += "[Source Update] git fetch failed:\n" + output + "\n";
		return FAILED;
	}

	List<String> behind_args;
	behind_args.push_back("rev-list");
	behind_args.push_back("--count");
	behind_args.push_back("HEAD.." + upstream);
	if (_run_prebuild_git_command(behind_args, output, exit_code) != OK) {
		r_log += "[Source Update] Could not compare local and upstream revisions:\n" + output + "\n";
		return FAILED;
	}
	const int behind_count = output.strip_edges().to_int();
	if (behind_count <= 0) {
		r_log += "[Source Update] Source is up to date.\n";
		return OK;
	}
	r_log += vformat("[Source Update] Upstream has %d new commit(s); updating before build.\n", behind_count);

	List<String> status_args;
	status_args.push_back("status");
	status_args.push_back("--porcelain");
	if (_run_prebuild_git_command(status_args, output, exit_code) != OK) {
		r_log += "[Source Update] Could not inspect local changes:\n" + output + "\n";
		return FAILED;
	}
	const bool had_local_changes = !output.strip_edges().is_empty();
	String stash_ref;
	if (had_local_changes) {
		const String stash_message = "jundot-ai-prebuild-" + String::num_int64(OS::get_singleton()->get_ticks_usec());
		List<String> stash_args;
		stash_args.push_back("stash");
		stash_args.push_back("push");
		stash_args.push_back("--include-untracked");
		stash_args.push_back("--message");
		stash_args.push_back(stash_message);
		if (_run_prebuild_git_command(stash_args, output, exit_code) != OK) {
			r_log += "[Source Update] Failed to preserve local changes before update:\n" + output + "\n";
			return FAILED;
		}
		stash_ref = "stash@{0}";
		r_log += "[Source Update] Preserved local tracked and untracked changes in " + stash_ref + ".\n";
	}

	List<String> merge_args;
	merge_args.push_back("merge");
	merge_args.push_back("--no-edit");
	merge_args.push_back("-X");
	merge_args.push_back("ours");
	merge_args.push_back(upstream);
	Error merge_err = _run_prebuild_git_command(merge_args, output, exit_code);
	if (merge_err != OK) {
		List<String> conflicts_args;
		conflicts_args.push_back("diff");
		conflicts_args.push_back("--name-only");
		conflicts_args.push_back("--diff-filter=U");
		String conflict_output;
		int conflict_exit = -1;
		_run_prebuild_git_command(conflicts_args, conflict_output, conflict_exit);
		if (conflict_output.strip_edges().is_empty()) {
			List<String> abort_args;
			abort_args.push_back("merge");
			abort_args.push_back("--abort");
			String ignored;
			int ignored_exit = -1;
			_run_prebuild_git_command(abort_args, ignored, ignored_exit);
			r_log += "[Source Update] Upstream merge failed without resolvable file conflicts:\n" + output + "\n";
			_restore_prebuild_stash_after_failed_update(stash_ref, r_log);
			return FAILED;
		}

		List<String> ours_args;
		ours_args.push_back("checkout");
		ours_args.push_back("--ours");
		ours_args.push_back("--");
		ours_args.push_back(".");
		String resolve_output;
		int resolve_exit = -1;
		if (_run_prebuild_git_command(ours_args, resolve_output, resolve_exit) != OK) {
			r_log += "[Source Update] Failed to resolve upstream conflicts with local branch versions:\n" + resolve_output + "\n";
			_restore_prebuild_stash_after_failed_update(stash_ref, r_log);
			return FAILED;
		}
		List<String> add_args;
		add_args.push_back("add");
		add_args.push_back("-A");
		if (_run_prebuild_git_command(add_args, resolve_output, resolve_exit) != OK) {
			r_log += "[Source Update] Failed to stage automatically resolved upstream conflicts:\n" + resolve_output + "\n";
			_restore_prebuild_stash_after_failed_update(stash_ref, r_log);
			return FAILED;
		}
		List<String> commit_args;
		commit_args.push_back("-c");
		commit_args.push_back("user.name=Jundot AI");
		commit_args.push_back("-c");
		commit_args.push_back("user.email=jundot-ai@local");
		commit_args.push_back("commit");
		commit_args.push_back("--no-edit");
		if (_run_prebuild_git_command(commit_args, resolve_output, resolve_exit) != OK) {
			r_log += "[Source Update] Failed to finalize automatically resolved upstream merge:\n" + resolve_output + "\n";
			_restore_prebuild_stash_after_failed_update(stash_ref, r_log);
			return FAILED;
		}
		r_log += "[Source Update] Resolved upstream conflicts by preserving current local branch versions.\n";
	}

	if (!stash_ref.is_empty()) {
		List<String> apply_args;
		apply_args.push_back("stash");
		apply_args.push_back("apply");
		apply_args.push_back("--index");
		apply_args.push_back(stash_ref);
		Error apply_err = _run_prebuild_git_command(apply_args, output, exit_code);
		if (apply_err != OK) {
			List<String> conflicts_args;
			conflicts_args.push_back("diff");
			conflicts_args.push_back("--name-only");
			conflicts_args.push_back("--diff-filter=U");
			String conflict_output;
			int conflict_exit = -1;
			_run_prebuild_git_command(conflicts_args, conflict_output, conflict_exit);
			if (conflict_output.strip_edges().is_empty()) {
				r_log += "[Source Update] Updated source, but failed to restore local changes:\n" + output + "\n";
				return FAILED;
			}
			List<String> theirs_args;
			theirs_args.push_back("checkout");
			theirs_args.push_back("--theirs");
			theirs_args.push_back("--");
			theirs_args.push_back(".");
			if (_run_prebuild_git_command(theirs_args, output, exit_code) != OK) {
				r_log += "[Source Update] Failed to restore local working changes during conflict resolution:\n" + output + "\n";
				return FAILED;
			}
			List<String> add_args;
			add_args.push_back("add");
			add_args.push_back("-A");
			if (_run_prebuild_git_command(add_args, output, exit_code) != OK) {
				r_log += "[Source Update] Failed to stage restored local working changes:\n" + output + "\n";
				return FAILED;
			}
			r_log += "[Source Update] Resolved restore conflicts by preserving the pre-build local working versions.\n";
		}

		List<String> drop_args;
		drop_args.push_back("stash");
		drop_args.push_back("drop");
		drop_args.push_back(stash_ref);
		if (_run_prebuild_git_command(drop_args, output, exit_code) != OK) {
			r_log += "[Source Update] Warning: restored local changes, but could not remove " + stash_ref + ".\n";
		}
	}

	List<String> unresolved_args;
	unresolved_args.push_back("diff");
	unresolved_args.push_back("--name-only");
	unresolved_args.push_back("--diff-filter=U");
	if (_run_prebuild_git_command(unresolved_args, output, exit_code) != OK || !output.strip_edges().is_empty()) {
		r_log += "[Source Update] Unresolved conflicts remain; build cancelled.\n" + output + "\n";
		return FAILED;
	}

	r_log += "[Source Update] Source update completed successfully.\n";
	return OK;
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

	String source_update_log;
	if (_auto_update_source_before_build(source_update_log) != OK) {
		OS::get_singleton()->set_cwd(previous_cwd);
		build_state.complete(source_update_log + "\nBuild cancelled because the engine source update did not complete safely.", -1);
		return;
	}

	// Build scons arguments.
	List<String> scons_args;
	scons_args.push_back("platform=windows");
	scons_args.push_back("target=editor");
	scons_args.push_back("arch=x86_64");
	scons_args.push_back("debug_symbols=no");
	// AI builds default to the native editor. A Mono editor also requires a
	// separate managed assemblies build, so never select it accidentally.
	scons_args.push_back("module_mono_enabled=no");
	scons_args.push_back("-j4");
	scons_args.push_back("d3d12=no");
	scons_args.push_back("accesskit=no");
	scons_args.push_back("angle=no");

	if (!extra_args.is_empty()) {
		Vector<String> extras = extra_args.split(" ", false);
		for (int i = 0; i < extras.size(); i++) {
			if (extras[i].begins_with("module_mono_enabled=")) {
				continue;
			}
			scons_args.push_back(extras[i]);
		}
	}

	const bool mono_enabled = false;
	const uint64_t build_started_at = OS::get_singleton()->get_unix_time();

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
	std_out = source_update_log + "\n" + std_out;
	const String built_executable = exit_code == 0 ? _find_latest_built_editor_executable(project_root, build_started_at, mono_enabled) : String();
	if (exit_code == 0 && built_executable.is_empty()) {
		std_out += mono_enabled ?
				"\n\nBuild succeeded, but the Mono editor or its JundotSharp/Api/Debug assemblies were not generated by this build." :
				"\n\nBuild succeeded, but no native non-console Windows editor executable generated by this build was found in: " + project_root.path_join("bin");
	}
	build_state.complete(std_out, exit_code, built_executable);
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
	} else if (name == AIToolNames::EDIT_FILE) {
		result = _edit_file(args);
	} else if (name == AIToolNames::SEARCH_FILES) {
		result = _search_files(args);
	} else if (name == AIToolNames::LIST_FILES) {
		result = _list_files(args);
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
	} else if (name == AIToolNames::BATCH_TOOLS) {
		result = _batch_tools(args);
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
	if (paths.is_empty() && p_args.has("path")) {
		paths.push_back(String(p_args.get("path", String())));
	}
	if (paths.is_empty() && p_args.has("paths") && p_args["paths"].get_type() == Variant::STRING) {
		paths.push_back(String(p_args["paths"]));
	}
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
		String full_path;
		if (rel_path.begins_with("res://")) {
			full_path = ProjectSettings::get_singleton() ? ProjectSettings::get_singleton()->globalize_path(rel_path) : rel_path;
		} else if (rel_path.is_absolute_path()) {
			full_path = rel_path;
		} else {
			full_path = project_root.path_join(rel_path);
		}

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
	if (content.is_empty() && p_args.has("new_string")) {
		// Compatibility with models that emit write_file(path, new_string)
		// using edit-style argument naming in text-form tool calls.
		content = p_args.get("new_string", String());
	}

	if (path.is_empty()) {
		return _make_result("No file path provided.", true);
	}
	if (content.is_empty()) {
		return _make_result(vformat("File write rejected: content is empty, so the existing file was not modified.\nPath: %s", path), true);
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

	const int64_t expected_size = content.utf8().length();
	file->store_string(content);
	file->flush();
	file.unref();

	const int64_t temporary_size = FileAccess::get_size(tmp_path);
	if (temporary_size != expected_size) {
		da->remove(tmp_path);
		return _make_result(vformat("File write rejected: temporary file size mismatch (expected %d bytes, wrote %d bytes). The existing file was not modified.\nPath: %s", expected_size, temporary_size, path), true);
	}

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

	const int64_t final_size = FileAccess::get_size(full_path);
	if (final_size != expected_size) {
		da->remove(full_path);
		if (FileAccess::exists(bak_path)) {
			da->rename(bak_path, full_path);
		}
		return _make_result(vformat("File write failed verification: final file size mismatch (expected %d bytes, got %d bytes). The previous file was restored when available.\nPath: %s", expected_size, final_size, path), true);
	}

	return _make_result(vformat("Successfully wrote %s (%d bytes).", path, expected_size));
}

Dictionary AIToolExecutor::_edit_file(const Dictionary &p_args) {
	const String path = p_args.get("path", String());
	const String old_string = p_args.get("old_string", String());
	const String new_string = p_args.get("new_string", String());

	if (path.is_empty()) {
		return _make_result("No file path provided.", true);
	}
	if (old_string.is_empty()) {
		return _make_result(vformat("File edit rejected: old_string is empty.\nPath: %s", path), true);
	}

	const String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result(_source_root_missing_message(), true);
	}
	const String full_path = project_root.path_join(path);
	if (!FileAccess::exists(full_path)) {
		return _make_result(vformat("File edit rejected: file not found.\nPath: %s", path), true);
	}

	Error read_err = OK;
	const String content = FileAccess::get_file_as_string(full_path, &read_err);
	if (read_err != OK) {
		return _make_result(vformat("File edit rejected: failed to read file (err=%d).\nPath: %s", (int)read_err, path), true);
	}

	const int occurrence_count = content.count(old_string);
	if (occurrence_count == 0) {
		return _make_result(vformat("File edit rejected: old_string was not found.\nPath: %s", path), true);
	}
	if (occurrence_count > 1) {
		return _make_result(vformat("File edit rejected: old_string occurs %d times; provide more surrounding context so it matches exactly once.\nPath: %s", occurrence_count, path), true);
	}

	Dictionary write_args;
	write_args["path"] = path;
	write_args["content"] = content.replace(old_string, new_string);
	Dictionary result = _write_file(write_args);
	if (!bool(result.get("is_error", false))) {
		result["content"] = vformat("Successfully edited %s by replacing one exact match.", path);
	}
	return result;
}

Dictionary AIToolExecutor::_batch_tools(const Dictionary &p_args) {
	Array operations = p_args.get("operations", Array());
	if (operations.is_empty()) {
		return _make_result("No batch operations provided.", true);
	}

	const int MAX_BATCH_OPERATIONS = 8;
	StringBuilder sb;
	sb += vformat("Batch executed %d operation(s):\n", MIN(operations.size(), MAX_BATCH_OPERATIONS));
	bool has_error = false;

	for (int i = 0; i < operations.size() && i < MAX_BATCH_OPERATIONS; i++) {
		if (operations[i].get_type() != Variant::DICTIONARY) {
			has_error = true;
			sb += vformat("\n--- Operation %d ---\nError: operation must be an object.\n", i + 1);
			continue;
		}

		Dictionary op = operations[i];
		const String name = String(op.get("name", String())).strip_edges();
		if (name.is_empty()) {
			has_error = true;
			sb += vformat("\n--- Operation %d ---\nError: missing tool name.\n", i + 1);
			continue;
		}
		if (name == AIToolNames::BATCH_TOOLS) {
			has_error = true;
			sb += vformat("\n--- Operation %d: %s ---\nError: nested batch_tools calls are not allowed.\n", i + 1, name);
			continue;
		}
		AISettingsData settings = AISettings::load();
		if (settings.context_mode != AIContextMode::ENGINE &&
				(name == AIToolNames::RUN_BUILD ||
						name == AIToolNames::READ_BUILD_LOG ||
						name == AIToolNames::CHECK_BUILD_STATUS ||
						name == AIToolNames::RESTART_ENGINE ||
						name == AIToolNames::FETCH_URL ||
						name == AIToolNames::UPLOAD_CODE)) {
			has_error = true;
			sb += vformat("\n--- Operation %d: %s ---\nError: this tool is only available in engine mode.\n", i + 1, name);
			continue;
		}

		Dictionary op_args;
		Variant raw_args = op.get("arguments", op.get("args", Dictionary()));
		if (raw_args.get_type() == Variant::DICTIONARY) {
			op_args = raw_args;
		} else if (raw_args.get_type() == Variant::STRING) {
			Variant parsed_args = JSON::parse_string(String(raw_args));
			if (parsed_args.get_type() == Variant::DICTIONARY) {
				op_args = parsed_args;
			}
		}

		Dictionary fn;
		fn["name"] = name;
		fn["arguments"] = JSON::stringify(op_args);

		Dictionary tool_call;
		tool_call["id"] = vformat("batch_%d", i);
		tool_call["type"] = "function";
		tool_call["function"] = fn;

		Dictionary result = execute(tool_call);
		if (result.has("is_error")) {
			has_error = true;
		}

		sb += vformat("\n--- Operation %d: %s ---\n", i + 1, name);
		sb += String(result.get("content", String()));
		sb += "\n";
	}

	if (operations.size() > MAX_BATCH_OPERATIONS) {
		has_error = true;
		sb += vformat("\nBatch stopped after %d operations. Split remaining work into another batch.\n", MAX_BATCH_OPERATIONS);
	}

	return _make_result(sb.as_string(), has_error);
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

Dictionary AIToolExecutor::_list_files(const Dictionary &p_args) {
	String path = p_args.get("path", ".");
	if (path.is_empty()) {
		path = ".";
	}

	int max_depth = (int)p_args.get("depth", 1);
	if (max_depth < 0) {
		max_depth = 0;
	} else if (max_depth > 5) {
		max_depth = 5;
	}

	String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result(_source_root_missing_message(), true);
	}

	String rel_root = path.replace("\\", "/").simplify_path();
	if (rel_root == ".") {
		rel_root = "";
	}
	if (rel_root.is_absolute_path() || rel_root.begins_with("../") || rel_root == ".." || rel_root.contains("/../")) {
		return _make_result("Directory listing rejected: path must stay inside the project root.", true);
	}

	String root_dir = rel_root.is_empty() ? project_root : project_root.path_join(rel_root);
	Ref<DirAccess> root_da = DirAccess::open(root_dir);
	if (root_da.is_null()) {
		return _make_result("Directory not found: " + (rel_root.is_empty() ? String(".") : rel_root), true);
	}

	struct PendingDir {
		String abs_path;
		String rel_path;
		int depth = 0;
	};

	Vector<String> entries;
	List<PendingDir> dirs;
	PendingDir root;
	root.abs_path = root_dir;
	root.rel_path = rel_root;
	root.depth = 0;
	dirs.push_back(root);

	const int MAX_RESULTS = 200;
	while (!dirs.is_empty() && entries.size() < MAX_RESULTS) {
		PendingDir current = dirs.front()->get();
		dirs.pop_front();

		Ref<DirAccess> da = DirAccess::open(current.abs_path);
		if (da.is_null()) {
			continue;
		}

		da->list_dir_begin();
		String name = da->get_next();
		while (!name.is_empty() && entries.size() < MAX_RESULTS) {
			if (name == "." || name == "..") {
				name = da->get_next();
				continue;
			}

			const bool is_dir = da->current_is_dir();
			const String rel = current.rel_path.is_empty() ? name : current.rel_path.path_join(name);
			if (is_dir) {
				entries.push_back(rel + "/");
				if (current.depth < max_depth && !name.begins_with(".") && name != "bin" && name != "obj" && name != "__pycache__") {
					PendingDir child;
					child.abs_path = current.abs_path.path_join(name);
					child.rel_path = rel;
					child.depth = current.depth + 1;
					dirs.push_back(child);
				}
			} else {
				entries.push_back(rel);
			}

			name = da->get_next();
		}
		da->list_dir_end();
	}

	StringBuilder sb;
	sb += vformat("Listed %d item(s) under '%s' (depth %d):\n", entries.size(), rel_root.is_empty() ? String(".") : rel_root, max_depth);
	for (int i = 0; i < entries.size(); i++) {
		sb += entries[i] + "\n";
	}
	if (entries.size() >= MAX_RESULTS) {
		sb += "... truncated at 200 items. Use a narrower path or lower depth.\n";
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
	Dictionary build_result = build_state.get_result();
	const String build_status = build_result.get("status", "idle");
	const String build_output = build_result.get("stdout", String());
	if (build_status == "running") {
		return _make_result("Build is still running. Use check_build_status to wait for completion.");
	}
	if (bool(build_result.get("has_result", false)) && !build_output.is_empty()) {
		const int MAX_LOG_CHARS = 30000;
		const String visible_output = build_output.length() > MAX_LOG_CHARS ? build_output.substr(build_output.length() - MAX_LOG_CHARS) : build_output;
		return _make_result(vformat("Latest in-memory build output (status: %s, exit code: %d):\n\n%s", build_status, int(build_result.get("exit_code", -1)), visible_output));
	}

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

static void _restart_editor_on_main_thread(const String &p_executable_path) {
	EditorNode *editor = EditorNode::get_singleton();
	if (!editor) {
		ERR_PRINT("AI engine restart failed: EditorNode is unavailable.");
		return;
	}

	const Error state_err = AIRestartHelper::save_state();
	if (state_err != OK) {
		ERR_PRINT(vformat("AI engine restart could not save editor state (err=%d). Restarting with the standard editor session restore path.", (int)state_err));
	}

	// restart_editor() touches editor UI and configures OS restart-on-exit, so
	// it must run on the main thread rather than the AI tool worker thread.
	OS::get_singleton()->set_restart_executable_path(p_executable_path);
	editor->restart_editor(false);
}

Dictionary AIToolExecutor::_restart_engine(const Dictionary &p_args) {
	const Dictionary build_result = build_state.get_result();
	if (String(build_result.get("status", "idle")) != "done" || int(build_result.get("exit_code", -1)) != 0) {
		return _make_result("Engine switch rejected: no successful AI build is available. Run run_build and wait for check_build_status to report success first.", true);
	}

	const String executable_path = build_result.get("executable_path", String());
	if (executable_path.is_empty() || !FileAccess::exists(executable_path)) {
		return _make_result("Engine switch rejected: the executable produced by the successful AI build could not be found.", true);
	}

	callable_mp_static(&_restart_editor_on_main_thread).bind(executable_path).call_deferred();
	return _make_result("Engine switch scheduled. The current editor will close, then launch the AI-compiled editor: " + executable_path);
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
	if (!runtime->is_running_server(p_server_name)) {
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
