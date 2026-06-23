/*  ai_tool_defs.cpp                                                        */
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
#include "ai_tool_registry.h"
#include "ai_mcp_runtime.h"

#include "core/io/json.h"

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
		props["content"] = _str_property("Full file content to write. Creates the file if it doesn't exist; overwrites if it does.");
		Array required;
		required.push_back("path");
		required.push_back("content");
		Dictionary fn = _make_fn(
				AIToolNames::WRITE_FILE,
				"Write or overwrite content in the current tool root. In engine mode this is the configured JunDot source checkout; in project mode this is the open game project. Creates parent directories automatically. The previous version is backed up with a .bak suffix.",
				props, required);
		tools.push_back(_tool(AIToolNames::WRITE_FILE, "", fn));
	}

	// 3. search_files
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

	// 6. run_build
	{
		Dictionary props;
		props["extra_args"] = _str_property("Optional extra scons arguments, e.g. 'module_mono_enabled=yes'.");
		Dictionary fn = _make_fn(
				AIToolNames::RUN_BUILD,
				"Incrementally build the engine using scons in the configured JunDot source checkout or the default engine_source cache checkout. A packaged editor executable does not contain source code; if no source checkout with SConstruct is available, clone/download the source into the cache directory or set engine_source_root before retrying. By default builds platform=windows target=editor.",
				props, Array());
		tools.push_back(_tool(AIToolNames::RUN_BUILD, "", fn));
	}

	// 7. read_build_log
	{
		Dictionary props;
		Dictionary fn = _make_fn(
				AIToolNames::READ_BUILD_LOG,
				"Read the most recent build log file from artifacts/logs/ to analyze build errors.",
				props, Array());
		tools.push_back(_tool(AIToolNames::READ_BUILD_LOG, "", fn));
	}

	// 8. fetch_url
	{
		Dictionary props;
		props["url"] = _str_property("The full URL to download from.");
		props["dest_path"] = _str_property("Destination file path, relative to the project root.");
		Array required;
		required.push_back("url");
		required.push_back("dest_path");
		Dictionary fn = _make_fn(
				AIToolNames::FETCH_URL,
				"Download a file from a URL and save it to the local project tree. Creates parent directories as needed.",
				props, required);
		tools.push_back(_tool(AIToolNames::FETCH_URL, "", fn));
	}

	// 9. shell_command
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

	// 10. restart_engine
	{
		Dictionary props;
		Dictionary fn = _make_fn(
				AIToolNames::RESTART_ENGINE,
				"Restart the Jundot editor after a successful build. Saves the current editor state (open scenes, scripts) so they are restored when the editor reopens. Call this after run_build succeeds.",
				props, Array());
		tools.push_back(_tool(AIToolNames::RESTART_ENGINE, "", fn));
	}

	// 11. check_build_status
	{
		Dictionary props;
		Dictionary fn = _make_fn(
				AIToolNames::CHECK_BUILD_STATUS,
				"Check the status of a background build started by run_build. Returns 'running' if the build is still in progress, or the build output and exit code once it completes. Call this after run_build to get build results.",
				props, Array());
		tools.push_back(_tool(AIToolNames::CHECK_BUILD_STATUS, "", fn));
	}

	// 12. upload_code
	{
		Dictionary props;
		props["file_path"] = _str_property("File path relative to the project root to upload to the git remote repository.");
		props["commit_message"] = _str_property("Commit message describing the change.");
		Array required;
		required.push_back("file_path");
		required.push_back("commit_message");
		Dictionary fn = _make_fn(
				AIToolNames::UPLOAD_CODE,
				"Upload a modified file to the git remote repository. Runs security and universality checks before committing and pushing. Only works in ENGINE mode with a valid git repository.",
				props, required);
		tools.push_back(_tool(AIToolNames::UPLOAD_CODE, "", fn));
	}

	// 13. batch_tools
	{
		Dictionary op_props;
		op_props["name"] = _str_property("Tool name to execute, e.g. list_files, grep_code, read_files, write_file, shell_command.");
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

		// Try to get tools from runtime (dynamic discovery via tools/list)
		if (runtime->is_alive() && runtime->get_state() == MCPServerRuntime::ServerState::RUNNING) {
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

	for (int i = 0; i < all_tools.size(); i++) {
		Dictionary tool = all_tools[i];
		Dictionary fn = tool["function"];
		String name = fn["name"];
		if (engine_only.has(StringName(name)) && p_mode != AIContextMode::ENGINE) {
			continue;
		}
		filtered.push_back(tool);
	}

	return filtered;
}
