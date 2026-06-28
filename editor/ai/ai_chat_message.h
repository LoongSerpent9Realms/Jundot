/*  ai_chat_message.h                                                     */
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

#include "core/variant/variant.h"
#include "scene/gui/box_container.h"

class Button;
class HBoxContainer;
class Label;
class PanelContainer;
class RichTextLabel;
class VBoxContainer;

class AIChatMessage : public VBoxContainer {
	GDCLASS(AIChatMessage, VBoxContainer);

	bool is_user = false;
	bool is_summary = false;
	String message_content;
	String think_content;
	double think_time_seconds = 0.0;
	int prompt_tokens = 0;
	int completion_tokens = 0;
	bool think_expanded = false;

	// Layout containers.
	HBoxContainer *alignment_box = nullptr;
	PanelContainer *bubble = nullptr;
	VBoxContainer *bubble_content = nullptr;

	// Content elements.
	Label *author_label = nullptr;
	RichTextLabel *content_label = nullptr;

	// Thinking section.
	VBoxContainer *think_container = nullptr;
	Button *think_toggle = nullptr;
	Label *think_label = nullptr;

	// Footer.
	HBoxContainer *footer = nullptr;
	Label *token_label = nullptr;
	Button *copy_button = nullptr;
	Button *edit_button = nullptr;

	void _toggle_think();
	void _copy_pressed();
	void _edit_pressed();
	void _meta_clicked(const Variant &p_meta);
	void _build_ui();
	void _update_translations();
	void _update_think_visibility();
	void _update_footer();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void setup_user(const String &p_content);
	void setup_ai(const String &p_content, const String &p_think_content = String(), double p_think_time = 0.0, int p_prompt_tokens = 0, int p_completion_tokens = 0);
	void setup_summary(const String &p_content);

	bool is_user_message() const { return is_user; }
	bool is_summary_message() const { return is_summary; }
	String get_content() const;
	String get_think_content() const { return think_content; }
	double get_think_time_seconds() const { return think_time_seconds; }
	void set_content(const String &p_content);
	void set_think_time_seconds(double p_seconds);

	void set_markdown_content(const String &p_content);
	void set_display_scale(float p_scale);

	AIChatMessage();
};

#undef VARIANT_PEEK_STEAL_ONLY
