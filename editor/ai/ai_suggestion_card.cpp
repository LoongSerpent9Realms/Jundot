/*  ai_suggestion_card.cpp                                                 */
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

#include "ai_suggestion_card.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"

void AISuggestionCard::_bind_methods() {
	ADD_SIGNAL(MethodInfo("suggestion_accepted"));
	ADD_SIGNAL(MethodInfo("suggestion_rejected"));
}

void AISuggestionCard::_notification(int p_what) {
	if (p_what == NOTIFICATION_TRANSLATION_CHANGED) {
		_update_translations();
	}
}

void AISuggestionCard::_toggle_expand() {
	expanded = !expanded;
	detail_container->set_visible(expanded);
	expand_button->set_text(expanded ? TTR("Less") : TTR("More"));
}

void AISuggestionCard::_accept_pressed() {
	emit_signal(SNAME("suggestion_accepted"));
}

void AISuggestionCard::_reject_pressed() {
	emit_signal(SNAME("suggestion_rejected"));
}

String _get_type_icon_text(AISuggestion::Type p_type) {
	switch (p_type) {
		case AISuggestion::TYPE_SKILL:
			return TTR("S");
		case AISuggestion::TYPE_MCP_SERVER:
			return TTR("MCP");
		case AISuggestion::TYPE_MEMORY:
			return TTR("M");
	}
	return String();
}

String _get_type_label_text(AISuggestion::Type p_type) {
	switch (p_type) {
		case AISuggestion::TYPE_SKILL:
			return TTR("Skill");
		case AISuggestion::TYPE_MCP_SERVER:
			return TTR("MCP Server");
		case AISuggestion::TYPE_MEMORY:
			return TTR("Memory");
	}
	return String();
}

String _get_suggestion_name(const AISuggestion &p_s) {
	switch (p_s.type) {
		case AISuggestion::TYPE_SKILL:
			return p_s.skill.name;
		case AISuggestion::TYPE_MCP_SERVER:
			return p_s.mcp_server.name;
		case AISuggestion::TYPE_MEMORY:
			return p_s.memory.title;
	}
	return String();
}

String _get_suggestion_description(const AISuggestion &p_s) {
	switch (p_s.type) {
		case AISuggestion::TYPE_SKILL:
			return p_s.skill.description;
		case AISuggestion::TYPE_MCP_SERVER:
			return p_s.mcp_server.command.is_empty() ? p_s.mcp_server.url : p_s.mcp_server.command;
		case AISuggestion::TYPE_MEMORY:
			return p_s.memory.content.left(120);
	}
	return String();
}

String _build_detail_text(const AISuggestion &p_s) {
	String detail;
	switch (p_s.type) {
		case AISuggestion::TYPE_SKILL: {
			detail += TTR("Name:") + " " + p_s.skill.name + "\n";
			detail += TTR("Description:") + " " + p_s.skill.description + "\n";
			detail += TTR("Permission:") + " " + p_s.skill.permission_level + "\n";
			detail += TTR("Writes:") + " " + String(p_s.skill.writes ? TTR("yes") : TTR("no")) + "\n";
			detail += TTR("Confirmation:") + " " + String(p_s.skill.requires_confirmation ? TTR("required") : TTR("not required")) + "\n";
			if (!p_s.skill.prompt_text.is_empty()) {
				detail += TTR("Prompt:") + " " + p_s.skill.prompt_text + "\n";
			}
		} break;
		case AISuggestion::TYPE_MCP_SERVER: {
			detail += TTR("Name:") + " " + p_s.mcp_server.name + "\n";
			if (!p_s.mcp_server.command.is_empty()) {
				detail += TTR("Command:") + " " + p_s.mcp_server.command + "\n";
			}
			if (!p_s.mcp_server.arguments.is_empty()) {
				detail += TTR("Args:") + " " + p_s.mcp_server.arguments + "\n";
			}
			if (!p_s.mcp_server.url.is_empty()) {
				detail += TTR("URL:") + " " + p_s.mcp_server.url + "\n";
			}
			detail += TTR("Writes:") + " " + String(p_s.mcp_server.writes ? TTR("yes") : TTR("no")) + "\n";
			detail += TTR("Confirmation:") + " " + String(p_s.mcp_server.requires_confirmation ? TTR("required") : TTR("not required")) + "\n";
			if (!p_s.mcp_server.capabilities_json.is_empty()) {
				detail += TTR("Capabilities:") + " " + p_s.mcp_server.capabilities_json + "\n";
			}
		} break;
		case AISuggestion::TYPE_MEMORY: {
			detail += TTR("Title:") + " " + p_s.memory.title + "\n";
			if (!p_s.memory.tags.is_empty()) {
				String tags;
				for (int i = 0; i < p_s.memory.tags.size(); i++) {
					if (i > 0) {
						tags += ", ";
					}
					tags += p_s.memory.tags[i];
				}
				detail += TTR("Tags:") + " " + tags + "\n";
			}
			detail += TTR("Content:") + " " + p_s.memory.content + "\n";
		} break;
	}
	return detail;
}

