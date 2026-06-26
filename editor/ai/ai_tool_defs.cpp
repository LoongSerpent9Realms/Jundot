/*  ai_tool_defs.cpp                                                      */
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

#include "ai_tool_defs.h"

#include "core/io/json.h"
#include "editor/ai/ai_mcp_runtime.h"
#include "editor/ai/ai_tool_registry.h"

static Dictionary _make_fn(const String &p_name, const String &p_description, const Dictionary &p_parameters, const Array &p_required) {
	Dictionary fn;
	fn["name"] = p_name;
	fn["description"] = p_description;

	// OpenAI-compatible tool definitions require:
	//   parameters = { type: "object", properties: {...}, required?: [...] }
	Dictionary params;
	params["type"] = "object";
	params["properties"] = p_parameters.duplicate();
	if (!p_required.is_empty()) {
		params["required"] = p_required;
	}
	fn["parameters"] = params;

	return fn;
}

static Dictionary _str_property(const String &p_description) {
	Dictionary prop;
	prop["type"] = "string";
	prop["description"] = p_description;
	return prop;
}

static Dictionary _array_str_property(const String &p_description) {
	Dictionary prop;
	prop["type"] = "array";
	prop["items"] = _str_property(p_description);
	return prop;
}

static Dictionary _array_object_property(const String &p_description, const Dictionary &p_item_properties, const Array &p_required) {
	Dictionary item;
	item["type"] = "object";
	item["properties"] = p_item_properties;
	if (!p_required.is_empty()) {
		item["required"] = p_required;
	}

	Dictionary prop;
	prop["type"] = "array";
	prop["description"] = p_description;
	prop["items"] = item;
	return prop;
}

static Dictionary _number_property(const String &p_description) {
	Dictionary prop;
	prop["type"] = "number";
	prop["description"] = p_description;
	return prop;
}

static Dictionary _tool(const String &p_name, const String &p_description, const Dictionary &p_fn) {
	Dictionary tool;
	tool["type"] = "function";
	tool["function"] = p_fn;
	return tool;
}

