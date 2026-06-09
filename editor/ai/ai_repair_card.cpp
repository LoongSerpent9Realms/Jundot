/*  ai_repair_card.cpp                                                     */
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

#include "ai_repair_card.h"

#include "core/object/callable_mp.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"

void AIRepairCard::_bind_methods() {
	ADD_SIGNAL(MethodInfo("apply_patch"));
	ADD_SIGNAL(MethodInfo("run_tests"));
	ADD_SIGNAL(MethodInfo("open_files"));
	ADD_SIGNAL(MethodInfo("retry_with_ai"));
	ADD_SIGNAL(MethodInfo("skip_task"));
	ADD_SIGNAL(MethodInfo("rebuild_requested"));
	ADD_SIGNAL(MethodInfo("publish_requested"));
}

void AIRepairCard::_notification(int p_what) {
	if (p_what == NOTIFICATION_TRANSLATION_CHANGED) {
		_build_detail_text();
		_update_buttons();
	}
}

static const char *_state_label_text(AIRepairTask::State p_state) {
	switch (p_state) {
		case AIRepairTask::STATE_PENDING:
			return TTRC("Pending");
		case AIRepairTask::STATE_APPLIED:
			return TTRC("Applied");
		case AIRepairTask::STATE_TESTS_PASSED:
			return TTRC("Tests Passed");
		case AIRepairTask::STATE_TESTS_FAILED:
			return TTRC("Tests Failed");
		case AIRepairTask::STATE_BUILD_TRIGGERED:
			return TTRC("Building...");
		case AIRepairTask::STATE_BUILD_SUCCEEDED:
			return TTRC("Build OK");
		case AIRepairTask::STATE_BUILD_FAILED:
			return TTRC("Build Failed");
		case AIRepairTask::STATE_EVALUATED:
			return TTRC("Ready");
		case AIRepairTask::STATE_PUBLISHED:
			return TTRC("Published");
	}
	return TTRC("?");
}

void AIRepairCard::_toggle_expand() {
	expanded = !expanded;
	detail_container->set_visible(expanded);
	expand_button->set_text(expanded ? TTR("Less") : TTR("More"));
}

void AIRepairCard::_apply_pressed() {
	emit_signal(SNAME("apply_patch"));
}

void AIRepairCard::_test_pressed() {
	emit_signal(SNAME("run_tests"));
}

void AIRepairCard::_open_files_pressed() {
	emit_signal(SNAME("open_files"));
}

void AIRepairCard::_retry_ai_pressed() {
	emit_signal(SNAME("retry_with_ai"));
}

void AIRepairCard::_skip_pressed() {
	emit_signal(SNAME("skip_task"));
}

void AIRepairCard::_rebuild_pressed() {
	emit_signal(SNAME("rebuild_requested"));
}

void AIRepairCard::_publish_pressed() {
	emit_signal(SNAME("publish_requested"));
}

