/**************************************************************************/
/*  ai_chat_panel.h                                                        */
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

#pragma once

#include "core/templates/vector.h"
#include "scene/gui/margin_container.h"

class AIChatService;
class Button;
class EditorFileDialog;
class HBoxContainer;
class Label;
class MenuButton;
class ScrollContainer;
class TextEdit;
class VBoxContainer;

class AIChatPanel : public MarginContainer {
	GDCLASS(AIChatPanel, MarginContainer)

	enum IterationMode {
		ITERATION_MODE_DEFECTS,
		ITERATION_MODE_FEATURE,
		ITERATION_MODE_HYBRID,
	};

	AIChatService *chat_service = nullptr;
	VBoxContainer *message_list = nullptr;
	ScrollContainer *message_scroll = nullptr;
	HBoxContainer *attachment_chips = nullptr;
	TextEdit *input = nullptr;
	Button *send_button = nullptr;
	Button *cancel_button = nullptr;
	Button *clear_button = nullptr;
	MenuButton *add_file_menu = nullptr;
	Label *status_label = nullptr;
	EditorFileDialog *reference_file_dialog = nullptr;
	EditorFileDialog *upload_file_dialog = nullptr;
	Button *mode_buttons[3] = {};
	IterationMode current_mode = ITERATION_MODE_DEFECTS;

	struct ChatAttachment {
		String path;
		String display_name;
		String content;
		bool external = false;
	};

	Vector<ChatAttachment> attachments;

	void _set_mode(int p_mode);
	void _send_message();
	void _cancel_request();
	void _clear_messages();
	void _chat_completed(int p_result, int p_response_code, const String &p_content, const Dictionary &p_json, const String &p_raw_body);
	void _add_message(const String &p_author, const String &p_text);
	void _add_file_menu_id_pressed(int p_id);
	void _project_file_selected(const String &p_path);
	void _external_file_selected(const String &p_path);
	void _add_attachment(const String &p_path, bool p_external);
	void _remove_attachment(int p_index);
	void _refresh_attachment_chips();
	String _build_attachment_context() const;
	String _get_mode_prompt() const;
	String _get_mode_button_text(int p_mode) const;
	void _update_translations();
	void _update_mode_buttons();
	void _set_requesting(bool p_requesting);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	AIChatPanel();
};
