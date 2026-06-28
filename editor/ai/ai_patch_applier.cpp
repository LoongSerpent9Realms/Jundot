/*  ai_patch_applier.cpp                                                   */
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

#include "ai_patch_applier.h"

#include "ai_repair_workflow.h"

#include "core/error/error_macros.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/string/ustring.h"

// ---- Internal helpers ----

static String _backup_dir_for_task(const String &p_task_id) {
	return OS::get_singleton()->get_executable_path().get_base_dir()
		.path_join(".jundot")
		.path_join("ai_patch_backups")
		.path_join(p_task_id);
}

static String _backup_file(const String &p_task_id, const String &p_file_path) {
	// Create a flattened backup path: {backup_dir}/path__to__file.cpp
	String flat = p_file_path.replace("/", "__").replace("\\", "__");
	return _backup_dir_for_task(p_task_id).path_join(flat);
}

static void _ensure_backup_dir(const String &p_task_id) {
	String dir = _backup_dir_for_task(p_task_id);
	Ref<DirAccess> da = DirAccess::create_for_path(dir);
	if (da.is_valid()) {
		da->make_dir_recursive(dir);
	}
}

static Error _copy_file(const String &p_src, const String &p_dst) {
	Error err = OK;
	String content = FileAccess::get_file_as_string(p_src, &err);
	if (err != OK) {
		return err;
	}

	Ref<FileAccess> dst = FileAccess::open(p_dst, FileAccess::WRITE);
	if (dst.is_null()) {
		return ERR_FILE_CANT_WRITE;
	}
	dst->store_string(content);
	return OK;
}

// Parse a unified diff hunk header.
// Format: @@ -src_start,src_count +dst_start,dst_count @@
// Returns true on success, fills out the hunk parameters.
struct HunkInfo {
	int src_start = -1;   // 1-indexed
	int src_count = 0;
	int dst_start = -1;   // 1-indexed
	int dst_count = 0;
};

static bool _parse_hunk_header(const String &p_line, HunkInfo &r_hunk) {
	// Expected: @@ -1,5 +1,6 @@
	int at2 = p_line.find("@@", 2);
	if (at2 < 0) {
		return false;
	}
	String body = p_line.substr(2, at2 - 2).strip_edges();

	int split = body.find(" ");
	if (split < 0) {
		return false;
	}
	String src_part = body.substr(0, split);  // -start,count
	String dst_part = body.substr(split + 1);  // +start,count

	// Parse src.
	int src_comma = src_part.find(",");
	if (src_comma > 0) {
		r_hunk.src_start = src_part.substr(1, src_comma - 1).to_int();
		r_hunk.src_count = src_part.substr(src_comma + 1).to_int();
	} else {
		r_hunk.src_start = src_part.substr(1).to_int();
		r_hunk.src_count = 1;
	}

	// Parse dst.
	int dst_comma = dst_part.find(",");
	if (dst_comma > 0) {
		r_hunk.dst_start = dst_part.substr(1, dst_comma - 1).to_int();
		r_hunk.dst_count = dst_part.substr(dst_comma + 1).to_int();
	} else {
		r_hunk.dst_start = dst_part.substr(1).to_int();
		r_hunk.dst_count = 1;
	}

	return r_hunk.src_start > 0 && r_hunk.dst_start > 0;
}

// Parse the file path from a "--- a/path" or "+++ b/path" line.
static String _parse_file_from_header(const String &p_line) {
	String stripped = p_line.trim_prefix("--- ").trim_prefix("+++ ");
	String path = stripped.trim_prefix("a/").trim_prefix("b/").trim_prefix("i/").trim_prefix("w/").strip_edges();
	// Handle "filename\tdate" trailing.
	int tab = path.find("\t");
	if (tab >= 0) {
		path = path.substr(0, tab);
	}
	return path;
}