void AIRepairCard::_build_ui() {
	VBoxContainer *vbox = memnew(VBoxContainer);
	vbox->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	vbox->add_theme_constant_override("separation", 4 * EDSCALE);
	add_child(vbox);

	header = memnew(HBoxContainer);
	header->add_theme_constant_override("separation", 6 * EDSCALE);
	vbox->add_child(header);

	type_label = memnew(Label);
	type_label->set_custom_minimum_size(Size2(64 * EDSCALE, 0));
	type_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	header->add_child(type_label);

	title_label = memnew(Label);
	title_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	title_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	header->add_child(title_label);

	state_label = memnew(Label);
	state_label->set_custom_minimum_size(Size2(80 * EDSCALE, 0));
	state_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	header->add_child(state_label);

	expand_button = memnew(Button);
	expand_button->connect(SceneStringName(pressed), callable_mp(this, &AIRepairCard::_toggle_expand));
	header->add_child(expand_button);

	open_files_button = memnew(Button);
	open_files_button->set_text(TTR("Locate"));
	open_files_button->connect(SceneStringName(pressed), callable_mp(this, &AIRepairCard::_open_files_pressed));
	header->add_child(open_files_button);

	apply_button = memnew(Button);
	apply_button->connect(SceneStringName(pressed), callable_mp(this, &AIRepairCard::_apply_pressed));
	header->add_child(apply_button);

	test_button = memnew(Button);
	test_button->set_text(TTR("Run Tests"));
	test_button->connect(SceneStringName(pressed), callable_mp(this, &AIRepairCard::_test_pressed));
	test_button->set_visible(false);
	header->add_child(test_button);

	retry_ai_button = memnew(Button);
	retry_ai_button->set_text(TTR("Ask AI"));
	retry_ai_button->connect(SceneStringName(pressed), callable_mp(this, &AIRepairCard::_retry_ai_pressed));
	retry_ai_button->set_visible(false);
	header->add_child(retry_ai_button);

	skip_button = memnew(Button);
	skip_button->connect(SceneStringName(pressed), callable_mp(this, &AIRepairCard::_skip_pressed));
	header->add_child(skip_button);

	rebuild_button = memnew(Button);
	rebuild_button->connect(SceneStringName(pressed), callable_mp(this, &AIRepairCard::_rebuild_pressed));
	rebuild_button->set_visible(false);
	header->add_child(rebuild_button);

	publish_button = memnew(Button);
	publish_button->connect(SceneStringName(pressed), callable_mp(this, &AIRepairCard::_publish_pressed));
	publish_button->set_visible(false);
	header->add_child(publish_button);

	detail_container = memnew(VBoxContainer);
	detail_container->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	detail_container->set_visible(false);
	vbox->add_child(detail_container);

	detail_label = memnew(Label);
	detail_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	detail_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	detail_container->add_child(detail_label);

	test_result_container = memnew(VBoxContainer);
	test_result_container->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	test_result_container->set_visible(false);
	test_result_container->add_theme_constant_override("separation", 4 * EDSCALE);
	detail_container->add_child(test_result_container);

	Label *test_header = memnew(Label);
	test_header->set_text(TTR("Test Output:"));
	test_header->add_theme_color_override("font_color", Color(0.5, 0.5, 0.5));
	test_result_container->add_child(test_header);

	test_output_label = memnew(Label);
	test_output_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	test_output_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	test_output_label->add_theme_font_size_override("font_size", 12 * EDSCALE);
	test_result_container->add_child(test_output_label);
}

void AIRepairCard::_build_detail_text() {
	String d;
	d += TTR("Issue:") + " " + task.issue_type + "\n";
	d += TTR("Reproduction:") + " " + task.reproduction + "\n";
	d += TTR("Root Cause:") + " " + task.root_cause + "\n";

	String files;
	for (int i = 0; i < task.candidate_files.size(); i++) {
		if (i > 0) {
			files += ", ";
		}
		files += task.candidate_files[i];
	}
	d += TTR("Files:") + " " + files + "\n";
	d += TTR("Patch:") + " " + task.patch_summary + "\n";
	d += TTR("Test Command:") + " " + task.test_command + "\n";
	d += TTR("Risk:") + " " + task.risk + "\n";

	if (!task.last_error.is_empty()) {
		d += TTR("Last Error:") + " " + task.last_error + "\n";
	}
	detail_label->set_text(d);
}

