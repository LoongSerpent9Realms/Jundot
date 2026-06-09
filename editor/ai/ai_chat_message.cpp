/*  ai_chat_message.cpp                                                  */
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

#include "ai_chat_message.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/variant/variant.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/rich_text_label.h"
#include "scene/main/window.h"
#include "scene/resources/style_box_flat.h"
#include "servers/display/display_server.h"

static String _markdown_to_bbcode(const String &p_md) {
	Vector<String> code_blocks;
	Vector<String> inline_codes;
	String s = p_md;

	// Extract code blocks (```...\n```).
	int pos = 0;
	while (true) {
		int fence = s.find("```", pos);
		if (fence == -1) {
			break;
		}

		int content_start = s.find("\n", fence);
		if (content_start == -1) {
			break;
		}
		content_start++;

		int fence_end = s.find("```", content_start);
		if (fence_end == -1) {
			break;
		}

		String content = s.substr(content_start, fence_end - content_start).strip_edges();
		code_blocks.push_back(content);

		String placeholder = "[CODEBLOCK_" + vformat("%d", code_blocks.size() - 1) + "]";
		s = s.substr(0, fence) + placeholder + s.substr(fence_end + 3);
		pos = fence + placeholder.length();
	}

	// Extract inline code (`...`).
	pos = 0;
	while (true) {
		int tick = s.find("`", pos);
		if (tick == -1) {
			break;
		}

		int end = s.find("`", tick + 1);
		if (end == -1) {
			break;
		}

		String code = s.substr(tick + 1, end - tick - 1);
		inline_codes.push_back(code);

		String placeholder = "[INLINECODE_" + vformat("%d", inline_codes.size() - 1) + "]";
		s = s.substr(0, tick) + placeholder + s.substr(end + 1);
		pos = tick + placeholder.length();
	}

	// Process bold (**...**).
	pos = 0;
	while (true) {
		int bold_start = s.find("**", pos);
		if (bold_start == -1) {
			break;
		}

		int bold_end = s.find("**", bold_start + 2);
		if (bold_end == -1) {
			break;
		}

		String bold = s.substr(bold_start + 2, bold_end - bold_start - 2);
		String replacement = "[b]" + bold + "[/b]";
		s = s.substr(0, bold_start) + replacement + s.substr(bold_end + 2);
		pos = bold_start + replacement.length();
	}

	// Process italic (_..._).
	pos = 0;
	while (true) {
		int italic_start = s.find("_", pos);
		if (italic_start == -1) {
			break;
		}

		// Skip __ (bold underline).
		if (italic_start + 1 < s.length() && s[italic_start + 1] == '_') {
			pos = italic_start + 2;
			continue;
		}

		int italic_end = s.find("_", italic_start + 1);
		if (italic_end == -1) {
			break;
		}

		String italic = s.substr(italic_start + 1, italic_end - italic_start - 1);
		String replacement = "[i]" + italic + "[/i]";
		s = s.substr(0, italic_start) + replacement + s.substr(italic_end + 1);
		pos = italic_start + replacement.length();
	}

	// Restore inline code.
	for (int j = 0; j < inline_codes.size(); j++) {
		String placeholder = "[INLINECODE_" + vformat("%d", j) + "]";
		s = s.replace(placeholder, "[code]" + inline_codes[j] + "[/code]");
	}

	// Restore code blocks.
	for (int j = 0; j < code_blocks.size(); j++) {
		String placeholder = "[CODEBLOCK_" + vformat("%d", j) + "]";
		s = s.replace(placeholder, "[code]" + code_blocks[j] + "[/code]");
	}

	return s;
}

void AIChatMessage::_bind_methods() {
	ADD_SIGNAL(MethodInfo("edit_requested", PropertyInfo(Variant::STRING, "content")));
}

