/*  ai_tool_defs.h                                                          */
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

#pragma once

#include "core/templates/vector.h"
#include "core/variant/array.h"

// Tool name constants.
namespace AIToolNames {
constexpr const char *READ_FILES = "read_files";
constexpr const char *WRITE_FILE = "write_file";
constexpr const char *SEARCH_FILES = "search_files";
constexpr const char *GREP_CODE = "grep_code";
constexpr const char *RUN_BUILD = "run_build";
constexpr const char *READ_BUILD_LOG = "read_build_log";
constexpr const char *FETCH_URL = "fetch_url";
constexpr const char *SHELL_COMMAND = "shell_command";
constexpr const char *RESTART_ENGINE = "restart_engine";
} // namespace AIToolNames

// Returns the built-in tool definitions as an Array of Dictionary,
// formatted for OpenAI/OpenRouter-compatible "tools" parameter.
class AIToolDefs {
public:
	// OpenAI-compatible tool definitions (the "tools" array in the API payload).
	static Array get_builtin_tools();

	// Tools derived from configured MCP server capabilities.
	static Array get_mcp_tools();
};
