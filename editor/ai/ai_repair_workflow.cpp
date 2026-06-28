/*  ai_repair_workflow.cpp                                                 */
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

#include "ai_repair_workflow.h"

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/templates/list.h"
#include "editor/file_system/editor_paths.h"
#include "core/variant/dictionary.h"

static String _repair_tasks_path() {
	return EditorPaths::get_singleton()->get_config_dir().path_join("ai_repair_tasks.json");
}

static String _now_iso() {
	return itos(static_cast<int64_t>(OS::get_singleton()->get_unix_time()));
}

String AIRepairWorkflow::_make_task_id() {
	return "repair-" + _now_iso();
}

static Dictionary _task_to_dict(const AIRepairTask &p_task) {
	Dictionary d;
	d["id"] = p_task.id;
	d["issue_type"] = p_task.issue_type;
	d["title"] = p_task.title;
	d["reproduction"] = p_task.reproduction;
	d["root_cause"] = p_task.root_cause;
	d["patch_summary"] = p_task.patch_summary;
	d["test_command"] = p_task.test_command;
	d["risk"] = p_task.risk;
	d["state"] = p_task.state;
	d["created_at"] = p_task.created_at;
	d["applied_at"] = p_task.applied_at;
	d["tests_completed_at"] = p_task.tests_completed_at;
	d["build_completed_at"] = p_task.build_completed_at;
	d["evaluated_at"] = p_task.evaluated_at;
	d["published_at"] = p_task.published_at;
	d["last_error"] = p_task.last_error;

	Array files;
	for (int i = 0; i < p_task.candidate_files.size(); i++) {
		files.push_back(p_task.candidate_files[i]);
	}
	d["candidate_files"] = files;
	return d;
}

static AIRepairTask _task_from_dict(const Dictionary &p_dict) {
	AIRepairTask task;
	task.id = p_dict.get("id", String());
	task.issue_type = p_dict.get("issue_type", String());
	task.title = p_dict.get("title", String());
	task.reproduction = p_dict.get("reproduction", String());
	task.root_cause = p_dict.get("root_cause", String());
	task.patch_summary = p_dict.get("patch_summary", String());
	task.test_command = p_dict.get("test_command", String());
	task.risk = p_dict.get("risk", String());
	task.state = static_cast<AIRepairTask::State>(int(p_dict.get("state", 0)));
	task.created_at = p_dict.get("created_at", String());
	task.applied_at = p_dict.get("applied_at", String());
	task.tests_completed_at = p_dict.get("tests_completed_at", String());
	task.build_completed_at = p_dict.get("build_completed_at", String());
	task.evaluated_at = p_dict.get("evaluated_at", String());
	task.published_at = p_dict.get("published_at", String());
	task.last_error = p_dict.get("last_error", String());

	const Variant files_var = p_dict.get("candidate_files", Array());
	if (files_var.get_type() == Variant::ARRAY) {
		Array files = files_var;
		for (int i = 0; i < files.size(); i++) {
			task.candidate_files.push_back(String(files[i]));
		}
	}
	return task;
}

Error AIRepairWorkflow::load(Vector<AIRepairTask> &r_tasks) {
	r_tasks.clear();

	const String path = _repair_tasks_path();
	if (!FileAccess::exists(path)) {
		return OK;
	}

	Error err = OK;
	String raw = FileAccess::get_file_as_string(path, &err);
	if (err != OK) {
		return err;
	}

	JSON json;
	const Error parse_err = json.parse(raw);
	if (parse_err != OK) {
		return parse_err;
	}

	const Variant data = json.get_data();
	if (data.get_type() != Variant::ARRAY) {
		return ERR_INVALID_DATA;
	}

	Array arr = data;
	for (int i = 0; i < arr.size(); i++) {
		if (arr[i].get_type() == Variant::DICTIONARY) {
			r_tasks.push_back(_task_from_dict(arr[i]));
		}
	}
	return OK;
}

Error AIRepairWorkflow::save(const Vector<AIRepairTask> &p_tasks) {
	Array arr;
	for (int i = 0; i < p_tasks.size(); i++) {
		arr.push_back(_task_to_dict(p_tasks[i]));
	}

	JSON json;
	const String raw = json.stringify(arr);
	if (raw.is_empty()) {
		return ERR_INVALID_DATA;
	}

	Ref<FileAccess> file = FileAccess::open(_repair_tasks_path(), FileAccess::WRITE);
	if (file.is_null()) {
		return ERR_FILE_CANT_WRITE;
	}
	file->store_string(raw);
	return OK;
}

Error AIRepairWorkflow::append(const AIRepairTask &p_task) {
	Vector<AIRepairTask> tasks;
	load(tasks);
	tasks.push_back(p_task);
	return save(tasks);
}

Error AIRepairWorkflow::update_task(const String &p_id, AIRepairTask::State p_state, const String &p_error) {
	Vector<AIRepairTask> tasks;
	load(tasks);

	for (int i = 0; i < tasks.size(); i++) {
		if (tasks[i].id == p_id) {
			tasks.write[i].state = p_state;
			tasks.write[i].last_error = p_error;

			String now = _now_iso();
			switch (p_state) {
				case AIRepairTask::STATE_APPLIED:
					tasks.write[i].applied_at = now;
					break;
				case AIRepairTask::STATE_TESTS_PASSED:
				case AIRepairTask::STATE_TESTS_FAILED:
					tasks.write[i].tests_completed_at = now;
					break;
				case AIRepairTask::STATE_BUILD_SUCCEEDED:
				case AIRepairTask::STATE_BUILD_FAILED:
					tasks.write[i].build_completed_at = now;
					break;
				case AIRepairTask::STATE_EVALUATED:
					tasks.write[i].evaluated_at = now;
					break;
				case AIRepairTask::STATE_PUBLISHED:
					tasks.write[i].published_at = now;
					break;
				default:
					break;
			}
			return save(tasks);
		}
	}
	return ERR_DOES_NOT_EXIST;
}

