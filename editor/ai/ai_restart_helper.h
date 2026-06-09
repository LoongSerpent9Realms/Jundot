/*  ai_restart_helper.h                                                    */
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
#include "core/templates/vector.h"

// Persists editor work state across AI-triggered restarts.
//
// Before EditorNode::restart_editor() is called (e.g. after a new build),
// save_state() writes a JSON blob of currently open scenes and scripts
// to {project_settings}/ai_restore_state.json.
//
// On the next editor startup, restore_state() reads that file, re-opens
// the scenes and scripts, and deletes the state file.
//
// This works _in addition to_ the built-in editor layout restore —
// it does not replace EditorNode::restore_scenes_on_load or
// ScriptEditor's script_editor_cache. It fills the gap where the
// editor process is killed mid-work and needs an explicit "return to
// where I was" signal.
class AIRestartHelper {
public:
	// Write current editor state to disk. Call before restart.
	static Error save_state();

	// Read and apply saved state. Call on startup after editor is ready.
	static Error restore_state();

	// Remove the state file (called after successful restore).
	static void cleanup_state_file();

	// Path to the state JSON file.
	static String _state_path();

	// Internal struct serialized to/from JSON.
	struct RestoreState {
		Vector<String> open_scene_paths;
		Vector<String> open_script_paths;
	};
};