void AISuggestionCard::_build_ui() {
	VBoxContainer *vbox = memnew(VBoxContainer);
	vbox->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	vbox->add_theme_constant_override("separation", 4 * EDSCALE);
	add_child(vbox);

	header = memnew(HBoxContainer);
	header->add_theme_constant_override("separation", 6 * EDSCALE);
	vbox->add_child(header);

	type_icon = memnew(Label);
	type_icon->set_custom_minimum_size(Size2(36 * EDSCALE, 0));
	type_icon->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	type_icon->set_theme_type_variation("HeaderSmall");
	header->add_child(type_icon);

	name_label = memnew(Label);
	name_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	name_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	header->add_child(name_label);

	type_label = memnew(Label);
	type_label->set_custom_minimum_size(Size2(80 * EDSCALE, 0));
	type_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	header->add_child(type_label);

	expand_button = memnew(Button);
	expand_button->connect(SceneStringName(pressed), callable_mp(this, &AISuggestionCard::_toggle_expand));
	header->add_child(expand_button);

	accept_button = memnew(Button);
	accept_button->connect(SceneStringName(pressed), callable_mp(this, &AISuggestionCard::_accept_pressed));
	header->add_child(accept_button);

	reject_button = memnew(Button);
	reject_button->connect(SceneStringName(pressed), callable_mp(this, &AISuggestionCard::_reject_pressed));
	header->add_child(reject_button);

	detail_container = memnew(VBoxContainer);
	detail_container->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	detail_container->set_visible(false);
	vbox->add_child(detail_container);

	detail_label = memnew(Label);
	detail_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	detail_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	detail_container->add_child(detail_label);
}

void AISuggestionCard::_update_translations() {
	accept_button->set_text(TTR("Add"));
	reject_button->set_text(TTR("Dismiss"));
	expand_button->set_text(expanded ? TTR("Less") : TTR("More"));
	type_icon->set_text(_get_type_icon_text(suggestion.type));
	type_label->set_text(_get_type_label_text(suggestion.type));
	name_label->set_text(_get_suggestion_name(suggestion));
	detail_label->set_text(_build_detail_text(suggestion));
}

void AISuggestionCard::setup(const AISuggestion &p_suggestion) {
	suggestion = p_suggestion;
	_update_translations();
}

AISuggestion AISuggestionCard::get_suggestion() const {
	return suggestion;
}

AISuggestionCard::AISuggestionCard() {
	set_h_size_flags(Control::SIZE_EXPAND_FILL);
	add_theme_constant_override("margin_left", 8 * EDSCALE);
	add_theme_constant_override("margin_top", 4 * EDSCALE);
	add_theme_constant_override("margin_right", 8 * EDSCALE);
	add_theme_constant_override("margin_bottom", 4 * EDSCALE);

	_build_ui();
}
