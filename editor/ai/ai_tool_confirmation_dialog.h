/*  ai_tool_confirmation_dialog.h                                           */
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

#include "scene/gui/dialogs.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/rich_text_label.h"

class Label;
class Button;
class HBoxContainer;
class VBoxContainer;
class GridContainer;
class LineEdit;

class AIToolConfirmationDialog : public ConfirmationDialog {
	GDCLASS(AIToolConfirmationDialog, ConfirmationDialog)

public:
	enum class ToolAction {
		EXECUTE,    // User confirmed
		SKIP,       // User skipped this tool
		CANCEL_ALL  // User cancelled all remaining tools
	};

private:
	struct ToolCallInfo {
		int id;
		String name;
		String arguments_formatted;
		Dictionary arguments_raw;
	};

	Vector<ToolCallInfo> pending_tools;
	int current_tool_index = 0;

	Label *title_label = nullptr;
	RichTextLabel *description_label = nullptr;
	VBoxContainer *args_container = nullptr;
	HBoxContainer *button_container = nullptr;
	Button *skip_button = nullptr;
	Button *cancel_all_button = nullptr;

	Callable confirm_callback;
	Callable skip_callback;
	Callable cancel_callback;

	void _on_confirm_pressed();
	void _on_skip_pressed();
	void _on_cancel_all_pressed();
	void _show_current_tool();

protected:
	static void _bind_methods();

public:
	void set_tool_calls(const Array &p_tool_calls);
	void set_callbacks(const Callable &p_confirm, const Callable &p_skip, const Callable &p_cancel);

	AIToolConfirmationDialog();
};