Array AIToolDefs::get_builtin_tools() {
	Array tools;

	// 1. read_files
	{
		Dictionary props;
		props["paths"] = _array_str_property("File path(s) relative to the project root to read.");
		Array required;
		required.push_back("paths");
		Dictionary fn = _make_fn(
				AIToolNames::READ_FILES,
				"Read the contents of one or more files from the current tool root. In engine mode this is the configured JunDot source checkout; in project mode this is the open game project. Returns each file's content or an error if a file is not found.",
				props, required);
		tools.push_back(_tool(AIToolNames::READ_FILES, "", fn));
	}

	// 2. write_file
	{
		Dictionary props;
		props["path"] = _str_property("File path relative to the project root.");
		props["content"] = _str_property("Complete non-empty file content to write. Creates the file if it does not exist; overwrites if it does. Empty content is rejected to prevent accidental truncation.");
		Array required;
		required.push_back("path");
		required.push_back("content");
		Dictionary fn = _make_fn(
				AIToolNames::WRITE_FILE,
				"Write or overwrite content in the current tool root. In engine mode this is the configured JunDot source checkout; in project mode this is the open game project. Creates parent directories automatically. The previous version is backed up with a .bak suffix.",
				props, required);
		tools.push_back(_tool(AIToolNames::WRITE_FILE, "", fn));
	}

	// 3. edit_file
	{
		Dictionary props;
		props["path"] = _str_property("File path relative to the project root.");
		props["old_string"] = _str_property("Exact existing text to replace. It must occur exactly once in the file.");
		props["new_string"] = _str_property("Replacement text.");
		Array required;
		required.push_back("path");
		required.push_back("old_string");
		required.push_back("new_string");
		Dictionary fn = _make_fn(
				AIToolNames::EDIT_FILE,
				"Edit one file by replacing an exact, unique old_string with new_string. Use this for localized changes instead of overwriting the complete file.",
				props, required);
		tools.push_back(_tool(AIToolNames::EDIT_FILE, "", fn));
	}

	// 4. search_files
	{
		Dictionary props;
		props["pattern"] = _str_property("Glob pattern to search for, e.g. '**/*.cpp', 'src/**/*.h'.");
		Array required;
		required.push_back("pattern");
		Dictionary fn = _make_fn(
				AIToolNames::SEARCH_FILES,
				"Search for files matching a glob pattern in the project tree. Returns a list of matching file paths.",
				props, required);
		tools.push_back(_tool(AIToolNames::SEARCH_FILES, "", fn));
	}

	// 4. list_files
	{
		Dictionary props;
		props["path"] = _str_property("Directory path relative to the project root to list. Use '.' for the root.");
		props["depth"] = _number_property("Maximum directory depth to include. Defaults to 1 and is capped at 5.");
		Array required;
		required.push_back("path");
		Dictionary fn = _make_fn(
				AIToolNames::LIST_FILES,
				"List files and directories under a directory in the current tool root. Use this to inspect project structure before choosing exact files to read.",
				props, required);
		tools.push_back(_tool(AIToolNames::LIST_FILES, "", fn));
	}

	// 5. grep_code
	{
		Dictionary props;
		props["pattern"] = _str_property("Regular expression pattern to search for in file contents.");
		props["glob"] = _str_property("Optional file glob filter, e.g. '*.cpp' to only search C++ source files.");
		Array required;
		required.push_back("pattern");
		Dictionary fn = _make_fn(
				AIToolNames::GREP_CODE,
				"Search for a pattern in file contents across the project. Returns file paths and matching lines with line numbers.",
				props, required);
		tools.push_back(_tool(AIToolNames::GREP_CODE, "", fn));
	}

	// 6. check_project_scripts
	{
		Dictionary props;
		props["paths"] = _array_str_property("Optional script path(s) relative to the project root to validate, e.g. 'scripts/player.gd' or 'res://scripts/player.gd'. If omitted, all project GDScript files are syntax-checked and C# projects are built when present.");
		Dictionary fn = _make_fn(
				AIToolNames::CHECK_PROJECT_SCRIPTS,
				"Validate project scripts after creating or editing them. In PROJECT mode, syntax-checks GDScript files with the current editor executable in headless check-only mode and, when a C# project is present, runs dotnet build in the open project directory. Returns compiler/parser output so the AI can fix errors and validate again.",
				props, Array());
		tools.push_back(_tool(AIToolNames::CHECK_PROJECT_SCRIPTS, "", fn));
	}

	// 7. run_build
	{
		Dictionary props;
		props["extra_args"] = _str_property("Optional extra scons arguments, e.g. 'module_mono_enabled=yes'.");
		Dictionary fn = _make_fn(
				AIToolNames::RUN_BUILD,
				"Check the configured Git source checkout for upstream updates, preserve local changes, automatically merge updates with local-first conflict handling, then incrementally build the engine using scons. A packaged editor executable does not contain source code; if no source checkout with SConstruct is available, clone/download the source into the cache directory or set engine_source_root before retrying. By default builds platform=windows target=editor with module_mono_enabled=no so the generated editor can restart without separately built .NET assemblies.",
				props, Array());
		tools.push_back(_tool(AIToolNames::RUN_BUILD, "", fn));
	}

	// 8. read_build_log
	{
		Dictionary props;
		Dictionary fn = _make_fn(
				AIToolNames::READ_BUILD_LOG,
				"Read the most recent build log file from artifacts/logs/ to analyze build errors.",
				props, Array());
		tools.push_back(_tool(AIToolNames::READ_BUILD_LOG, "", fn));
	}

	// 9. fetch_url
	{
		Dictionary props;
		props["url"] = _str_property("The full URL to download from.");
		props["dest_path"] = _str_property("Destination file path, relative to the project root.");
		Array required;
		required.push_back("url");
		required.push_back("dest_path");
		Dictionary fn = _make_fn(
				AIToolNames::FETCH_URL,
				"Download a URL and save it in the current tool root. In PROJECT mode this is restricted to official Steam or Epic Games Store research pages and destinations under .JundotAI/research/. Use it to verify reference games before proposing differentiators. In ENGINE mode it may also fetch development dependencies.",
				props, required);
		tools.push_back(_tool(AIToolNames::FETCH_URL, "", fn));
	}

	// 10. shell_command
	{
		Dictionary props;
		props["command"] = _str_property("Shell command to execute in the project root directory.");
		Array required;
		required.push_back("command");
		Dictionary fn = _make_fn(
				AIToolNames::SHELL_COMMAND,
				"Execute a shell command in the current tool root directory. In engine mode this is the configured source checkout when available; if no source exists yet, it runs in the default engine_source cache directory so the source can be cloned/downloaded. In project mode this is the open game project. Returns stdout, stderr, and the exit code. Use with caution.",
				props, required);
		tools.push_back(_tool(AIToolNames::SHELL_COMMAND, "", fn));
	}

	// 11. restart_engine
	{
		Dictionary props;
		Dictionary fn = _make_fn(
				AIToolNames::RESTART_ENGINE,
				"Restart the Jundot editor after a successful build. Saves the current editor state (open scenes, scripts) so they are restored when the editor reopens. Call this after run_build succeeds.",
				props, Array());
		tools.push_back(_tool(AIToolNames::RESTART_ENGINE, "", fn));
	}

	// 12. check_build_status
	{
		Dictionary props;
		Dictionary fn = _make_fn(
				AIToolNames::CHECK_BUILD_STATUS,
				"Check the status of a background build started by run_build. Returns 'running' if the build is still in progress, or the build output and exit code once it completes. Call this after run_build to get build results.",
				props, Array());
		tools.push_back(_tool(AIToolNames::CHECK_BUILD_STATUS, "", fn));
	}

	// 13. upload_code
	{
		Dictionary props;
		props["file_path"] = _str_property("File path relative to the project root to upload to the git remote repository.");
		props["commit_message"] = _str_property("Commit message describing the change.");
		Array required;
		required.push_back("file_path");
		required.push_back("commit_message");
		Dictionary fn = _make_fn(
				AIToolNames::UPLOAD_CODE,
				"Upload a modified file to the git remote repository. Before committing and pushing, validates repository formatting, code quality, security, and configured universality threshold. Any failed gate blocks the upload. Only works in ENGINE mode with a valid git repository.",
				props, required);
		tools.push_back(_tool(AIToolNames::UPLOAD_CODE, "", fn));
	}

	// 14. develop_ai_verify
	{
		Dictionary props;
		Dictionary passed;
		passed["type"] = "boolean";
		passed["description"] = "Whether AI validation passed after reviewing the user feedback and available evidence.";
		props["passed"] = passed;
		props["summary"] = _str_property("AI validation findings and evidence.");
		Array required;
		required.push_back("passed");
		required.push_back("summary");
		Dictionary fn = _make_fn(
				AIToolNames::DEVELOP_AI_VERIFY,
				"Record the AI verification stage of a Develop Mode demonstration after the user has tested the restarted editor. This never uploads code.",
				props, required);
		tools.push_back(_tool(AIToolNames::DEVELOP_AI_VERIFY, "", fn));
	}

	// 15. setup_engine_workspace
	{
		Dictionary props;
		props["workspace_name"] = _str_property("Short project-specific engine workspace name. If omitted, the open project directory name is used.");
		props["provider"] = _str_property("Remote provider to record: local, github, or gitee. This does not store credentials.");
		props["remote_url"] = _str_property("Optional GitHub/Gitee/git remote URL to attach as the project-engine remote. Authentication uses the user's existing git credentials.");
		props["branch"] = _str_property("Optional engine branch name. Defaults to project/<workspace_name>.");
		props["base_ref"] = _str_property("Optional base branch/ref for a new workspace branch. Defaults to HEAD of the configured engine source.");
		Array required;
		Dictionary fn = _make_fn(
				AIToolNames::SETUP_ENGINE_WORKSPACE,
				"PROJECT mode only. Create or bind a project-specific JunDot engine worktree and branch, optionally attach a GitHub/Gitee remote URL using the user's existing git credentials, save the mapping in .JundotAI/engine_workspace.json, and point engine mode at that workspace.",
				props, required);
		tools.push_back(_tool(AIToolNames::SETUP_ENGINE_WORKSPACE, "", fn));
	}

	// 16. request_engine_change
	{
		Dictionary props;
		props["reason"] = _str_property("The exact project requirement or engine limitation that makes an engine change necessary.");
		props["required_change"] = _str_property("The engine behavior, API, editor feature, or runtime capability that should be modified.");
		props["project_work_done"] = _str_property("Optional summary of safe project-side work already completed before switching.");
		Array required;
		required.push_back("reason");
		required.push_back("required_change");
		Dictionary fn = _make_fn(
				AIToolNames::REQUEST_ENGINE_CHANGE,
				"PROJECT mode only. Request a controlled switch to ENGINE mode when the project task genuinely requires engine source changes. The editor will preserve the conversation, switch modes, continue with engine tools, and expect return_to_project_mode after the engine work is verified.",
				props, required);
		tools.push_back(_tool(AIToolNames::REQUEST_ENGINE_CHANGE, "", fn));
	}

	// 17. return_to_project_mode
	{
		Dictionary props;
		props["summary"] = _str_property("Summary of the engine change, validation result, and what the project-side continuation should do next.");
		Array required;
		required.push_back("summary");
		Dictionary fn = _make_fn(
				AIToolNames::RETURN_TO_PROJECT_MODE,
				"ENGINE mode only. Return to PROJECT mode after the requested engine change has been completed and validated, so the AI can continue or finish the original game-project task in the project context.",
				props, required);
		tools.push_back(_tool(AIToolNames::RETURN_TO_PROJECT_MODE, "", fn));
	}

	// 18. batch_tools
	{
		Dictionary op_props;
		op_props["name"] = _str_property("Tool name to execute, e.g. list_files, grep_code, read_files, write_file, check_project_scripts, shell_command.");
		op_props["arguments"] = _str_property("JSON object string for the named tool's arguments, e.g. {\"paths\":[\"editor/ai/ai_chat_panel.cpp\"]}.");

		Array op_required;
		op_required.push_back("name");
		op_required.push_back("arguments");

		Dictionary props;
		props["operations"] = _array_object_property("Ordered tool operations to execute in one editor-side batch. Use this to combine independent reads/searches/writes and reduce AI request round trips.", op_props, op_required);

		Array required;
		required.push_back("operations");
		Dictionary fn = _make_fn(
				AIToolNames::BATCH_TOOLS,
				"Execute multiple editor tools locally in one tool call and return all results together. Prefer this for independent exploration steps such as list_files + grep_code + read_files, or multiple write_file operations. The arguments field of each operation must be a JSON object encoded as a string. Do not nest batch_tools inside itself.",
				props, required);
		tools.push_back(_tool(AIToolNames::BATCH_TOOLS, "", fn));
	}

	return tools;
}

