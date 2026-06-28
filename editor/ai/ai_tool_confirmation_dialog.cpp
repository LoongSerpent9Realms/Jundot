/*  ai_tool_confirmation_dialog.cpp                                         */
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

#include "ai_tool_confirmation_dialog.h"

#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/variant/variant.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/separator.h"

void AIToolConfirmationDialog::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_tool_calls", "tool_calls"), &AIToolConfirmationDialog::set_tool_calls);
	ClassDB::bind_method(D_METHOD("set_callbacks", "confirm", "skip", "cancel"), &AIToolConfirmationDialog::set_callbacks);
}

void AIToolConfirmationDialog::_on_confirm_pressed() {
	if (confirm_callback.is_valid()) {
		confirm_callback.call(current_tool_index);
	}
	// Move to next tool or close
	if (current_tool_index < pending_tools.size() - 1) {
		current_tool_index++;
		_show_current_tool();
	} else {
		hide();
	}
}

void AIToolConfirmationDialog::_on_skip_pressed() {
	if (skip_callback.is_valid()) {
		skip_callback.call(current_tool_index);
	}
	// Move to next tool or close
	if (current_tool_index < pending_tools.size() - 1) {
		current_tool_index++;
		_show_current_tool();
	} else {
		hide();
	}
}

void AIToolConfirmationDialog::_on_cancel_all_pressed() {
	if (cancel_callback.is_valid()) {
		cancel_callback.call();
	}
	hide();
}

void AIToolConfirmationDialog::_show_current_tool() {
	if (pending_tools.is_empty() || current_tool_index >= pending_tools.size()) {
		return;
	}

	const ToolCallInfo &tool = pending_tools[current_tool_index];

	// Update title
	title_label->set_text(vformat(TTR("Tool Call %d of %d"), current_tool_index + 1, pending_tools.size()));

	// Update description with tool info
	description_label->clear();
	description_label->append_text("[b]" + tool.name + "[/b]\n\n");
	description_label->append_text(TTR("Arguments:") + "\n");
	description_label->append_text(tool.arguments_formatted);
}

void AIToolConfirmationDialog::set_tool_calls(const Array &p_tool_calls) {
	pending_tools.clear();
	current_tool_index = 0;

	for (int i = 0; i < p_tool_calls.size(); i++) {
		Dictionary tool_call = p_tool_calls[i];
		if (tool_call.is_empty()) {
			continue;
		}

		ToolCallInfo info;
		info.id = tool_call.get("id", i);
		Dictionary func_dict = tool_call.get("function", Dictionary());
		info.name = func_dict.get("name", String());

		Dictionary args = func_dict.get("arguments", Dictionary());
		info.arguments_raw = args;

		// Format arguments as readable text
		info.arguments_formatted = JSON::stringify(args, "\t");
		if (info.arguments_formatted == "null" || info.arguments_formatted.is_empty()) {
			info.arguments_formatted = "{}";
		}

		pending_tools.push_back(info);
	}

	if (!pending_tools.is_empty()) {
		_show_current_tool();
	}
}

void AIToolConfirmationDialog::set_callbacks(const Callable &p_confirm, const Callable &p_skip, const Callable &p_cancel) {
	confirm_callback = p_confirm;
	skip_callback = p_skip;
	cancel_callback = p_cancel;
}

AIToolConfirmationDialog::AIToolConfirmationDialog() {
	set_title(TTR("AI Tool Call Confirmation"));
	set_ok_button_text(TTR("Execute"));
	set_cancel_button_text(TTR("Cancel"));

	// Create custom content
	VBoxContainer *content_vbox = memnew(VBoxContainer);
	content_vbox->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	add_child(content_vbox);

	// Title label
	title_label = memnew(Label);
	title_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	content_vbox->add_child(title_label);

	// Separator
	HSeparator *sep = memnew(HSeparator);
	content_vbox->add_child(sep);

	// Description with tool info
	description_label = memnew(RichTextLabel);
	description_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	description_label->set_custom_minimum_size(Size2(0, 200) * EDSCALE);
	description_label->set_fit_content(false);
	description_label->set_scroll_active(true);
	description_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	description_label->set_use_bbcode(true); // Enable BBCode for [b] tags
	content_vbox->add_child(description_label);

	// Connect base button signals
	get_ok_button()->connect(SceneStringName(pressed), callable_mp(this, &AIToolConfirmationDialog::_on_confirm_pressed));
	get_cancel_button()->connect(SceneStringName(pressed), callable_mp(this, &AIToolConfirmationDialog::_on_cancel_all_pressed));

	// Create skip button (add to button container if needed, or hook after)
	skip_button = memnew(Button);
	skip_button->set_text(TTR("Skip"));
	skip_button->connect(SceneStringName(pressed), callable_mp(this, &AIToolConfirmationDialog::_on_skip_pressed));

	// Set initial content
	title_label->set_text(TTR("Tool Call Confirmation"));
	description_label->clear();
	description_label->append_text(TTR("AI wants to execute tool calls. Review each call below."));
}