// Apply unified diff text in-memory and write back.
static AIPatchApplier::PatchResult _apply_one_diff(const String &p_diff_text, const String &p_working_dir, bool p_dry_run) {
	AIPatchApplier::PatchResult result;

	Vector<String> lines = p_diff_text.split("\n");

	// Collect files and their hunks.
	// Simple single-file diff format; multi-file diffs split by "diff --git" line.
	struct FileHunk {
		String file_path;
		int hunk_src_start;
		int hunk_src_count;
		int hunk_dst_start;
		Vector<String> hunk_lines; // +, -, context lines relative to this hunk
	};

	String current_file;
	Vector<FileHunk> hunks;
	bool in_hunk = false;
	FileHunk current_hunk;

	for (int i = 0; i < lines.size(); i++) {
		String line = lines[i];

		// Detect new diff --git
		if (line.begins_with("diff --git ")) {
			continue;
		}

		// Detect --- a/file
		if (line.begins_with("--- ")) {
			// This is the source file. The destination file is in the next +++ line.
			continue;
		}

		// Detect +++ b/file
		if (line.begins_with("+++ ")) {
			current_file = _parse_file_from_header(line);
			continue;
		}

		// Detect hunk header
		if (line.begins_with("@@")) {
			if (in_hunk && current_hunk.hunk_lines.size() > 0) {
				hunks.push_back(current_hunk);
			}
			in_hunk = true;
			current_hunk = FileHunk();
			current_hunk.file_path = current_file;
			HunkInfo info;
			if (!_parse_hunk_header(line, info)) {
				result.valid = false;
				result.error = "Failed to parse hunk header: " + line;
				return result;
			}
			current_hunk.hunk_src_start = info.src_start;
			current_hunk.hunk_src_count = info.src_count;
			current_hunk.hunk_dst_start = info.dst_start;
			continue;
		}

		// Collect hunk body lines
		if (in_hunk) {
			current_hunk.hunk_lines.push_back(line);
		}
	}

	// Push last hunk.
	if (in_hunk && current_hunk.hunk_lines.size() > 0) {
		hunks.push_back(current_hunk);
	}

	if (hunks.is_empty()) {
		result.valid = false;
		result.error = "No hunks found in diff text.";
		return result;
	}

	// Group hunks by file.
	HashMap<String, Vector<FileHunk>> file_hunks;
	for (const FileHunk &h : hunks) {
		if (!file_hunks.has(h.file_path)) {
			file_hunks[h.file_path] = Vector<FileHunk>();
		}
		file_hunks[h.file_path].push_back(h);
	}

	// Apply each file's hunks.
	for (const KeyValue<String, Vector<FileHunk>> &kv : file_hunks) {
		String file_path = p_working_dir.path_join(kv.key);

		if (!FileAccess::exists(file_path)) {
			result.valid = false;
			result.error = "Target file not found: " + file_path;
			return result;
		}

		// Read source file lines.
		String src_content;
		{
			Error read_err = OK;
			src_content = FileAccess::get_file_as_string(file_path, &read_err);
			if (read_err != OK) {
				result.valid = false;
				result.error = "Cannot read target file: " + file_path;
				return result;
			}
		}

		Vector<String> src_lines = src_content.split("\n");

		Vector<String> result_lines;
		int src_idx = 0;  // 0-indexed line counter in source file

		// Sort hunks by src_start for sequential processing.
		struct HunkSorter {
			bool operator()(const FileHunk &a, const FileHunk &b) const {
				return a.hunk_src_start < b.hunk_src_start;
			}
		};

		Vector<FileHunk> sorted_hunks = kv.value;
		sorted_hunks.sort_custom<HunkSorter>();

		for (const FileHunk &hunk : sorted_hunks) {
			// hunk_src_start is 1-indexed; src_idx is 0-indexed.
			int hunk_start = hunk.hunk_src_start - 1;

			if (hunk_start > (int)src_lines.size()) {
				result.valid = false;
				result.error = vformat("Hunk offset %d exceeds file size %d for %s", hunk.hunk_src_start, src_lines.size(), kv.key);
				return result;
			}

			// Copy lines before this hunk.
			while (src_idx < hunk_start) {
				result_lines.push_back(src_lines[src_idx]);
				src_idx++;
			}

			// Apply the hunk: walk hunk lines and src lines in parallel.
			int hunk_line_idx = 0;
			int expected_src = hunk.hunk_src_count;

			while (hunk_line_idx < hunk.hunk_lines.size()) {
				String hl = hunk.hunk_lines[hunk_line_idx];

				if (hl.begins_with("+")) {
					// Add new line.
					result_lines.push_back(hl.substr(1));
					hunk_line_idx++;
				} else if (hl.begins_with("-")) {
					// Remove line (skip in source).
					src_idx++;
					expected_src--;
					hunk_line_idx++;
				} else {
					// Context line (starts with " " or is empty).
					String ctx = hl;
					if (!ctx.is_empty() && ctx[0] == ' ') {
						ctx = ctx.substr(1);
					}
					// Advance source and output the context line.
					if (src_idx < (int)src_lines.size()) {
						result_lines.push_back(src_lines[src_idx]);
						src_idx++;
					}
					expected_src--;
					hunk_line_idx++;
				}
			}

			// Skip remaining source lines for this hunk.
			while (expected_src > 0 && src_idx < (int)src_lines.size()) {
				src_idx++;
				expected_src--;
			}
		}

		// Copy remaining lines after all hunks.
		while (src_idx < (int)src_lines.size()) {
			result_lines.push_back(src_lines[src_idx]);
			src_idx++;
		}

		// Join and write.
		String new_content;
		for (int i = 0; i < result_lines.size(); i++) {
			if (i > 0) {
				new_content += "\n";
			}
			new_content += result_lines[i];
		}

		if (p_dry_run) {
			result.dirty_files.push_back(file_path);
			continue;
		}

		Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::WRITE);
		if (f.is_null()) {
			result.valid = false;
			result.error = "Cannot write to file: " + file_path;
			return result;
		}
		f->store_string(new_content);
		result.dirty_files.push_back(file_path);
	}

	return result;
}