void AIChatMessage::_notification(int p_what) {
	if (p_what == NOTIFICATION_TRANSLATION_CHANGED) {
		_update_translations();
	}
	if (p_what == NOTIFICATION_THEME_CHANGED) {
		// Apply bubble colors based on editor theme.
		Ref<StyleBoxFlat> bubble_style;
		bubble_style.instantiate();
		bubble_style->set_corner_radius_all(8 * EDSCALE);
		bubble_style->set_content_margin_all(10 * EDSCALE);

		Color base = get_theme_color(SNAME("base_color"), SNAME("Editor"));

		if (is_user) {
			// User bubble: slightly lighter/different from base.
			Color user_bg = base.lightened(0.08f);
			user_bg.a = 0.85f;
			bubble_style->set_bg_color(user_bg);
			bubble_style->set_border_width_all(1);
			bubble_style->set_border_color(base.lightened(0.15f));
		} else {
			// AI bubble: use base color with subtle border.
			Color ai_bg = base.lightened(0.02f);
			ai_bg.a = 0.85f;
			bubble_style->set_bg_color(ai_bg);
			bubble_style->set_border_width_all(1);
			bubble_style->set_border_color(base.lightened(0.1f));
		}
		bubble->add_theme_style_override(SNAME("panel"), bubble_style);

		// Thinking toggle button style.
		if (think_toggle) {
			Ref<StyleBoxFlat> think_style;
			think_style.instantiate();
			think_style->set_bg_color(Color(0, 0, 0, 0));
			think_style->set_border_width_all(0);
			think_toggle->add_theme_style_override(SNAME("normal"), think_style);
			think_toggle->add_theme_style_override(SNAME("hover"), think_style);
			think_toggle->add_theme_style_override(SNAME("pressed"), think_style);
			Color accent = get_theme_color(SNAME("accent_color"), SNAME("Editor"));
			think_toggle->add_theme_color_override(SNAME("font_color"), accent);
		}
	}
}

void AIChatMessage::_toggle_think() {
	think_expanded = !think_expanded;
	think_label->set_visible(think_expanded);
	_update_think_visibility();
}

void AIChatMessage::_copy_pressed() {
	DisplayServer::get_singleton()->clipboard_set(message_content);
}

void AIChatMessage::_edit_pressed() {
	// Emit signal so parent can populate the input field.
	emit_signal(SNAME("edit_requested"), message_content);
}

void AIChatMessage::_update_think_visibility() {
	if (think_content.is_empty()) {
		think_container->set_visible(false);
		return;
	}
	think_container->set_visible(true);
	String arrow = think_expanded ? String::utf8("\xe2\x96\xbc") : String::utf8("\xe2\x96\xb6");
	String time_text;
	if (think_time_seconds > 0.0) {
		time_text = vformat(TTR(" (%.1f s)"), think_time_seconds);
	}
	think_toggle->set_text(arrow + " " + TTR("Thought") + time_text);
}

void AIChatMessage::_update_footer() {
	String token_text;
	if (prompt_tokens > 0 || completion_tokens > 0) {
		token_text = vformat(TTR("In: %d tokens  Out: %d tokens"), prompt_tokens, completion_tokens);
	}
	token_label->set_text(token_text);
	token_label->set_visible(!token_text.is_empty());
}

void AIChatMessage::_update_translations() {
	author_label->set_text(is_user ? TTR("You") : TTR("AI"));
	copy_button->set_tooltip_text(TTR("Copy message"));
	edit_button->set_tooltip_text(TTR("Edit message"));
	_update_think_visibility();
	_update_footer();
}

