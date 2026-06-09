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
				"Read the contents of one or more files from the project source tree. Returns each file's content or an error if a file is not found.",
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
				"Write or overwrite content to a file in the project source tree. Creates parent directories automatically. The previous version is backed up with a .bak suffix.",
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

	// 4. grep_code
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

	// 5. run_build
	{
		Dictionary props;
		props["extra_args"] = _str_property("Optional extra scons arguments, e.g. 'module_mono_enabled=yes'.");
		Dictionary fn = _make_fn(
				AIToolNames::RUN_BUILD,
				"Build the engine using scons. By default builds platform=windows target=editor. Returns the build output text and exit code.",
				props, Array());
		tools.push_back(_tool(AIToolNames::RUN_BUILD, "", fn));
	}

	// 6. read_build_log
	{
		Dictionary props;
		Dictionary fn = _make_fn(
				AIToolNames::READ_BUILD_LOG,
				"Read the most recent build log file from artifacts/logs/ to analyze build errors.",
				props, Array());
		tools.push_back(_tool(AIToolNames::READ_BUILD_LOG, "", fn));
	}

	// 7. fetch_url
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

	// 8. shell_command
	{
		Dictionary props;
		props["command"] = _str_property("Shell command to execute in the project root directory.");
		Array required;
		required.push_back("command");
		Dictionary fn = _make_fn(
				AIToolNames::SHELL_COMMAND,
				"Execute a shell command in the project root directory. Returns stdout, stderr, and the exit code. Use with caution.",
				props, required);
		tools.push_back(_tool(AIToolNames::SHELL_COMMAND, "", fn));
	}

	// 9. restart_engine
	{
		Dictionary props;
		Dictionary fn = _make_fn(
				AIToolNames::RESTART_ENGINE,
				"Restart the Jundot editor after a successful build. Saves the current editor state (open scenes, scripts) so they are restored when the editor reopens. Call this after run_build succeeds.",
				props, Array());
		tools.push_back(_tool(AIToolNames::RESTART_ENGINE, "", fn));
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

	for (const AIMCPServerEntry &server : mcp_servers) {
		if (!server.enabled) {
			continue;
		}

		if (server.capabilities_json.is_empty()) {
			continue;
		}

		// Parse capabilities JSON (array of tool definitions).
		Variant parsed = JSON::parse_string(server.capabilities_json);
		if (parsed.get_type() != Variant::ARRAY) {
			continue;
		}

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

			// Prefix tool name with MCP server name to avoid collisions.
			String tool_name = server.name + "." + cap_name;

			// Build OpenAI tool definition.
			Dictionary fn;
			fn["name"] = tool_name;
			fn["description"] = cap.get("description", String());

			Dictionary params;
			params["type"] = "object";
			params["properties"] = cap.get("inputSchema", cap.get("parameters", cap.get("input", Dictionary())));
			if (params["properties"].get_type() != Variant::DICTIONARY) {
				// Some MCP tools use a flat input format. Convert to OpenAI format.
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

	return tools;
}