// ---- Public API ----

AIPatchApplier::PatchResult AIPatchApplier::apply_patch(const AIRepairTask &p_task, bool p_dry_run) {
	if (p_task.patch_type == "diff") {
		String repo_root = OS::get_singleton()->get_executable_path().get_base_dir();
		return apply_unified_diff(p_task.patch_code, repo_root, p_dry_run);
	}

	// "full" or unrecognized: treat as full file replacements.
	Vector<String> paths = p_task.candidate_files;
	Vector<String> contents;
	for (int i = 0; i < paths.size(); i++) {
		contents.push_back(p_task.patch_code);
	}

	return apply_full_replace(paths, contents, p_dry_run);
}

AIPatchApplier::PatchResult AIPatchApplier::apply_unified_diff(const String &p_diff_text, const String &p_working_dir, bool p_dry_run) {
	if (p_diff_text.is_empty()) {
		PatchResult result;
		result.valid = false;
		result.error = "Empty diff text.";
		return result;
	}

	return _apply_one_diff(p_diff_text, p_working_dir, p_dry_run);
}

AIPatchApplier::PatchResult AIPatchApplier::apply_full_replace(const Vector<String> &p_file_paths, const Vector<String> &p_new_contents, bool p_dry_run) {
	PatchResult result;

	if (p_file_paths.is_empty()) {
		result.valid = false;
		result.error = "Empty file path list.";
		return result;
	}

	for (int i = 0; i < p_file_paths.size(); i++) {
		String path = p_file_paths[i];

		if (!FileAccess::exists(path)) {
			result.valid = false;
			result.error = "File not found: " + path;
			return result;
		}

		if (p_dry_run) {
			result.dirty_files.push_back(path);
			continue;
		}

		// Backup original.
		String backup = _backup_file("_full_replace", path);
		_ensure_backup_dir("_full_replace");
		Error copy_err = _copy_file(path, backup);
		if (copy_err == OK) {
			result.backup_paths.push_back(backup);
		}

		// Write new content.
		Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
		if (f.is_null()) {
			result.valid = false;
			result.error = "Cannot write to file: " + path;
			return result;
		}

		String content = (i < p_new_contents.size()) ? p_new_contents[i] : p_new_contents[0];
		f->store_string(content);
		result.dirty_files.push_back(path);
	}

	return result;
}

Error AIPatchApplier::rollback(const Vector<String> &p_backup_paths) {
	for (const String &backup : p_backup_paths) {
		if (!FileAccess::exists(backup)) {
			WARN_PRINT("AIPatchApplier::rollback: backup not found: " + backup);
			continue;
		}

		// Filename encodes original path (flattened).
		String flat = backup.get_file();
		String original = flat.replace("__", "/");

		Error copy_err = _copy_file(backup, original);
		if (copy_err != OK) {
			ERR_FAIL_V_MSG(copy_err, "AIPatchApplier::rollback: failed to restore " + original);
		}
	}

	return OK;
}
