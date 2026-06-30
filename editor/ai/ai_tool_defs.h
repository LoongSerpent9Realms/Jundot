/*  ai_tool_defs.h                                                        */
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
#include "editor/ai/ai_settings.h"

// Tool name constants.
namespace AIToolNames {
constexpr const char *READ_FILES = "read_files";
constexpr const char *WRITE_FILE = "write_file";
constexpr const char *EDIT_FILE = "edit_file";
constexpr const char *SEARCH_FILES = "search_files";
constexpr const char *LIST_FILES = "list_files";
constexpr const char *GREP_CODE = "grep_code";
constexpr const char *CHECK_PROJECT_SCRIPTS = "check_project_scripts";
constexpr const char *CHECK_HTML_PROTOTYPE = "check_html_prototype";
constexpr const char *CHECK_UI_LAYOUT = "check_ui_layout";
constexpr const char *CREATE_3D_SCENE = "create_3d_scene";
constexpr const char *ADD_3D_OBJECT = "add_3d_object";
constexpr const char *ADD_3D_LIGHT = "add_3d_light";
constexpr const char *CHECK_3D_SCENE = "check_3d_scene";
constexpr const char *BUILD_PROJECT = "build_project";
constexpr const char *BUILD_CPP_HOT_MODULE = "build_cpp_hot_module";
constexpr const char *RELOAD_CPP_HOT_MODULE = "reload_cpp_hot_module";
constexpr const char *PACKAGE_PROJECT = "package_project";
constexpr const char *CHECK_PACKAGE_STATUS = "check_package_status";
constexpr const char *TEST_PACKAGE = "test_package";
constexpr const char *PLAY_SCENE = "play_scene";
constexpr const char *CLICK_UI_POSITION = "click_ui_position";
constexpr const char *CLICK_UI_NODE = "click_ui_node";
constexpr const char *ASSERT_NODE_VISIBLE = "assert_node_visible";
constexpr const char *ASSERT_NO_RUNTIME_ERRORS = "assert_no_runtime_errors";
constexpr const char *CAPTURE_GAME_SCREENSHOT = "capture_game_screenshot";
constexpr const char *CAPTURE_RUNTIME_UI_SNAPSHOT = "capture_runtime_ui_snapshot";
constexpr const char *STOP_PLAY_SCENE = "stop_play_scene";
constexpr const char *RUN_BUILD = "run_build";
constexpr const char *READ_BUILD_LOG = "read_build_log";
constexpr const char *FETCH_URL = "fetch_url";
constexpr const char *SHELL_COMMAND = "shell_command";
constexpr const char *RESTART_ENGINE = "restart_engine";
constexpr const char *CHECK_BUILD_STATUS = "check_build_status";
constexpr const char *UPLOAD_CODE = "upload_code";
constexpr const char *DEVELOP_AI_VERIFY = "develop_ai_verify";
constexpr const char *SETUP_ENGINE_WORKSPACE = "setup_engine_workspace";
constexpr const char *REQUEST_ENGINE_CHANGE = "request_engine_change";
constexpr const char *RETURN_TO_PROJECT_MODE = "return_to_project_mode";
constexpr const char *BATCH_TOOLS = "batch_tools";
constexpr const char *ADD_PHYSICS = "add_physics";
constexpr const char *ADD_ANIMATION = "add_animation";
constexpr const char *ADD_PARTICLES = "add_particles";
constexpr const char *ADD_VFX = "add_vfx";
constexpr const char *ADD_CHARACTER_CONTROLLER = "add_character_controller";
constexpr const char *REMOVE_NODE = "remove_node";
constexpr const char *MODIFY_NODE_PROPERTIES = "modify_node_properties";
constexpr const char *CONNECT_SIGNAL = "connect_signal";
constexpr const char *DUPLICATE_NODE = "duplicate_node";
constexpr const char *REPARENT_NODE = "reparent_node";
} // namespace AIToolNames

// Returns the built-in tool definitions as an Array of Dictionary,
// formatted for OpenAI/OpenRouter-compatible "tools" parameter.
class AIToolDefs {
public:
	// OpenAI-compatible tool definitions (the "tools" array in the API payload).
	static Array get_builtin_tools();

	// Tools derived from configured MCP server capabilities.
	static Array get_mcp_tools();

	// Returns the tools available for a specific context mode.
	// - PROJECT: project file tools + shell_command + setup_engine_workspace + request_engine_change (no build tools).
	// - ENGINE: engine source tools + build tools + return_to_project_mode.
	static Array get_tools_for_mode(AIContextMode p_mode);

	// Returns a minimal read-only tool set suitable for consultation/design
	// discussion queries where the AI may need to look up project files but
	// should not modify them. Saves ~3,000 tokens vs. the full mode set.
	static Array get_readonly_tools();

	// Heuristic check: does the user message look like a consultation/design
	// discussion rather than an implementation request?
	static bool is_consultation_message(const String &p_message);
};