// ---- Dirty worktree protection ----

Vector<String> AIRepairWorkflow::check_dirty_worktree(const Vector<String> &p_candidate_files) {
	Vector<String> conflicting;

	String output;
	int exit_code = 0;
	List<String> git_args;
	git_args.push_back("status");
	git_args.push_back("--porcelain");
	git_args.push_back("-u");
	Error err = OS::get_singleton()->execute("git", git_args, &output, nullptr, &exit_code);
	if (err != OK || exit_code != 0) {
		// Not a git repo or git unavailable → skip protection
		return conflicting;
	}

	Vector<String> lines = output.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		String line = lines[i].strip_edges();
		if (line.is_empty()) {
			continue;
		}

		// git status --porcelain format: "XY filename" where X=index status, Y=worktree status
		// Skip the first 3 chars (status + space) to get the filename
		String file_path = line.substr(3).strip_edges();
		if (file_path.is_empty()) {
			continue;
		}

		// Check if this dirty file is in the candidate list
		bool in_candidate = false;
		for (int j = 0; j < p_candidate_files.size(); j++) {
			if (file_path.find(p_candidate_files[j]) != -1 || p_candidate_files[j].ends_with(file_path)) {
				in_candidate = true;
				break;
			}
		}

		if (!in_candidate) {
			conflicting.push_back(file_path);
		}
	}

	return conflicting;
}

// ---- Default test command suggestion ----

static bool _is_cpp_file(const String &p_path) {
	return p_path.ends_with(".cpp") || p_path.ends_with(".h") || p_path.ends_with(".hpp") || p_path.ends_with(".c");
}

static bool _is_csharp_file(const String &p_path) {
	return p_path.ends_with(".cs");
}

static bool _is_python_file(const String &p_path) {
	return p_path.ends_with(".py");
}

String AIRepairWorkflow::suggest_default_test(const Vector<String> &p_files) {
	bool has_cpp = false;
	bool has_cs = false;
	bool has_py = false;

	for (int i = 0; i < p_files.size(); i++) {
		if (_is_cpp_file(p_files[i])) {
			has_cpp = true;
		}
		if (_is_csharp_file(p_files[i])) {
			has_cs = true;
		}
		if (_is_python_file(p_files[i])) {
			has_py = true;
		}
	}

	// Priority: C++ compilation check → C# dotnet test → Python pytest
	if (has_cpp) {
		return "scons platform=windows target=editor dev_build=yes -j$(nproc)";
	}
	if (has_cs) {
		return "dotnet test tools/PackageBuilder/PackageBuilder.csproj -c Release";
	}
	if (has_py) {
		return "python -m pytest";
	}

	return "";
}

// ---- Pre-patch snapshot ----

String AIRepairWorkflow::record_pre_patch_snapshot(const Vector<String> &p_files) {
	String result;

	for (int i = 0; i < p_files.size(); i++) {
		String output;
		int exit_code = 0;
		List<String> diff_args;
		diff_args.push_back("diff");
		diff_args.push_back(p_files[i]);
		Error err = OS::get_singleton()->execute("git", diff_args, &output, nullptr, &exit_code);

		if (err == OK && !output.is_empty()) {
			result += "=== " + p_files[i] + " ===\n";
			result += output;
			result += "\n\n";
		} else if (err == OK && output.is_empty()) {
			// File exists in worktree but is clean → record its content as snapshot
			// This is a "before" snapshot; we use git show HEAD if possible
			String head_output;
			int head_exit = 0;
			List<String> show_args;
			show_args.push_back("show");
			show_args.push_back("HEAD:" + p_files[i]);
			Error head_err = OS::get_singleton()->execute("git", show_args, &head_output, nullptr, &head_exit);
			if (head_err == OK && head_exit == 0) {
				result += "=== " + p_files[i] + " (HEAD) ===\n";
				result += head_output;
				result += "\n\n";
			}
		}
	}

	return result;
}

// ---- Test runner ----

Error AIRepairWorkflow::run_repair_tests(const String &p_test_command, String &r_output, int &r_exit_code) {
	r_output.clear();
	r_exit_code = -1;

	if (p_test_command.is_empty()) {
		r_output = "No test command specified.";
		return ERR_INVALID_PARAMETER;
	}

	// Split command into executable + args
	List<String> cmd_args;
	Vector<String> parts = p_test_command.split(" ", false);

	if (parts.is_empty()) {
		return ERR_INVALID_PARAMETER;
	}

	String executable = parts[0];
	for (int i = 1; i < parts.size(); i++) {
		if (!parts[i].is_empty()) {
			cmd_args.push_back(parts[i]);
		}
	}

	Error err = OS::get_singleton()->execute(executable, cmd_args, &r_output, nullptr, &r_exit_code);
	return err;
}