void AIChatMessage::_build_ui() {
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
	add_theme_constant_override("separation", 4 * EDSCALE);

	alignment_box = memnew(HBoxContainer);
	alignment_box->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	add_child(alignment_box);

	// Spacers push the bubble to the correct side.
	Control *left_spacer = memnew(Control);
	left_spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	alignment_box->add_child(left_spacer);

	bubble = memnew(PanelContainer);
	bubble->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
	bubble->set_v_size_flags(Control::SIZE_SHRINK_BEGIN);
	alignment_box->add_child(bubble);

	bubble_content = memnew(VBoxContainer);
	bubble_content->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	bubble_content->add_theme_constant_override("separation", 6 * EDSCALE);
	bubble->add_child(bubble_content);

	// Author label (small, subtle).
	author_label = memnew(Label);
	author_label->set_theme_type_variation(SNAME("HeaderSmall"));
	bubble_content->add_child(author_label);

	// Message content.
	content_label = memnew(RichTextLabel);
	content_label->set_fit_content(true);
	content_label->set_selection_enabled(true);
	content_label->set_deselect_on_focus_loss_enabled(true);
	content_label->set_custom_minimum_size(Size2(260 * EDSCALE, 0));
	content_label->set_use_bbcode(true);
	bubble_content->add_child(content_label);

	// Thinking section (AI only).
	think_container = memnew(VBoxContainer);
	think_container->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	think_container->set_visible(false);
	bubble_content->add_child(think_container);

	think_toggle = memnew(Button);
	think_toggle->set_flat(true);
	think_toggle->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
	think_toggle->connect(SceneStringName(pressed), callable_mp(this, &AIChatMessage::_toggle_think));
	think_container->add_child(think_toggle);

	think_label = memnew(Label);
	think_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	think_label->set_v_size_flags(Control::SIZE_SHRINK_BEGIN);
	think_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	think_label->set_visible(false);
	// Slightly muted color for thinking content.
	think_label->add_theme_color_override(SNAME("font_color"), Color(0.6f, 0.6f, 0.6f));
	think_container->add_child(think_label);

	// Footer with token info and action buttons.
	footer = memnew(HBoxContainer);
	footer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	footer->add_theme_constant_override("separation", 8 * EDSCALE);
	bubble_content->add_child(footer);

	token_label = memnew(Label);
	token_label->set_theme_type_variation(SNAME("HeaderSmall"));
	token_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	footer->add_child(token_label);

	copy_button = memnew(Button);
	copy_button->set_flat(true);
	copy_button->set_text(TTR("Copy"));
	copy_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatMessage::_copy_pressed));
	footer->add_child(copy_button);

	edit_button = memnew(Button);
	edit_button->set_flat(true);
	edit_button->set_text(TTR("Edit"));
	edit_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatMessage::_edit_pressed));
	footer->add_child(edit_button);

	Control *right_spacer = memnew(Control);
	right_spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	alignment_box->add_child(right_spacer);
}

void AIChatMessage::setup_user(const String &p_content) {
	is_user = true;
	message_content = p_content;
	content_label->set_text(p_content);
	think_container->set_visible(false);
	footer->set_visible(true);
	token_label->set_visible(false);

	// Align to right: show left spacer to push bubble right, hide right spacer.
	Object::cast_to<Control>(alignment_box->get_child(0))->set_visible(true); // left_spacer
	Object::cast_to<Control>(alignment_box->get_child(alignment_box->get_child_count() - 1))->set_visible(false); // right_spacer

	// Ensure edit button is in footer for user messages.
	if (edit_button && edit_button->get_parent() != footer) {
		footer->add_child(edit_button);
	}

	_update_translations();
}

void AIChatMessage::setup_ai(const String &p_content, const String &p_think_content, double p_think_time, int p_prompt_tokens, int p_completion_tokens) {
	is_user = false;
	message_content = p_content;
	think_content = p_think_content;
	think_time_seconds = p_think_time;
	prompt_tokens = p_prompt_tokens;
	completion_tokens = p_completion_tokens;

	content_label->set_text(_markdown_to_bbcode(p_content));
	think_label->set_text(p_think_content);

	// AI messages: hide edit button, keep only copy.
	if (edit_button && edit_button->is_inside_tree()) {
		footer->remove_child(edit_button);
	}

	// Align to left: hide left spacer, show right spacer to keep bubble left.
	Object::cast_to<Control>(alignment_box->get_child(0))->set_visible(false); // left_spacer
	Object::cast_to<Control>(alignment_box->get_child(alignment_box->get_child_count() - 1))->set_visible(true); // right_spacer

	_update_think_visibility();
	_update_footer();
	_update_translations();
}

String AIChatMessage::get_content() const {
	return message_content;
}

void AIChatMessage::set_content(const String &p_content) {
	message_content = p_content;
	if (is_user) {
		content_label->set_text(p_content);
	} else {
		content_label->set_text(_markdown_to_bbcode(p_content));
	}
}

void AIChatMessage::set_markdown_content(const String &p_content) {
	message_content = p_content;
	content_label->set_text(_markdown_to_bbcode(p_content));
}

AIChatMessage::AIChatMessage() {
	_build_ui();
}