Array AIToolDefs::get_mcp_tools() {
	Array tools;
	Vector<AISkillEntry> skills;
	Vector<AIMCPServerEntry> mcp_servers;
	if (AIToolRegistry::load(skills, mcp_servers) != OK) {
		return tools;
	}

	MCPServerRuntime *runtime = MCPServerRuntime::get_singleton();

	for (const AIMCPServerEntry &server : mcp_servers) {
		if (!server.enabled) {
			continue;
		}

		bool has_runtime_tools = false;

		// Discover tools on demand. URL-only servers still require a configured
		// stdio bridge command until the runtime supports Streamable HTTP natively.
		if (!runtime->is_running_server(server.name) && !server.command.is_empty()) {
			runtime->start(server);
		}

		// Try to get tools from runtime (dynamic discovery via tools/list)
		if (runtime->is_running_server(server.name)) {
			Array runtime_tools = runtime->get_tools();
			if (!runtime_tools.is_empty()) {
				for (int i = 0; i < runtime_tools.size(); i++) {
					if (runtime_tools[i].get_type() == Variant::DICTIONARY) {
						Dictionary rt = runtime_tools[i];
						Dictionary tool;
						tool["type"] = "function";
						tool["function"] = rt;
						tool["x_mcp_server_name"] = server.name;
						tool["x_mcp_server_command"] = server.command;
						tool["x_mcp_server_args"] = server.arguments;
						tools.push_back(tool);
						has_runtime_tools = true;
					}
				}
			}
		}

		// Fallback to static capabilities_json if runtime has no tools
		if (!has_runtime_tools && !server.capabilities_json.is_empty()) {
			Variant parsed = JSON::parse_string(server.capabilities_json);
			if (parsed.get_type() == Variant::ARRAY) {
				Array caps = parsed;
				for (int i = 0; i < caps.size(); i++) {
					if (caps[i].get_type() != Variant::DICTIONARY) {
						continue;
					}

					Dictionary cap = caps[i];
					String cap_name = cap.get("name", String());
					if (cap_name.is_empty()) {
						continue;
					}

					String tool_name = server.name + "." + cap_name;

					Dictionary fn;
					fn["name"] = tool_name;
					fn["description"] = cap.get("description", String());

					Dictionary params;
					params["type"] = "object";
					params["properties"] = cap.get("inputSchema", cap.get("parameters", cap.get("input", Dictionary())));
					if (params["properties"].get_type() != Variant::DICTIONARY) {
						params["properties"] = _str_property("Tool input as a JSON string.");
					}

					fn["parameters"] = params;

					Dictionary tool;
					tool["type"] = "function";
					tool["function"] = fn;
					tool["x_mcp_server_name"] = server.name;
					tool["x_mcp_server_command"] = server.command;
					tool["x_mcp_server_args"] = server.arguments;
					tools.push_back(tool);
				}
			}
		}
	}

	return tools;
}

