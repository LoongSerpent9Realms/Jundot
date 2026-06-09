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

// Helper: check if a character is a word character (alphanumeric or underscore).
static bool _is_word_char(char32_t c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// Helper: check if a trimmed line is a horizontal rule (---, ***, ___ with 3+ chars).
static bool _is_horizontal_rule(const String &p_line) {
	if (p_line.length() < 3) {
		return false;
	}
	char32_t first = p_line[0];
	if (first != '-' && first != '*' && first != '_') {
		return false;
	}
	for (int i = 1; i < p_line.length(); i++) {
		if (p_line[i] != first) {
			return false;
		}
	}
	return true;
}

static String _markdown_to_bbcode(const String &p_md) {
	Vector<String> code_blocks;
	Vector<String> inline_codes;
	String s = p_md;

	// ---------- Step 1: Extract code blocks (```...```) ----------
	{
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
	}

	// ---------- Step 2: Extract inline code (`...`) ----------
	{
		int pos = 0;
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
	}

	// ---------- Step 3: Inline formatting on non-code text ----------
	// Strikethrough (~~text~~) → [s]text[/s]
	{
		int pos = 0;
		while (true) {
			int ss = s.find("~~", pos);
			if (ss == -1) {
				break;
			}
			int se = s.find("~~", ss + 2);
			if (se == -1) {
				break;
			}
			String content = s.substr(ss + 2, se - ss - 2);
			String replacement = "[s]" + content + "[/s]";
			s = s.substr(0, ss) + replacement + s.substr(se + 2);
			pos = ss + replacement.length();
		}
	}

	// Bold (**...**) → [b]...[/b]
	{
		int pos = 0;
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
	}

	// Italic (_..._), skip __ and word-internal underscores.
	{
		int pos = 0;
		while (true) {
			int italic_start = s.find("_", pos);
			if (italic_start == -1) {
				break;
			}
			// Skip __.
			if (italic_start + 1 < s.length() && s[italic_start + 1] == '_') {
				pos = italic_start + 2;
				continue;
			}
			int italic_end = s.find("_", italic_start + 1);
			if (italic_end == -1) {
				break;
			}
			// Only process if surrounded by non-word characters.
			bool start_boundary = (italic_start == 0) || !_is_word_char(s[italic_start - 1]);
			bool end_boundary = (italic_end == s.length() - 1) || !_is_word_char(s[italic_end + 1]);
			if (!start_boundary || !end_boundary) {
				pos = italic_start + 1;
				continue;
			}
			String italic = s.substr(italic_start + 1, italic_end - italic_start - 1);
			String replacement = "[i]" + italic + "[/i]";
			s = s.substr(0, italic_start) + replacement + s.substr(italic_end + 1);
			pos = italic_start + replacement.length();
		}
	}

	// Images ![alt](url) → show alt text in italic (RTL doesn't support inline images easily).
	{
		int pos = 0;
		while (true) {
			int img_start = s.find("![", pos);
			if (img_start == -1) {
				break;
			}
			int alt_end = s.find("]", img_start + 2);
			if (alt_end == -1) {
				break;
			}
			int url_start = s.find("(", alt_end + 1);
			if (url_start == -1) {
				break;
			}
			int url_end = s.find(")", url_start + 1);
			if (url_end == -1) {
				break;
			}
			String alt = s.substr(img_start + 2, alt_end - img_start - 2);
			String replacement = "[i]" + alt + "[/i]"; // fallback: show alt text as italic
			s = s.substr(0, img_start) + replacement + s.substr(url_end + 1);
			pos = img_start + replacement.length();
		}
	}

	// Links [text](url) → [url=url]text[/url] (run after image processing).
	{
		int pos = 0;
		while (true) {
			int link_start = s.find("[", pos);
			if (link_start == -1) {
				break;
			}
			// Check it's not an already-handled image (marked by ! before [).
			if (link_start > 0 && s[link_start - 1] == '!') {
				pos = link_start + 1;
				continue;
			}
			int text_end = s.find("]", link_start + 1);
			if (text_end == -1) {
				break;
			}
			int url_start = s.find("(", text_end + 1);
			if (url_start == -1) {
				break;
			}
			int url_end = s.find(")", url_start + 1);
			if (url_end == -1) {
				break;
			}
			String text = s.substr(link_start + 1, text_end - link_start - 1);
			String url = s.substr(url_start + 1, url_end - url_start - 1);
			String replacement = "[url=" + url + "]" + text + "[/url]";
			s = s.substr(0, link_start) + replacement + s.substr(url_end + 1);
			pos = link_start + replacement.length();
		}
	}

	// ---------- Step 4: Block-level processing (line by line) ----------
	{
		Vector<String> lines = s.split("\n", false);
		s.clear();
		enum ListState { LIST_NONE, LIST_UL, LIST_OL };
		ListState current_list = LIST_NONE;
		bool in_table = false;
		int table_cols = 0;

		for (int i = 0; i < lines.size(); i++) {
			String line = lines[i];
			String trimmed = line.strip_edges();

			// Blank line: flush lists and tables.
			if (trimmed.is_empty()) {
				if (current_list != LIST_NONE) {
					s += "[/" + String(current_list == LIST_UL ? "ul" : "ol") + "]\n";
					current_list = LIST_NONE;
				}
				if (in_table) {
					s += "[/table]\n";
					in_table = false;
				}
				s += "\n";
				continue;
			}

			// Code placeholder lines: preserve as-is after flushing lists/tables.
			if (trimmed.begins_with("[CODEBLOCK_") || trimmed.begins_with("[INLINECODE_")) {
				if (current_list != LIST_NONE) {
					s += "[/" + String(current_list == LIST_UL ? "ul" : "ol") + "]\n";
					current_list = LIST_NONE;
				}
				if (in_table) {
					s += "[/table]\n";
					in_table = false;
				}
				s += line + "\n";
				continue;
			}

			// Horizontal rules (---, ***, ___).
			if (_is_horizontal_rule(trimmed)) {
				if (current_list != LIST_NONE) {
					s += "[/" + String(current_list == LIST_UL ? "ul" : "ol") + "]\n";
					current_list = LIST_NONE;
				}
				if (in_table) {
					s += "[/table]\n";
					in_table = false;
				}
				s += "[hr]\n";
				continue;
			}

			// Headers (# ...).
			if (trimmed.begins_with("#") && trimmed.length() > 1 && trimmed[1] == ' ') {
				if (current_list != LIST_NONE) {
					s += "[/" + String(current_list == LIST_UL ? "ul" : "ol") + "]\n";
					current_list = LIST_NONE;
				}
				if (in_table) {
					s += "[/table]\n";
					in_table = false;
				}
				int level = 0;
				int j = 0;
				while (j < trimmed.length() && trimmed[j] == '#') {
					level++;
					j++;
				}
				String text = trimmed.substr(j).strip_edges();
				static const int header_sizes[] = { 24, 20, 18, 16, 14, 13 };
				int size = header_sizes[MIN(level - 1, 5)];
				s += "[b][font_size=" + itos(size) + "]" + text + "[/font_size][/b]\n";
				continue;
			}
			// Header without space after # (e.g. "#title").
			if (trimmed.begins_with("#") && trimmed[0] == '#') {
				int level = 0;
				int j = 0;
				while (j < trimmed.length() && trimmed[j] == '#') {
					level++;
					j++;
				}
				String text = trimmed.substr(j).strip_edges();
				if (!text.is_empty()) {
					if (current_list != LIST_NONE) {
						s += "[/" + String(current_list == LIST_UL ? "ul" : "ol") + "]\n";
						current_list = LIST_NONE;
					}
					if (in_table) {
						s += "[/table]\n";
						in_table = false;
					}
					static const int header_sizes[] = { 24, 20, 18, 16, 14, 13 };
					int size = header_sizes[MIN(level - 1, 5)];
					s += "[b][font_size=" + itos(size) + "]" + text + "[/font_size][/b]\n";
					continue;
				}
			}

			// Blockquotes (> ..., >> ..., >>> ...).
			if (trimmed.begins_with(">")) {
				if (current_list != LIST_NONE) {
					s += "[/" + String(current_list == LIST_UL ? "ul" : "ol") + "]\n";
					current_list = LIST_NONE;
				}
				if (in_table) {
					s += "[/table]\n";
					in_table = false;
				}
				int level = 0;
				int j = 0;
				while (j < trimmed.length() && trimmed[j] == '>') {
					level++;
					j++;
				}
				String quote_text = trimmed.substr(j).strip_edges();
				String indent_tag;
				for (int k = 0; k < level && k < 3; k++) {
					indent_tag += "[indent]";
				}
				s += indent_tag + "[i][color=#888888]" + quote_text + "[/color][/i]\n";
				for (int k = 0; k < level && k < 3; k++) {
					s += "[/indent]";
				}
				continue;
			}

			// Table rows (| ... |).
			if (trimmed.begins_with("|") && trimmed.ends_with("|")) {
				// Separator row (|---|) -> skip.
				if (trimmed.find("---", 0) != -1 || trimmed.find("===", 0) != -1) {
					if (!in_table) {
						// Separator without preceding header; skip.
					}
					continue;
				}
				String inner = trimmed.substr(1, trimmed.length() - 2);
				Vector<String> cells = inner.split("|");
				for (int c = 0; c < cells.size(); c++) {
					cells.write[c] = cells[c].strip_edges();
				}
				if (!in_table) {
					table_cols = cells.size();
					s += "[table=" + itos(table_cols) + "]\n";
					in_table = true;
				}
				for (int c = 0; c < cells.size(); c++) {
					s += "[cell]" + cells[c] + "[/cell]";
				}
				s += "\n";
				continue;
			}
			if (in_table) {
				s += "[/table]\n";
				in_table = false;
			}

			// Unordered list items (- or * at line start).
			{
				bool is_ul = false;
				String item_text;
				if (trimmed.begins_with("- ") || trimmed.begins_with("* ")) {
					item_text = trimmed.substr(2);
					is_ul = true;
				} else if (trimmed.length() > 1 && trimmed[0] == '-' && !_is_word_char(trimmed[1])) {
					item_text = trimmed.substr(1).strip_edges();
					is_ul = true;
				}
				if (is_ul) {
					if (current_list != LIST_UL) {
						if (current_list != LIST_NONE) {
							s += "[/" + String(current_list == LIST_UL ? "ul" : "ol") + "]\n";
						}
						s += "[ul]\n";
						current_list = LIST_UL;
					}
					s += "[li]" + item_text + "[/li]\n";
					continue;
				}
			}

			// Ordered list items (N. at line start).
			{
				bool is_ol = false;
				String item_text;
				int dot_pos = trimmed.find(". ");
				if (dot_pos > 0 && dot_pos < 4) {
					String prefix = trimmed.substr(0, dot_pos);
					bool all_digits = true;
					for (int c = 0; c < prefix.length(); c++) {
						if (prefix[c] < '0' || prefix[c] > '9') {
							all_digits = false;
							break;
						}
					}
					if (all_digits) {
						item_text = trimmed.substr(dot_pos + 2);
						is_ol = true;
					}
				}
				if (is_ol) {
					if (current_list != LIST_OL) {
						if (current_list != LIST_NONE) {
							s += "[/" + String(current_list == LIST_UL ? "ul" : "ol") + "]\n";
						}
						s += "[ol]\n";
						current_list = LIST_OL;
					}
					s += "[li]" + item_text + "[/li]\n";
					continue;
				}
			}

			// Regular paragraph line.
			{
				if (current_list != LIST_NONE) {
					s += "[/" + String(current_list == LIST_UL ? "ul" : "ol") + "]\n";
					current_list = LIST_NONE;
				}
				s += line + "\n";
			}
		}

		// Flush remaining open list or table.
		if (current_list != LIST_NONE) {
			s += "[/" + String(current_list == LIST_UL ? "ul" : "ol") + "]\n";
		}
		if (in_table) {
			s += "[/table]\n";
		}
	}

	// ---------- Step 5: Restore inline code ----------
	for (int j = 0; j < inline_codes.size(); j++) {
		String placeholder = "[INLINECODE_" + vformat("%d", j) + "]";
		s = s.replace(placeholder, "[code]" + inline_codes[j] + "[/code]");
	}

	// ---------- Step 6: Restore code blocks ----------
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
		} else if (is_summary) {
			// Summary bubble: warm amber/yellow tone.
			Color summary_bg(0.5f, 0.45f, 0.25f, 0.25f);
			bubble_style->set_bg_color(summary_bg);
			bubble_style->set_border_width_all(1);
			bubble_style->set_border_color(Color(0.6f, 0.55f, 0.3f, 0.5f));
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
	author_label->set_text(is_summary ? TTR("Summary") : (is_user ? TTR("You") : TTR("AI")));
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

void AIChatMessage::setup_summary(const String &p_content) {
	is_user = false;
	is_summary = true;
	message_content = p_content;
	think_content = String();
	think_time_seconds = 0.0;
	prompt_tokens = 0;
	completion_tokens = 0;

	// Prepend a marker prefix to identify summary content.
	content_label->set_text("[b]Conversation Summary[/b]\n" + p_content);

	// Summary messages: hide thinking, hide edit button, hide token stats.
	think_container->set_visible(false);
	token_label->set_visible(false);

	if (edit_button && edit_button->is_inside_tree()) {
		footer->remove_child(edit_button);
	}

	// Align to left (like AI messages).
	Object::cast_to<Control>(alignment_box->get_child(0))->set_visible(false); // left_spacer
	Object::cast_to<Control>(alignment_box->get_child(alignment_box->get_child_count() - 1))->set_visible(true); // right_spacer

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
