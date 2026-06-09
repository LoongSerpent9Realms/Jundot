/*  ai_repair_workflow.h                                                   */
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

struct AIRepairTask {
	enum State {
		STATE_PENDING,
		STATE_APPLIED,
		STATE_TESTS_PASSED,
		STATE_TESTS_FAILED,
		STATE_BUILD_TRIGGERED,
		STATE_BUILD_SUCCEEDED,
		STATE_BUILD_FAILED,
		STATE_EVALUATED,
		STATE_PUBLISHED,
	};

	String id;
	String issue_type;
	String title;
	String reproduction;
	String root_cause;
	Vector<String> candidate_files;
	String patch_summary;
	String test_command;
	String risk;
	State state = STATE_PENDING;

	String created_at;
	String applied_at;
	String tests_completed_at;
	String build_completed_at;
	String evaluated_at;
	String published_at;

	String last_error;
};

class AIRepairWorkflow {
	static String _make_task_id();

public:
	static Error load(Vector<AIRepairTask> &r_tasks);
	static Error save(const Vector<AIRepairTask> &p_tasks);
	static Error append(const AIRepairTask &p_task);
	static Error update_task(const String &p_id, AIRepairTask::State p_state, const String &p_error = String());

	// Dirty worktree protection: returns list of dirty files NOT in candidate_files.
	static Vector<String> check_dirty_worktree(const Vector<String> &p_candidate_files);

	// Suggest a default test command based on file extensions.
	static String suggest_default_test(const Vector<String> &p_files);

	// Record a pre-patch snapshot (git diff) for the given files; returns the diff text.
	static String record_pre_patch_snapshot(const Vector<String> &p_files);

	// Run the test command and capture output.
	static Error run_repair_tests(const String &p_test_command, String &r_output, int &r_exit_code);
};
