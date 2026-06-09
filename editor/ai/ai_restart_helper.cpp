/*  ai_restart_helper.cpp                                                   */
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

#include "ai_restart_helper.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/variant/variant.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_paths.h"

// ---- Public API ----

String AIRestartHelper::_state_path() {
	return EditorPaths::get_singleton()->get_project_settings_dir()
		.path_join("ai_restore_state.json");
}

Error AIRestartHelper::save_state() {
	// 1. Force editor layout save so session scenes are written to
	//    editor_layout.cfg. This is what restore_scenes_on_load reads.
	EditorNode::get_singleton()->save_editor_layout_delayed();

	// 2. Save all unsaved scene changes.
	EditorNode::get_singleton()->save_all_scenes();

	// 3. Write a sentinel state file recording currently open scenes.
	//    This serves as a safety net if editor_layout.cfg is stale.
	RestoreState state;
	for (int i = 0; i < EditorNode::get_singleton()->get_editor_data().get_edited_scene_count(); i++) {
		String path = EditorNode::get_singleton()->get_editor_data().get_scene_path(i);
		if (!path.is_empty()) {
			state.open_scene_paths.push_back(path);
		}
	}

	// 4. Also save open script paths via the edit_resource tracking.
	//    Script paths are cached in script_editor_cache (EditorSettings),
	//    which is auto-restored when scripts are reopened.

	Dictionary root;
	{
		Array scenes;
		for (const String &s : state.open_scene_paths) {
			scenes.push_back(s);
		}
		root["open_scenes"] = scenes;
		root["saved_at"] = itos(static_cast<int64_t>(OS::get_singleton()->get_unix_time()));
	}

	JSON json;
	String raw = json.stringify(root);

	Ref<FileAccess> f = FileAccess::open(_state_path(), FileAccess::WRITE);
	if (f.is_null()) {
		return ERR_FILE_CANT_WRITE;
	}
	f->store_string(raw);
	return OK;
}

Error AIRestartHelper::restore_state() {
	// Godot's built-in restore_scenes_on_load already handles scene
	// restoration from editor_layout.cfg. This method is a safety net
	// that re-opens any scenes missing from the built-in restore.

	String path = _state_path();
	if (!FileAccess::exists(path)) {
		return OK;
	}

	Error read_err = OK;
	String raw = FileAccess::get_file_as_string(path, &read_err);
	if (read_err != OK) {
		cleanup_state_file();
		return read_err;
	}

	JSON json;
	Error parse_err = json.parse(raw);
	if (parse_err != OK) {
		cleanup_state_file();
		return parse_err;
	}

	Dictionary root = json.get_data();
	if (root.has("open_scenes")) {
		Array scenes = root["open_scenes"];
		EditorNode *en = EditorNode::get_singleton();
		for (int i = 0; i < scenes.size(); i++) {
			String scene_path = scenes[i];
			if (!FileAccess::exists(scene_path)) {
				continue;
			}
			bool already_open = false;
			for (int j = 0; j < en->get_editor_data().get_edited_scene_count(); j++) {
				if (en->get_editor_data().get_scene_path(j) == scene_path) {
					already_open = true;
					break;
				}
			}
			if (!already_open) {
				en->load_scene(scene_path);
			}
		}
	}

	cleanup_state_file();
	return OK;
}

void AIRestartHelper::cleanup_state_file() {
	String path = _state_path();
	if (FileAccess::exists(path)) {
		Ref<DirAccess> da = DirAccess::create_for_path(path);
		if (da.is_valid()) {
			da->remove(path);
		}
	}
}
