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
#include "ai_restart_helper.h"
#include "ai_tool_registry.h"

#include "core/error/error_macros.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/string/string_builder.h"
#include "editor/editor_node.h"

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
	} else if (name == AIToolNames::READ_BUILD_LOG) {
		result = _read_build_log(args);
	} else if (name == AIToolNames::FETCH_URL) {
		result = _fetch_url(args);
	} else if (name == AIToolNames::SHELL_COMMAND) {
		result = _shell_command(args);
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
	// Use OS::get_singleton()->get_executable_path() and go up to the repo root.
	String exe_path = OS::get_singleton()->get_executable_path();
	// Try detecting via a known subdirectory.
	String probe = exe_path.get_base_dir().path_join("SConstruct");
	if (FileAccess::exists(probe)) {
		return exe_path.get_base_dir();
	}
	// Walk up looking for SConstruct.
	String dir = exe_path.get_base_dir();
	for (int i = 0; i < 10; i++) {
		if (FileAccess::exists(dir.path_join("SConstruct"))) {
			return dir;
		}
		dir = dir.get_base_dir();
	}
	return exe_path.get_base_dir();
}

// ---- Tool implementations ----

Dictionary AIToolExecutor::_read_files(const Dictionary &p_args) {
	Array paths = p_args.get("paths", Array());
	if (paths.is_empty()) {
		return _make_result("No file paths provided.", true);
	}

	String project_root = _get_project_root();
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

	String project_root = _get_project_root();
	String full_path = project_root.path_join(path);

	// Backup existing file.
	if (FileAccess::exists(full_path)) {
		String bak_path = full_path + ".bak";
		// Remove old backup.
		if (FileAccess::exists(bak_path)) {
			Ref<DirAccess> da = DirAccess::create_for_path(bak_path);
			if (da.is_valid()) {
				da->remove(bak_path);
			}
		}
		// Rename current file to backup.
		Ref<DirAccess> da = DirAccess::create_for_path(full_path);
		if (da.is_valid()) {
			da->rename(full_path, bak_path);
		}
	}

	// Create parent directories.
	Ref<DirAccess> da = DirAccess::create_for_path(full_path);
	if (da.is_valid()) {
		Error mkdir_err = da->make_dir_recursive(full_path.get_base_dir());
		if (mkdir_err != OK) {
			return _make_result(vformat("Failed to create directory for %s", path), true);
		}
	}

	// Write file.
	Error err = OK;
	Ref<FileAccess> file = FileAccess::open(full_path, FileAccess::WRITE, &err);
	if (err != OK || file.is_null()) {
		return _make_result(vformat("Failed to open file for writing: %s", path), true);
	}

	file->store_string(content);
	file.unref();

	return _make_result(vformat("Successfully wrote %s (%d bytes).", path, content.utf8().length()));
}

