/*  ai_repair_card.h                                                       */
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

#include "ai_repair_workflow.h"

#include "scene/gui/panel_container.h"

class Button;
class HBoxContainer;
class Label;
class VBoxContainer;

class AIRepairCard : public PanelContainer {
	GDCLASS(AIRepairCard, PanelContainer)

	AIRepairTask task;
	bool expanded = false;

	HBoxContainer *header = nullptr;
	Label *type_label = nullptr;
	Label *title_label = nullptr;
	Label *state_label = nullptr;
	Button *expand_button = nullptr;
	Button *apply_button = nullptr;
	Button *test_button = nullptr;
	Button *open_files_button = nullptr;
	Button *retry_ai_button = nullptr;
	Button *skip_button = nullptr;
	Button *rebuild_button = nullptr;
	Button *publish_button = nullptr;
	VBoxContainer *detail_container = nullptr;
	Label *detail_label = nullptr;
	Label *test_output_label = nullptr;
	VBoxContainer *test_result_container = nullptr;

	void _toggle_expand();
	void _apply_pressed();
	void _test_pressed();
	void _open_files_pressed();
	void _retry_ai_pressed();
	void _skip_pressed();
	void _rebuild_pressed();
	void _publish_pressed();
	void _build_ui();
	void _build_detail_text();
	void _update_buttons();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void setup(const AIRepairTask &p_repair_task);
	AIRepairTask get_task() const;
	void set_test_result(const String &p_output, int p_exit_code);
	void set_dirty_warning(const String &p_warning);

	AIRepairCard();
};
