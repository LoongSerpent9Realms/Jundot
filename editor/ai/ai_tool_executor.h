/*  ai_tool_executor.h                                                    */
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

#include "core/error/error_list.h"
#include "core/string/ustring.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

// Executes tool calls returned by the LLM in a Function Calling response.
// Each tool_call is a Dictionary with "id", "type", "function" keys.
// AIToolExecutor executes the tool and returns a tool result Dictionary
// suitable for use in the "tool" role message.
class AIToolExecutor {
public:
	// Execute a single tool call.
	// p_tool_call: Dictionary with {id, type, function: {name, arguments}}.
	// Returns a Dictionary with {role: "tool", tool_call_id: p_id, content: "..."}.
	static Dictionary execute(const Dictionary &p_tool_call);

private:
	static Dictionary _read_files(const Dictionary &p_args);
	static Dictionary _write_file(const Dictionary &p_args);
	static Dictionary _edit_file(const Dictionary &p_args);
	static Dictionary _search_files(const Dictionary &p_args);
	static Dictionary _list_files(const Dictionary &p_args);
	static Dictionary _grep_code(const Dictionary &p_args);
	static Dictionary _check_project_scripts(const Dictionary &p_args);
	static Dictionary _check_html_prototype(const Dictionary &p_args);
	static Dictionary _check_ui_layout(const Dictionary &p_args);
	static Dictionary _create_3d_scene(const Dictionary &p_args);
	static Dictionary _add_3d_object(const Dictionary &p_args);
	static Dictionary _add_3d_light(const Dictionary &p_args);
	static Dictionary _check_3d_scene(const Dictionary &p_args);
	static Dictionary _build_project(const Dictionary &p_args);
	static Dictionary _build_cpp_hot_module(const Dictionary &p_args);
	static Dictionary _reload_cpp_hot_module(const Dictionary &p_args);
	static Dictionary _package_project(const Dictionary &p_args);
	static Dictionary _check_package_status(const Dictionary &p_args);
	static Dictionary _test_package(const Dictionary &p_args);
	static Dictionary _capture_package_screenshot(const Dictionary &p_args);
	static Dictionary _play_scene(const Dictionary &p_args);
	static Dictionary _click_ui_position(const Dictionary &p_args);
	static Dictionary _click_ui_node(const Dictionary &p_args);
	static Dictionary _assert_node_visible(const Dictionary &p_args);
	static Dictionary _assert_no_runtime_errors(const Dictionary &p_args);
	static Dictionary _capture_game_screenshot(const Dictionary &p_args);
	static Dictionary _capture_runtime_ui_snapshot(const Dictionary &p_args);
	static Dictionary _stop_play_scene(const Dictionary &p_args);
	static Dictionary _run_build(const Dictionary &p_args);
	static Dictionary _check_build_status(const Dictionary &p_args);
	static Dictionary _read_build_log(const Dictionary &p_args);
	static Dictionary _fetch_url(const Dictionary &p_args);
	static Dictionary _shell_command(const Dictionary &p_args);
	static Dictionary _restart_engine(const Dictionary &p_args);
	static Dictionary _upload_code(const Dictionary &p_args);
	static Dictionary _develop_ai_verify(const Dictionary &p_args);
	static Dictionary _setup_engine_workspace(const Dictionary &p_args);
	static Dictionary _request_engine_change(const Dictionary &p_args);
	static Dictionary _return_to_project_mode(const Dictionary &p_args);
	static Dictionary _add_physics(const Dictionary &p_args);
	static Dictionary _add_animation(const Dictionary &p_args);
	static Dictionary _add_particles(const Dictionary &p_args);
	static Dictionary _add_vfx(const Dictionary &p_args);
	static Dictionary _add_character_controller(const Dictionary &p_args);
	static Dictionary _remove_node(const Dictionary &p_args);
	static Dictionary _modify_node_properties(const Dictionary &p_args);
	static Dictionary _connect_signal(const Dictionary &p_args);
	static Dictionary _duplicate_node(const Dictionary &p_args);
	static Dictionary _reparent_node(const Dictionary &p_args);
	static Dictionary _batch_tools(const Dictionary &p_args);
	static Dictionary _execute_mcp_tool(const String &p_server_name, const String &p_tool_name, const String &p_args_json);

	static Dictionary _make_result(const String &p_content, bool p_is_error = false);

public:
	static String _get_project_root();
};