Dictionary AIToolExecutor::_search_files(const Dictionary &p_args) {
	String pattern = p_args.get("pattern", String());
	if (pattern.is_empty()) {
		return _make_result("No search pattern provided.", true);
	}

	String project_root = _get_project_root();

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
			// ** pattern — search from root.
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

	String project_root = _get_project_root();

	// Build the scons command.
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
		// Parse extra args by space.
		Vector<String> extras = extra_args.split(" ", false);
		for (int i = 0; i < extras.size(); i++) {
			scons_args.push_back(extras[i]);
		}
	}

	String std_out;
	int exit_code = -1;

	// Try python -m SCons first, then scons.
	Error err = OS::get_singleton()->execute("python", scons_args, &std_out, &exit_code, true);
	if (err != OK) {
		// Fallback: try "scons" directly or "python3".
		List<String> py_args;
		py_args.push_back("-m");
		py_args.push_back("SCons");
		for (const String &arg : scons_args) {
			py_args.push_back(arg);
		}
		err = OS::get_singleton()->execute("python3", py_args, &std_out, &exit_code, true);
	}

	StringBuilder result;
	result += vformat("Build exit code: %d\n", exit_code);
	if (!std_out.is_empty()) {
		result += "\n--- stdout ---\n" + std_out;
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

#ifdef WINDOWS_ENABLED
	// On Windows, use cmd /c for shell commands.
	List<String> cmd_args;
	cmd_args.push_back("/c");
	cmd_args.push_back(command);
	Error err = OS::get_singleton()->execute("cmd", cmd_args, &std_out, &exit_code, true);
#else
	Error err = OS::get_singleton()->execute(program, shell_args, &std_out, &exit_code, true);
#endif

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

	// Build the JSON-RPC request.
	Dictionary rpc_request;
	rpc_request["jsonrpc"] = "2.0";
	rpc_request["id"] = 1;
	rpc_request["method"] = "tools/call";

	Dictionary rpc_params;
	rpc_params["name"] = p_tool_name;

	// Parse arguments JSON.
	Variant parsed_args = JSON::parse_string(p_args_json);
	if (parsed_args.get_type() == Variant::DICTIONARY) {
		rpc_params["arguments"] = parsed_args;
	} else {
		rpc_params["arguments"] = Dictionary();
	}

	rpc_request["params"] = rpc_params;

	String request_body = JSON::stringify(rpc_request);

	// Launch MCP server as a subprocess.
	String command = target_server.command;
	if (command.is_empty()) {
		return _make_result(vformat("MCP server '%s' has no command configured.", p_server_name), true);
	}

	List<String> mcp_args;
	if (!target_server.arguments.is_empty()) {
		// Parse space-separated arguments.
		Vector<String> arg_parts = target_server.arguments.split(" ", false);
		for (int i = 0; i < arg_parts.size(); i++) {
			mcp_args.push_back(arg_parts[i]);
		}
	}

	String std_out;
	int exit_code = -1;

	// Execute the MCP server with the request piped via stdin.
	Error err = OS::get_singleton()->execute(command, mcp_args, &std_out, &exit_code, true);
	if (err != OK) {
		return _make_result(vformat("Failed to start MCP server '%s': err=%d", p_server_name, (int)err), true);
	}

	// The MCP protocol uses stdin/stdout. OS::execute captures stdout,
	// but we also need stdin. For now, we execute the command and check output.
	// A more robust implementation would use the url field for HTTP-based MCP.
	if (!target_server.url.is_empty()) {
		// HTTP-based MCP: send request via HTTPRequest to the server URL.
		// This is deferred — the current implementation uses subprocess only.
		return _make_result(vformat("HTTP-based MCP server '%s' is not yet supported via Function Calling. Use subprocess-based MCP servers instead.", p_server_name), true);
	}

	// Parse the response. The stdout should contain a JSON-RPC response.
	if (std_out.is_empty()) {
		return _make_result(vformat("MCP tool '%s.%s' returned empty response.", p_server_name, p_tool_name), true);
	}

	// Parse JSON-RPC response.
	Variant response = JSON::parse_string(std_out);
	if (response.get_type() != Variant::DICTIONARY) {
		return _make_result(vformat("MCP tool '%s.%s' returned invalid JSON: %s", p_server_name, p_tool_name, std_out.substr(0, 500)), true);
	}

	Dictionary rpc = response;

	// Check for JSON-RPC error.
	if (rpc.has("error") && rpc["error"].get_type() == Variant::DICTIONARY) {
		Dictionary error_obj = rpc["error"];
		String err_msg = error_obj.get("message", "Unknown error");
		return _make_result(vformat("MCP tool '%s.%s' error: %s", p_server_name, p_tool_name, err_msg), true);
	}

	// Extract the result content.
	if (rpc.has("result")) {
		Variant result_data = rpc["result"];
		if (result_data.get_type() == Variant::DICTIONARY) {
			Dictionary result_dict = result_data;
			// MCP tools typically return content in content[0].text or a data field.
			Variant content = result_dict.get("content", result_dict);
			return _make_result(JSON::stringify(content));
		}
		return _make_result(JSON::stringify(result_data));
	}

	return _make_result(vformat("MCP tool '%s.%s' returned result with no recognizable data.", p_server_name, p_tool_name), true);
}