void AIRepairCard::_update_buttons() {
	expand_button->set_text(expanded ? TTR("Less") : TTR("More"));

	switch (task.state) {
		case AIRepairTask::STATE_PENDING:
			apply_button->set_text(TTR("Apply Patch"));
			apply_button->set_visible(true);
			open_files_button->set_visible(true);
			test_button->set_visible(false);
			retry_ai_button->set_visible(false);
			skip_button->set_text(TTR("Skip"));
			skip_button->set_visible(true);
			rebuild_button->set_visible(false);
			publish_button->set_visible(false);
			break;
		case AIRepairTask::STATE_APPLIED:
			apply_button->set_text(TTR("Re-Apply"));
			apply_button->set_visible(true);
			open_files_button->set_visible(true);
			test_button->set_text(TTR("Run Tests"));
			test_button->set_visible(true);
			retry_ai_button->set_visible(false);
			skip_button->set_text(TTR("Skip"));
			skip_button->set_visible(true);
			rebuild_button->set_visible(false);
			publish_button->set_visible(false);
			break;
		case AIRepairTask::STATE_TESTS_FAILED:
			apply_button->set_visible(false);
			open_files_button->set_visible(true);
			test_button->set_text(TTR("Retry Tests"));
			test_button->set_visible(true);
			retry_ai_button->set_visible(true);
			skip_button->set_text(TTR("Skip"));
			skip_button->set_visible(true);
			rebuild_button->set_visible(false);
			publish_button->set_visible(false);
			break;
		case AIRepairTask::STATE_TESTS_PASSED:
		case AIRepairTask::STATE_BUILD_FAILED:
			apply_button->set_visible(false);
			open_files_button->set_visible(true);
			test_button->set_visible(false);
			retry_ai_button->set_visible(task.state == AIRepairTask::STATE_BUILD_FAILED);
			skip_button->set_text(TTR("Dismiss"));
			skip_button->set_visible(true);
			rebuild_button->set_visible(true);
			publish_button->set_visible(false);
			break;
		case AIRepairTask::STATE_BUILD_SUCCEEDED:
		case AIRepairTask::STATE_EVALUATED:
			apply_button->set_visible(false);
			open_files_button->set_visible(true);
			test_button->set_visible(false);
			retry_ai_button->set_visible(false);
			skip_button->set_text(TTR("Dismiss"));
			skip_button->set_visible(true);
			rebuild_button->set_visible(false);
			publish_button->set_visible(true);
			break;
		case AIRepairTask::STATE_PUBLISHED:
			apply_button->set_visible(false);
			open_files_button->set_visible(true);
			test_button->set_visible(false);
			retry_ai_button->set_visible(false);
			skip_button->set_text(TTR("Dismiss"));
			skip_button->set_visible(true);
			rebuild_button->set_visible(false);
			publish_button->set_visible(false);
			break;
		default:
			break;
	}

	state_label->set_text(_state_label_text(task.state));
}

void AIRepairCard::setup(const AIRepairTask &p_repair_task) {
	task = p_repair_task;
	type_label->set_text(task.issue_type == "performance_bottleneck" ? TTR("Perf") : TTR("Fix"));
	title_label->set_text(task.title);
	_build_detail_text();
	_update_buttons();
}

AIRepairTask AIRepairCard::get_task() const {
	return task;
}

void AIRepairCard::set_test_result(const String &p_output, int p_exit_code) {
	test_result_container->set_visible(true);
	String s;
	s += TTR("Exit code:") + " " + itos(p_exit_code) + "\n";
	s += p_output;
	test_output_label->set_text(s);

	if (!expanded) {
		expanded = true;
		detail_container->set_visible(true);
		expand_button->set_text(TTR("Less"));
	}
}

void AIRepairCard::set_dirty_warning(const String &p_warning) {
	if (p_warning.is_empty()) {
		return;
	}
	detail_label->set_text(detail_label->get_text() + "\n\n[color=#f0ad4e]" + TTR("Dirty worktree warning:") + " " + p_warning + "[/color]");
}

AIRepairCard::AIRepairCard() {
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
	add_theme_constant_override("margin_left", 8 * EDSCALE);
	add_theme_constant_override("margin_top", 4 * EDSCALE);
	add_theme_constant_override("margin_right", 8 * EDSCALE);
	add_theme_constant_override("margin_bottom", 4 * EDSCALE);

	_build_ui();
}