Array AIToolDefs::get_tools_for_mode(AIContextMode p_mode) {
	Array all_tools = get_builtin_tools();
	Array filtered;

	HashSet<StringName> engine_only;
	engine_only.insert(StringName(AIToolNames::RUN_BUILD));
	engine_only.insert(StringName(AIToolNames::READ_BUILD_LOG));
	engine_only.insert(StringName(AIToolNames::CHECK_BUILD_STATUS));
	engine_only.insert(StringName(AIToolNames::RESTART_ENGINE));
	engine_only.insert(StringName(AIToolNames::FETCH_URL));
	engine_only.insert(StringName(AIToolNames::UPLOAD_CODE));
	engine_only.insert(StringName(AIToolNames::DEVELOP_AI_VERIFY));
	engine_only.insert(StringName(AIToolNames::RETURN_TO_PROJECT_MODE));

	HashSet<StringName> project_only;
	project_only.insert(StringName(AIToolNames::CHECK_PROJECT_SCRIPTS));
	project_only.insert(StringName(AIToolNames::SETUP_ENGINE_WORKSPACE));
	project_only.insert(StringName(AIToolNames::REQUEST_ENGINE_CHANGE));

	for (int i = 0; i < all_tools.size(); i++) {
		Dictionary tool = all_tools[i];
		Dictionary fn = tool["function"];
		String name = fn["name"];
		if (project_only.has(StringName(name)) && p_mode != AIContextMode::PROJECT) {
			continue;
		}
		if (engine_only.has(StringName(name)) && p_mode != AIContextMode::ENGINE) {
			continue;
		}
		filtered.push_back(tool);
	}

	return filtered;
}
