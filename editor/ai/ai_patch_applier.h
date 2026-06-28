/*  ai_patch_applier.h                                                     */
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

// Forward declaration
struct AIRepairTask;

// Applies AI-generated code patches to the local working tree.
//
// Two modes:
//   "full"  : overwrite the entire file with new content.
//   "diff"  : parse a unified diff and apply line-level changes.
//
// Safety guarantees:
//   - dry_run mode only reports what WOULD change, never writes.
//   - Before writing, the original file is backed up to
//     .jundot/ai_patch_backups/{task_id}/.
//   - Unified diffs are validated against the target file line count
//     before any write happens, and rejected on mismatch.
class AIPatchApplier {
public:
	struct PatchResult {
		bool valid = true;
		String error;                      // empty on success
		Vector<String> backup_paths;       // backup file paths created
		Vector<String> dirty_files;        // files that were modified
	};

	// Apply an AI repair task's patch to the working tree.
	// Routes to apply_unified_diff or apply_full_replace based on task.patch_type.
	static PatchResult apply_patch(const AIRepairTask &p_task, bool p_dry_run = false);

	// Parse and apply a unified-diff string against the working directory.
	// Supports standard diff --git header, @@ hunk ranges, + / - / context lines.
	static PatchResult apply_unified_diff(const String &p_diff_text, const String &p_working_dir, bool p_dry_run = false);

	// Overwrite target file(s) with new content.
	static PatchResult apply_full_replace(const Vector<String> &p_file_paths, const Vector<String> &p_new_contents, bool p_dry_run = false);

	// Restore files from backup dir.
	static Error rollback(const Vector<String> &p_backup_paths);

	// Threshold in lines: files shorter than this use "full" mode; longer use "diff".
	static constexpr int FULL_REPLACE_THRESHOLD = 200;
};
