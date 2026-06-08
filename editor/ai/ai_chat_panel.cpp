/**************************************************************************/
/*  ai_chat_panel.cpp                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             JUNDOT ENGINE                               */
/*                        https://jundotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Jundot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
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
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "ai_chat_panel.h"

#include "ai_chat_service.h"
#include "ai_settings.h"

#include "core/io/file_access.h"
#include "core/object/callable_mp.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/text_edit.h"
#include "scene/main/http_request.h"

static constexpr int AI_CHAT_ATTACHMENT_MAX_BYTES = 64 * 1024;

void AIChatPanel::_bind_methods() {
}

void AIChatPanel::_notification(int p_what) {
	if (p_what == NOTIFICATION_TRANSLATION_CHANGED) {
		_update_translations();
	}
}

String AIChatPanel::_get_mode_prompt() const {
	switch (current_mode) {
		case ITERATION_MODE_DEFECTS:
			return TTR("Mode: Defect discovery. Prioritize engine logs, crash reports, code patterns, root causes, and performance bottlenecks.");
		case ITERATION_MODE_FEATURE:
			return TTR("Mode: Feature evaluation. Judge whether a requested feature is generally useful and necessary before expanding scope.");
		case ITERATION_MODE_HYBRID:
			return TTR("Mode: Hybrid expansion. Propose AI-assisted changes while preserving developer confirmation and manual collaboration.");
	}
	return String();
}

String AIChatPanel::_get_mode_button_text(int p_mode) const {
	switch ((IterationMode)p_mode) {
		case ITERATION_MODE_DEFECTS:
			return TTR("Defects");
		case ITERATION_MODE_FEATURE:
			return TTR("Feature");
		case ITERATION_MODE_HYBRID:
			return TTR("Hybrid");
	}
	return String();
}

void AIChatPanel::_update_translations() {
	set_name(TTRC("Chat"));
	for (int i = 0; i < 3; i++) {
		mode_buttons[i]->set_text(_get_mode_button_text(i));
	}
	input->set_placeholder(TTR("Input message / drop files here"));
	add_file_menu->set_text(TTR("+"));
	add_file_menu->set_tooltip_text(TTR("Attach or reference a text file"));
	PopupMenu *file_popup = add_file_menu->get_popup();
	file_popup->set_item_text(file_popup->get_item_index(0), TTR("Reference Project File"));
	file_popup->set_item_text(file_popup->get_item_index(1), TTR("Upload Text File"));
	clear_button->set_text(TTR("Clear"));
	cancel_button->set_text(TTR("Cancel"));
	send_button->set_text(TTR("Send"));
	reference_file_dialog->set_title(TTR("Reference Project File"));
	upload_file_dialog->set_title(TTR("Upload Text File"));
	_refresh_attachment_chips();
	status_label->set_text(_get_mode_prompt());
}

void AIChatPanel::_update_mode_buttons() {
	for (int i = 0; i < 3; i++) {
		mode_buttons[i]->set_pressed(i == current_mode);
	}
}

void AIChatPanel::_set_mode(int p_mode) {
	current_mode = (IterationMode)p_mode;
	_update_mode_buttons();
	status_label->set_text(_get_mode_prompt());
}

void AIChatPanel::_set_requesting(bool p_requesting) {
	send_button->set_disabled(p_requesting);
	cancel_button->set_disabled(!p_requesting);
}

void AIChatPanel::_add_message(const String &p_author, const String &p_text) {
	Label *message = memnew(Label);
	message->set_text(vformat("%s\n%s", p_author, p_text));
	message->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	message->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	message_list->add_child(message);
	message_scroll->set_deferred(SNAME("scroll_vertical"), message_scroll->get_v_scroll_bar()->get_max());
}

void AIChatPanel::_add_file_menu_id_pressed(int p_id) {
	if (p_id == 0) {
		reference_file_dialog->popup_file_dialog();
	} else if (p_id == 1) {
		upload_file_dialog->popup_file_dialog();
	}
}

void AIChatPanel::_project_file_selected(const String &p_path) {
	_add_attachment(p_path, false);
}

void AIChatPanel::_external_file_selected(const String &p_path) {
	_add_attachment(p_path, true);
}

void AIChatPanel::_add_attachment(const String &p_path, bool p_external) {
	Error err = OK;
	const int64_t size = FileAccess::get_size(p_path);
	if (size < 0) {
		status_label->set_text(TTR("Could not read the selected file."));
		return;
	}
	if (size > AI_CHAT_ATTACHMENT_MAX_BYTES) {
		status_label->set_text(vformat(TTR("Selected file is larger than %d KB."), AI_CHAT_ATTACHMENT_MAX_BYTES / 1024));
		return;
	}

	const String content = FileAccess::get_file_as_string(p_path, &err);
	if (err != OK) {
		status_label->set_text(TTR("Could not read the selected file as text."));
		return;
	}

	ChatAttachment attachment;
	attachment.path = p_path;
	attachment.display_name = p_path.get_file();
	attachment.content = content;
	attachment.external = p_external;
	attachments.push_back(attachment);

	_refresh_attachment_chips();
	status_label->set_text(vformat(TTR("Attached %s."), attachment.display_name));
}

void AIChatPanel::_remove_attachment(int p_index) {
	ERR_FAIL_INDEX(p_index, attachments.size());
	attachments.remove_at(p_index);
	_refresh_attachment_chips();
}

void AIChatPanel::_refresh_attachment_chips() {
	for (int i = attachment_chips->get_child_count() - 1; i >= 0; i--) {
		attachment_chips->get_child(i)->queue_free();
	}

	for (int i = 0; i < attachments.size(); i++) {
		HBoxContainer *chip = memnew(HBoxContainer);
		chip->add_theme_constant_override("separation", 2 * EDSCALE);
		attachment_chips->add_child(chip);

		Button *name = memnew(Button);
		name->set_text(attachments[i].display_name);
		name->set_tooltip_text(attachments[i].path);
		name->set_disabled(true);
		chip->add_child(name);

		Button *remove = memnew(Button);
		remove->set_text(TTR("Remove"));
		remove->set_tooltip_text(TTR("Remove attachment"));
		remove->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_remove_attachment).bind(i));
		chip->add_child(remove);
	}
}

String AIChatPanel::_build_attachment_context() const {
	if (attachments.is_empty()) {
		return String();
	}

	String context = TTR("Attached text files for this message:");
	for (int i = 0; i < attachments.size(); i++) {
		const ChatAttachment &attachment = attachments[i];
		context += "\n\n";
		context += vformat("[%s] %s\n%s\n", attachment.external ? TTR("Uploaded") : TTR("Referenced"), attachment.display_name, vformat(TTR("Path: %s"), attachment.path));
		context += "```text\n";
		context += attachment.content;
		context += "\n```";
	}
	return context;
}

void AIChatPanel::_send_message() {
	const String text = input->get_text().strip_edges();
	if (text.is_empty()) {
		return;
	}

	AISettingsData settings = AISettings::load();
	if (settings.api_key.is_empty()) {
		status_label->set_text(TTR("Configure an API key before sending AI messages."));
		return;
	}

	settings.system_prompt = _get_mode_prompt() + "\n" + settings.system_prompt;
	chat_service->configure(settings);

	String request_text = text;
	const String attachment_context = _build_attachment_context();
	if (!attachment_context.is_empty()) {
		request_text += "\n\n" + attachment_context;
	}

	const Error err = chat_service->send_chat(request_text);
	if (err != OK) {
		status_label->set_text(TTR("AI request could not start."));
		return;
	}

	_add_message(TTR("You"), text);
	input->clear();
	attachments.clear();
	_refresh_attachment_chips();
	status_label->set_text(TTR("Waiting for AI response..."));
	_set_requesting(true);
}

void AIChatPanel::_cancel_request() {
	chat_service->cancel_request();
	status_label->set_text(TTR("AI request cancelled."));
	_set_requesting(false);
}

void AIChatPanel::_clear_messages() {
	for (int i = message_list->get_child_count() - 1; i >= 0; i--) {
		message_list->get_child(i)->queue_free();
	}
	status_label->set_text(_get_mode_prompt());
}

void AIChatPanel::_chat_completed(int p_result, int p_response_code, const String &p_content, const Dictionary &p_json, const String &p_raw_body) {
	_set_requesting(false);
	if (p_result == HTTPRequest::RESULT_SUCCESS && p_response_code < HTTPClient::RESPONSE_BAD_REQUEST) {
		_add_message(TTR("AI"), p_content);
		status_label->set_text(TTR("AI response received."));
		return;
	}

	String error_text = p_content;
	if (error_text.is_empty()) {
		error_text = vformat(TTR("AI request failed. HTTP %d."), p_response_code);
	}
	_add_message(TTR("AI Error"), error_text);
	status_label->set_text(error_text);
}

AIChatPanel::AIChatPanel() {
	set_name(TTRC("Chat"));
	add_theme_constant_override("margin_left", 8 * EDSCALE);
	add_theme_constant_override("margin_top", 8 * EDSCALE);
	add_theme_constant_override("margin_right", 8 * EDSCALE);
	add_theme_constant_override("margin_bottom", 8 * EDSCALE);

	VBoxContainer *root = memnew(VBoxContainer);
	root->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	root->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	root->add_theme_constant_override("separation", 8 * EDSCALE);
	add_child(root);

	HBoxContainer *modes = memnew(HBoxContainer);
	modes->add_theme_constant_override("separation", 4 * EDSCALE);
	root->add_child(modes);

	for (int i = 0; i < 3; i++) {
		mode_buttons[i] = memnew(Button);
		mode_buttons[i]->set_toggle_mode(true);
		mode_buttons[i]->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_set_mode).bind(i));
		modes->add_child(mode_buttons[i]);
	}

	message_scroll = memnew(ScrollContainer);
	message_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	message_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	root->add_child(message_scroll);

	message_list = memnew(VBoxContainer);
	message_list->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	message_list->add_theme_constant_override("separation", 10 * EDSCALE);
	message_scroll->add_child(message_list);

	PanelContainer *composer = memnew(PanelContainer);
	composer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	composer->set_custom_minimum_size(Size2(0, 180) * EDSCALE);
	root->add_child(composer);

	VBoxContainer *composer_vb = memnew(VBoxContainer);
	composer_vb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	composer_vb->add_theme_constant_override("separation", 6 * EDSCALE);
	composer->add_child(composer_vb);

	HBoxContainer *attachment_row = memnew(HBoxContainer);
	composer_vb->add_child(attachment_row);

	attachment_chips = memnew(HBoxContainer);
	attachment_chips->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	attachment_chips->add_theme_constant_override("separation", 4 * EDSCALE);
	attachment_row->add_child(attachment_chips);

	input = memnew(TextEdit);
	input->set_custom_minimum_size(Size2(0, 96) * EDSCALE);
	input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	composer_vb->add_child(input);

	HBoxContainer *actions = memnew(HBoxContainer);
	actions->add_theme_constant_override("separation", 6 * EDSCALE);
	composer_vb->add_child(actions);

	add_file_menu = memnew(MenuButton);
	add_file_menu->get_popup()->add_item(TTR("Reference Project File"), 0);
	add_file_menu->get_popup()->add_item(TTR("Upload Text File"), 1);
	add_file_menu->get_popup()->connect(SceneStringName(id_pressed), callable_mp(this, &AIChatPanel::_add_file_menu_id_pressed));
	actions->add_child(add_file_menu);

	clear_button = memnew(Button);
	clear_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_clear_messages));
	actions->add_child(clear_button);

	Control *spacer = memnew(Control);
	spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	actions->add_child(spacer);

	cancel_button = memnew(Button);
	cancel_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_cancel_request));
	actions->add_child(cancel_button);

	send_button = memnew(Button);
	send_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_send_message));
	actions->add_child(send_button);

	status_label = memnew(Label);
	status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	root->add_child(status_label);

	chat_service = memnew(AIChatService);
	chat_service->connect(SNAME("chat_completed"), callable_mp(this, &AIChatPanel::_chat_completed));
	add_child(chat_service, false, INTERNAL_MODE_BACK);

	reference_file_dialog = memnew(EditorFileDialog);
	reference_file_dialog->set_access(EditorFileDialog::ACCESS_RESOURCES);
	reference_file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
	reference_file_dialog->add_filter("*.txt,*.md,*.log,*.gd,*.cs,*.cpp,*.h,*.hpp,*.json,*.cfg,*.ini,*.shader", TTRC("Text Files"));
	reference_file_dialog->connect(SNAME("file_selected"), callable_mp(this, &AIChatPanel::_project_file_selected));
	add_child(reference_file_dialog);

	upload_file_dialog = memnew(EditorFileDialog);
	upload_file_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	upload_file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
	upload_file_dialog->add_filter("*.txt,*.md,*.log,*.json,*.cfg,*.ini,*.cpp,*.h,*.hpp,*.cs,*.gd,*.shader", TTRC("Text Files"));
	upload_file_dialog->connect(SNAME("file_selected"), callable_mp(this, &AIChatPanel::_external_file_selected));
	add_child(upload_file_dialog);

	_update_translations();
	_update_mode_buttons();
	_set_requesting(false);
	status_label->set_text(_get_mode_prompt());
}
