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

static Dictionary _bool_property(const String &p_description) {
	Dictionary prop;
	prop["type"] = "boolean";
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

	// 7. check_ui_layout
	{
		Dictionary props;
		props["paths"] = _array_str_property("Scene file path(s) relative to the project root to inspect, e.g. 'scenes/main_menu.tscn' or 'res://scenes/hud.tscn'.");
		Array required;
		required.push_back("paths");
		Dictionary fn = _make_fn(
				AIToolNames::CHECK_UI_LAYOUT,
				"PROJECT mode only. Statically inspect Godot .tscn UI scenes for likely overlapping sibling Control nodes, z_index/order mistakes, non-interactive upper Controls that may block Button/input clicks, and modal settings/dialog/menu panels that pass clicks through because mouse_filter is Pass or Ignore. Use after creating or editing UI scenes. Container-managed children are treated as managed layout and are not flagged as fixed-rectangle overlaps.",
				props, required);
		tools.push_back(_tool(AIToolNames::CHECK_UI_LAYOUT, "", fn));
	}

	// 8. build_project
	{
		Dictionary props;
		props["path"] = _str_property("Scene file path to create relative to the project root, e.g. 'scenes/levels/test_arena.tscn' or 'res://scenes/levels/test_arena.tscn'.");
		props["root_name"] = _str_property("Optional root Node3D name. Defaults to World.");
		props["include_camera"] = _bool_property("If true or omitted, add a Camera3D looking at the origin.");
		props["include_lighting"] = _bool_property("If true or omitted, add a DirectionalLight3D.");
		props["include_floor"] = _bool_property("If true or omitted, add a simple floor MeshInstance3D.");
		Array required;
		required.push_back("path");
		Dictionary fn = _make_fn(
				AIToolNames::CREATE_3D_SCENE,
				"PROJECT mode only. Create a new Godot .tscn 3D scene with a Node3D root and optional starter camera, directional light, and floor. Use this for quickly scaffolding playable 3D prototypes before adding objects and scripts.",
				props, required);
		tools.push_back(_tool(AIToolNames::CREATE_3D_SCENE, "", fn));
	}

	// 9. add_3d_object
	{
		Dictionary props;
		props["scene_path"] = _str_property("Existing .tscn scene file relative to the project root.");
		props["name"] = _str_property("Name for the new MeshInstance3D node.");
		props["mesh_type"] = _str_property("Primitive mesh type: box, sphere, cylinder, capsule, plane, or quad. Defaults to box.");
		props["parent"] = _str_property("Optional parent node path inside the scene. Defaults to root '.'.");
		props["position"] = _str_property("Optional Vector3 position, e.g. '0,1,0' or 'Vector3(0, 1, 0)'. Defaults to 0,0,0.");
		props["rotation_degrees"] = _str_property("Optional Euler rotation in degrees, e.g. '0,45,0'. Defaults to 0,0,0.");
		props["scale"] = _str_property("Optional Vector3 scale, e.g. '1,1,1'. Defaults to 1,1,1.");
		props["size"] = _str_property("Optional primitive size. For box use Vector3, for sphere/cylinder/capsule use one number as radius, for plane/quad use Vector2 or two numbers.");
		props["color"] = _str_property("Optional material color as '#RRGGBB', 'r,g,b', or 'Color(r,g,b,a)'. Defaults to neutral gray.");
		Array required;
		required.push_back("scene_path");
		required.push_back("name");
		Dictionary fn = _make_fn(
				AIToolNames::ADD_3D_OBJECT,
				"PROJECT mode only. Add a primitive MeshInstance3D with a StandardMaterial3D to an existing .tscn scene. Use for simple generated 3D props, blockers, floors, pickups, and prototype geometry.",
				props, required);
		tools.push_back(_tool(AIToolNames::ADD_3D_OBJECT, "", fn));
	}

	// 10. add_3d_light
	{
		Dictionary props;
		props["scene_path"] = _str_property("Existing .tscn scene file relative to the project root.");
		props["name"] = _str_property("Name for the new light node.");
		props["light_type"] = _str_property("Light type: directional, omni, or spot. Defaults to directional.");
		props["parent"] = _str_property("Optional parent node path inside the scene. Defaults to root '.'.");
		props["position"] = _str_property("Optional Vector3 position, e.g. '0,4,2'. Mostly used for omni and spot lights.");
		props["rotation_degrees"] = _str_property("Optional Euler rotation in degrees, e.g. '-45,30,0'. Useful for directional and spot lights.");
		props["color"] = _str_property("Optional light color as '#RRGGBB', 'r,g,b', or 'Color(r,g,b,a)'. Defaults to white.");
		props["energy"] = _number_property("Optional light_energy value. Defaults to 1.5.");
		props["range"] = _number_property("Optional omni_range or spot_range. Defaults to 8 for omni and 12 for spot.");
		props["spot_angle"] = _number_property("Optional spot_angle in degrees for SpotLight3D. Defaults to 45.");
		props["shadows"] = _bool_property("If true or omitted, enable shadows.");
		Array required;
		required.push_back("scene_path");
		required.push_back("name");
		Dictionary fn = _make_fn(
				AIToolNames::ADD_3D_LIGHT,
				"PROJECT mode only. Add a DirectionalLight3D, OmniLight3D, or SpotLight3D to an existing .tscn scene with color, energy, transform, range, angle, and shadow settings.",
				props, required);
		tools.push_back(_tool(AIToolNames::ADD_3D_LIGHT, "", fn));
	}

	// 11. check_3d_scene
	{
		Dictionary props;
		props["paths"] = _array_str_property("3D .tscn scene file path(s) relative to the project root to inspect.");
		Array required;
		required.push_back("paths");
		Dictionary fn = _make_fn(
				AIToolNames::CHECK_3D_SCENE,
				"PROJECT mode only. Statically inspect .tscn 3D scenes for likely missing basics such as Node3D root, Camera3D, lighting/world environment, visible MeshInstance3D content, and physics/collision coverage. Use after creating or modifying 3D scenes.",
				props, required);
		tools.push_back(_tool(AIToolNames::CHECK_3D_SCENE, "", fn));
	}

	// 12. build_project
	{
		Dictionary props;
		props["project_path"] = _str_property("Optional .csproj or .sln path relative to the open project root, e.g. '22.csproj' or 'src/Game.csproj'. If omitted, the tool auto-detects a single .csproj/.sln in the project root.");
		props["configuration"] = _str_property("Optional build configuration, such as Debug or Release.");
		props["target"] = _str_property("Optional MSBuild target, such as Build, Rebuild, or Clean. Defaults to Build.");
		Dictionary bool_prop;
		bool_prop["type"] = "boolean";
		bool_prop["description"] = "If true, pass --no-restore to dotnet build.";
		props["no_restore"] = bool_prop;
		Dictionary fn = _make_fn(
				AIToolNames::BUILD_PROJECT,
				"PROJECT mode only. Build a C#/.NET project or solution inside the open project root without using shell_command. Use this instead of shell_command for dotnet build, including compiling a specified .csproj. Returns exit code and compiler output for the AI to fix errors.",
				props, Array());
		tools.push_back(_tool(AIToolNames::BUILD_PROJECT, "", fn));
	}

	// 13. build_cpp_hot_module
	{
		Dictionary props;
		props["extension_path"] = _str_property("Path to the project .gdextension file that should be reloaded after a successful build, relative to the open project root or res://.");
		props["program"] = _str_property("Build executable to run inside the project, such as scons, cmake, ninja, python, or dotnet.");
		Dictionary args_prop;
		args_prop["type"] = "array";
		args_prop["items"] = _str_property("One command argument.");
		args_prop["description"] = "Command arguments as an array, not a shell string. Example: ['--build', 'build', '--config', 'Debug'].";
		props["args"] = args_prop;
		props["workdir"] = _str_property("Optional working directory relative to the open project root. Defaults to '.'.");
		Array required;
		required.push_back("extension_path");
		required.push_back("program");
		Dictionary fn = _make_fn(
				AIToolNames::BUILD_CPP_HOT_MODULE,
				"PROJECT mode only. Build a project-local C++ hot module using an explicit program + args command, then hot-reload its reloadable .gdextension with reload_cpp_hot_module. Workdir must stay inside the open project root.",
				props, required);
		tools.push_back(_tool(AIToolNames::BUILD_CPP_HOT_MODULE, "", fn));
	}

	// 14. reload_cpp_hot_module
	{
		Dictionary props;
		props["extension_path"] = _str_property("Path to a project .gdextension file, relative to the open project root or res://, for example 'native/my_module.gdextension'. The extension must be marked reloadable=true to support hot reload.");
		Array required;
		required.push_back("extension_path");
		Dictionary fn = _make_fn(
				AIToolNames::RELOAD_CPP_HOT_MODULE,
				"PROJECT mode only. Load or hot-reload a C++ module implemented as a reloadable GDExtension. Use after the module's native library has been rebuilt in place. Returns whether the extension was loaded, reloaded, or needs an editor restart.",
				props, required);
		tools.push_back(_tool(AIToolNames::RELOAD_CPP_HOT_MODULE, "", fn));
	}

	// 15. package_project
	{
		Dictionary props;
		props["target"] = _str_property("Optional PackageBuilder target: editor, editor.dev, template_release, or template_debug. Defaults to editor. Use template targets when only export templates are needed.");
		props["platform"] = _str_property("Optional target platform, such as windows, android, linuxbsd, macos, or web. Defaults to windows. PackageBuilder will reject unsupported host/platform combinations early.");
		props["arch"] = _str_property("Optional architecture, such as x86_64, x86_32, or arm64. Defaults to x86_64.");
		Dictionary bool_prop;
		bool_prop["type"] = "boolean";
		bool_prop["description"] = "If true, package existing binaries from bin/ without rebuilding the engine. Defaults to false.";
		props["skip_build"] = bool_prop;
		Dictionary mono_prop;
		mono_prop["type"] = "boolean";
		mono_prop["description"] = "If true, build/package Mono artifacts. Defaults to false.";
		props["mono"] = mono_prop;
		Dictionary auto_version_prop;
		auto_version_prop["type"] = "boolean";
		auto_version_prop["description"] = "If true, auto-increment version.py before a real build. Defaults to true. Set false for quick package-only or diagnostic runs.";
		props["auto_update_version"] = auto_version_prop;
		Dictionary manifest_prop;
		manifest_prop["type"] = "boolean";
		manifest_prop["description"] = "If true, generate update-manifest.json after packaging. Defaults to true.";
		props["generate_update_manifest"] = manifest_prop;
		Dictionary jobs_prop;
		jobs_prop["type"] = "integer";
		jobs_prop["description"] = "Optional SCons job count. 0 or omitted lets PackageBuilder choose.";
		props["jobs"] = jobs_prop;
		props["extra_scons_args"] = _str_property("Optional extra key=value SCons arguments, for example 'd3d12=no accesskit=no'. Use sparingly and prefer defaults.");
		props["note"] = _str_property("Optional short reason for starting this package run, such as the approved plan or validation summary.");
		Dictionary fn = _make_fn(
				AIToolNames::PACKAGE_PROJECT,
				"PROJECT mode only. Start an unattended JunDot package build through PackageBuilder using the AI build request file. For fast repackaging, set skip_build=true to reuse existing bin/ products. Use target/platform/arch only when a smaller or different package is needed. Returns immediately; call check_package_status until the package succeeds or fails.",
				props, Array());
		tools.push_back(_tool(AIToolNames::PACKAGE_PROJECT, "", fn));
	}

	// 14. check_package_status
	{
		Dictionary props;
		Dictionary fn = _make_fn(
				AIToolNames::CHECK_PACKAGE_STATUS,
				"PROJECT mode only. Check the unattended PackageBuilder run started by package_project. Returns running, success, or failed plus package zip, manifest, and build log paths when available. If running, call it again later before telling the user packaging is complete.",
				props, Array());
		tools.push_back(_tool(AIToolNames::CHECK_PACKAGE_STATUS, "", fn));
	}

	// 15. play_scene
	{
		Dictionary props;
		props["args"] = _str_property("Optional command-line arguments for the packaged executable smoke test. Defaults to --version, which should start and exit quickly.");
		Dictionary fn = _make_fn(
				AIToolNames::TEST_PACKAGE,
				"PROJECT mode only. Smoke-test the latest packaged build before handing it to the user. Finds the latest PackageBuilder record, runs the packaged executable with a quick command such as --version, and returns exit code/output. Use after check_package_status reports success.",
				props, Array());
		tools.push_back(_tool(AIToolNames::TEST_PACKAGE, "", fn));
	}

	// 16. play_scene
	{
		Dictionary props;
		props["scene_path"] = _str_property("Scene file path relative to the project root, e.g. 'scenes/main_menu.tscn' or 'res://scenes/main_menu.tscn'. If omitted, the project's main scene is played.");
		Dictionary fn = _make_fn(
				AIToolNames::PLAY_SCENE,
				"PROJECT mode only. Play the project's main scene or a specified .tscn/.scn scene from the open project using the editor run bar. Use before click_ui_position when validating generated UI by actually running it.",
				props, Array());
		tools.push_back(_tool(AIToolNames::PLAY_SCENE, "", fn));
	}

	// 17. click_ui_position
	{
		Dictionary props;
		props["x"] = _number_property("X coordinate in the running game's viewport/window, in pixels from the top-left corner.");
		props["y"] = _number_property("Y coordinate in the running game's viewport/window, in pixels from the top-left corner.");
		props["button"] = _str_property("Mouse button to click: left, right, or middle. Defaults to left.");
		props["wait_ms"] = _number_property("Optional time to wait after sending the click before returning, in milliseconds. Defaults to 250.");
		Array required;
		required.push_back("x");
		required.push_back("y");
		Dictionary fn = _make_fn(
				AIToolNames::CLICK_UI_POSITION,
				"PROJECT mode only. Send a synthetic mouse click to the currently running game viewport at the given pixel coordinate through the debugger channel. Use after play_scene to validate that generated UI buttons can be clicked. This does not move the user's desktop cursor.",
				props, required);
		tools.push_back(_tool(AIToolNames::CLICK_UI_POSITION, "", fn));
	}

	// 18. stop_play_scene
	{
		Dictionary props;
		props["node_path"] = _str_property("Runtime scene-tree path to the Control node to click, for example '/root/Main/Menu/StartButton'. The node must exist in the running scene and be visible.");
		props["button"] = _str_property("Mouse button to click: left, right, or middle. Defaults to left.");
		props["wait_ms"] = _number_property("Optional time to wait for the running game to report the click result, in milliseconds. Defaults to 500.");
		Array required;
		required.push_back("node_path");
		Dictionary fn = _make_fn(
				AIToolNames::CLICK_UI_NODE,
				"PROJECT mode only. Click a visible runtime Control by node path. This asks the running game to resolve the node, compute its global rectangle center, and send a synthetic mouse click there. Use this instead of coordinate clicks when a button path is known.",
				props, required);
		tools.push_back(_tool(AIToolNames::CLICK_UI_NODE, "", fn));
	}

	// 19. assert_node_visible
	{
		Dictionary props;
		props["node_path"] = _str_property("Runtime scene-tree path to the CanvasItem node to check, for example '/root/Main/Menu/StartButton'.");
		props["wait_ms"] = _number_property("Optional time to wait for the running game to report the assertion result, in milliseconds. Defaults to 500.");
		Array required;
		required.push_back("node_path");
		Dictionary fn = _make_fn(
				AIToolNames::ASSERT_NODE_VISIBLE,
				"PROJECT mode only. Assert that a runtime CanvasItem node exists and is visible in the running scene tree. Use after clicks or scene transitions to verify visible UI state.",
				props, required);
		tools.push_back(_tool(AIToolNames::ASSERT_NODE_VISIBLE, "", fn));
	}

	// 20. assert_no_runtime_errors
	{
		Dictionary props;
		Dictionary allow_warnings;
		allow_warnings["type"] = "boolean";
		allow_warnings["description"] = "If true, warnings do not fail the assertion. Runtime errors always fail. Defaults to true.";
		props["allow_warnings"] = allow_warnings;
		Dictionary fn = _make_fn(
				AIToolNames::ASSERT_NO_RUNTIME_ERRORS,
				"PROJECT mode only. Check the active debugger sessions for runtime errors and warnings after playing or clicking a scene. Use after click_ui_position or click_ui_node so AI does not treat a sent click as a passed test when the button callback throws.",
				props, Array());
		tools.push_back(_tool(AIToolNames::ASSERT_NO_RUNTIME_ERRORS, "", fn));
	}

	// 21. stop_play_scene
	{
		Dictionary props;
		props["wait_ms"] = _number_property("Optional time to wait for the game viewport screenshot to arrive, in milliseconds. Defaults to 1000.");
		props["file_name"] = _str_property("Optional PNG file name to save under .JundotAI/runtime_screenshots/. If omitted, a timestamped file name is generated.");
		Dictionary fn = _make_fn(
				AIToolNames::CAPTURE_GAME_SCREENSHOT,
				"PROJECT mode only. Capture the currently running game viewport as a PNG and save it under .JundotAI/runtime_screenshots/. Use after play_scene and UI interactions to inspect visual layout, visible state, and screen composition. The tool returns the saved image path and dimensions.",
				props, Array());
		tools.push_back(_tool(AIToolNames::CAPTURE_GAME_SCREENSHOT, "", fn));
	}

	// 22. stop_play_scene
	{
		Dictionary props;
		props["max_nodes"] = _number_property("Maximum runtime CanvasItem/Control nodes to include. Defaults to 200, capped at 1000.");
		Dictionary include_invisible;
		include_invisible["type"] = "boolean";
		include_invisible["description"] = "If true, include invisible CanvasItem nodes too. Defaults to false.";
		props["include_invisible"] = include_invisible;
		props["wait_ms"] = _number_property("Optional time to wait for the running game to return the snapshot, in milliseconds. Defaults to 1000.");
		Dictionary fn = _make_fn(
				AIToolNames::CAPTURE_RUNTIME_UI_SNAPSHOT,
				"PROJECT mode only. Capture structured runtime UI evidence from the currently running game: CanvasItem/Control hierarchy, node paths, classes, visibility, global rectangles, z_index, mouse_filter, clipping, modulate/self_modulate, and basic ColorRect/Label colors. Use with capture_game_screenshot to diagnose whether UI position, layering, and displayed state are correct.",
				props, Array());
		tools.push_back(_tool(AIToolNames::CAPTURE_RUNTIME_UI_SNAPSHOT, "", fn));
	}

	// 23. stop_play_scene
	{
		Dictionary props;
		Dictionary fn = _make_fn(
				AIToolNames::STOP_PLAY_SCENE,
				"PROJECT mode only. Stop the currently running game scene if one is playing.",
				props, Array());
		tools.push_back(_tool(AIToolNames::STOP_PLAY_SCENE, "", fn));
	}

	// 24. run_build
	{
		Dictionary props;
		props["extra_args"] = _str_property("Optional extra scons arguments, e.g. 'module_mono_enabled=yes'.");
		Dictionary fn = _make_fn(
				AIToolNames::RUN_BUILD,
				"Check the configured Git source checkout for upstream updates, preserve local changes, automatically merge updates with local-first conflict handling, then incrementally build the engine using scons. A packaged editor executable does not contain source code; if no source checkout with SConstruct is available, clone/download the source into the cache directory or set engine_source_root before retrying. By default builds platform=windows target=editor with module_mono_enabled=no so the generated editor can restart without separately built .NET assemblies.",
				props, Array());
		tools.push_back(_tool(AIToolNames::RUN_BUILD, "", fn));
	}

	// 25. read_build_log
	{
		Dictionary props;
		Dictionary fn = _make_fn(
				AIToolNames::READ_BUILD_LOG,
				"Read the most recent build log file from artifacts/logs/ to analyze build errors.",
				props, Array());
		tools.push_back(_tool(AIToolNames::READ_BUILD_LOG, "", fn));
	}

	// 26. fetch_url
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

	// 18. shell_command
	{
		Dictionary props;
		props["command"] = _str_property("Shell command to execute.");
		props["workdir"] = _str_property("Optional working directory. In PROJECT mode this must be the open project root or a directory inside it; use '.' or omit it for the project root. In ENGINE mode this must stay inside the configured engine source root.");
		Array required;
		required.push_back("command");
		Dictionary fn = _make_fn(
				AIToolNames::SHELL_COMMAND,
				"Execute a shell command in the current tool root or an allowed subdirectory. In PROJECT mode, workdir must stay inside the open game project, so the AI can run commands such as dotnet build from a project subfolder. In ENGINE mode, workdir must stay inside the configured source checkout. Returns stdout, stderr, and the exit code. Use with caution.",
				props, required);
		tools.push_back(_tool(AIToolNames::SHELL_COMMAND, "", fn));
	}

	// 19. restart_engine
	{
		Dictionary props;
		Dictionary fn = _make_fn(
				AIToolNames::RESTART_ENGINE,
				"Restart the Jundot editor after a successful build. Saves the current editor state (open scenes, scripts) so they are restored when the editor reopens. Call this after run_build succeeds.",
				props, Array());
		tools.push_back(_tool(AIToolNames::RESTART_ENGINE, "", fn));
	}

	// 20. check_build_status
	{
		Dictionary props;
		Dictionary fn = _make_fn(
				AIToolNames::CHECK_BUILD_STATUS,
				"Check the status of a background build started by run_build. Returns 'running' if the build is still in progress, or the build output and exit code once it completes. Call this after run_build to get build results.",
				props, Array());
		tools.push_back(_tool(AIToolNames::CHECK_BUILD_STATUS, "", fn));
	}

	// 21. upload_code
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

	// 22. develop_ai_verify
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

	// 23. setup_engine_workspace
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

	// 24. request_engine_change
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

	// 25. return_to_project_mode
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

	// 26. batch_tools
	{
		Dictionary op_props;
		op_props["name"] = _str_property("Tool name to execute, e.g. list_files, grep_code, read_files, write_file, check_project_scripts, check_ui_layout, create_3d_scene, add_3d_object, add_3d_light, check_3d_scene, play_scene, click_ui_position, click_ui_node, assert_node_visible, assert_no_runtime_errors, capture_game_screenshot, capture_runtime_ui_snapshot, shell_command.");
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
	project_only.insert(StringName(AIToolNames::CHECK_UI_LAYOUT));
	project_only.insert(StringName(AIToolNames::CREATE_3D_SCENE));
	project_only.insert(StringName(AIToolNames::ADD_3D_OBJECT));
	project_only.insert(StringName(AIToolNames::ADD_3D_LIGHT));
	project_only.insert(StringName(AIToolNames::CHECK_3D_SCENE));
	project_only.insert(StringName(AIToolNames::BUILD_PROJECT));
	project_only.insert(StringName(AIToolNames::BUILD_CPP_HOT_MODULE));
	project_only.insert(StringName(AIToolNames::RELOAD_CPP_HOT_MODULE));
	project_only.insert(StringName(AIToolNames::PACKAGE_PROJECT));
	project_only.insert(StringName(AIToolNames::CHECK_PACKAGE_STATUS));
	project_only.insert(StringName(AIToolNames::TEST_PACKAGE));
	project_only.insert(StringName(AIToolNames::PLAY_SCENE));
	project_only.insert(StringName(AIToolNames::CLICK_UI_POSITION));
	project_only.insert(StringName(AIToolNames::CLICK_UI_NODE));
	project_only.insert(StringName(AIToolNames::ASSERT_NODE_VISIBLE));
	project_only.insert(StringName(AIToolNames::ASSERT_NO_RUNTIME_ERRORS));
	project_only.insert(StringName(AIToolNames::CAPTURE_GAME_SCREENSHOT));
	project_only.insert(StringName(AIToolNames::CAPTURE_RUNTIME_UI_SNAPSHOT));
	project_only.insert(StringName(AIToolNames::STOP_PLAY_SCENE));
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
