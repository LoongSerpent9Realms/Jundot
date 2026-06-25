/*  ai_chat_panel.cpp                                                      */
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

#include "ai_chat_panel.h"

#include "ai_build_bridge.h"
#include "ai_chat_message.h"
#include "ai_code_fetcher.h"
#include "ai_new_build_notifier.h"
#include "ai_patch_applier.h"
#include "ai_restart_helper.h"
#include "ai_chat_service.h"
#include "ai_context_builder.h"
#include "ai_importer.h"
#include "ai_memory_store.h"
#include "ai_settings.h"
#include "ai_repair_card.h"
#include "ai_repair_workflow.h"
#include "ai_suggestion_card.h"
#include "ai_tool_confirmation_dialog.h"
#include "ai_tool_defs.h"
#include "ai_tool_executor.h"
#include "ai_tool_registry.h"
#include "ai_usage_agreement_dialog.h"
#include "ai_feature_gate.h"

#include "core/io/file_access.h"
#include "core/io/dir_access.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "editor/docks/editor_dock.h"
#include "editor/file_system/editor_paths.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/text_edit.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/main/http_request.h"
#include "scene/main/timer.h"
#include "scene/resources/style_box_flat.h"

static constexpr int AI_CHAT_ATTACHMENT_MAX_BYTES = 64 * 1024;

static String _ai_chat_next_question_protocol() {
	return String("=== Next Question Options Protocol ===\n"
			"- REQUIRED: At the end of every final assistant response, suggest 2 to 4 likely next questions the user may want to ask. Never omit them.\n"
			"- Put each question in a hidden machine-readable block so the editor can show it as a clickable option:\n"
			"<!-- NEXT_QUESTION -->\nQUESTION: <one concise user question>\n<!-- END_NEXT_QUESTION -->\n"
			"- Keep each question specific to the current conversation and useful as the user's next message.\n"
			"- Do not mention these hidden blocks in the visible response.");
}

static Ref<StyleBoxFlat> _ai_chat_make_panel_style(const Color &p_bg, const Color &p_border, float p_radius = 8.0f, float p_margin = 8.0f) {
	Ref<StyleBoxFlat> style;
	style.instantiate();
	style->set_bg_color(p_bg);
	style->set_corner_radius_all(p_radius * EDSCALE);
	style->set_border_width_all(1);
	style->set_border_color(p_border);
	style->set_content_margin_all(p_margin * EDSCALE);
	return style;
}

static Ref<StyleBoxFlat> _ai_chat_make_button_style(const Color &p_bg, const Color &p_border, float p_radius = 8.0f) {
	Ref<StyleBoxFlat> style;
	style.instantiate();
	style->set_bg_color(p_bg);
	style->set_corner_radius_all(p_radius * EDSCALE);
	style->set_border_width_all(1);
	style->set_border_color(p_border);
	style->set_content_margin(SIDE_LEFT, 12 * EDSCALE);
	style->set_content_margin(SIDE_RIGHT, 12 * EDSCALE);
	style->set_content_margin(SIDE_TOP, 7 * EDSCALE);
	style->set_content_margin(SIDE_BOTTOM, 7 * EDSCALE);
	return style;
}

void AIChatPanel::_bind_methods() {
}

// ==========================================================================
// Multi-conversation support
// ==========================================================================

Dictionary AIChatPanel::Conversation::to_dict(const Conversation &p_conv) {
	Dictionary d;
	d["id"] = p_conv.id;
	d["title"] = p_conv.title;
	d["context_mode"] = p_conv.context_mode == AIContextMode::ENGINE ? "engine" : "project";
	d["created_at"] = (int64_t)p_conv.created_at;
	d["updated_at"] = (int64_t)p_conv.updated_at;
	d["tool_limit_options_available"] = p_conv.tool_limit_options_available;
	d["tool_limit_options_collapsed"] = p_conv.tool_limit_options_collapsed;
	d["tool_limit_options_due_to_limit"] = p_conv.tool_limit_options_due_to_limit;
	Array next_questions;
	for (int i = 0; i < p_conv.next_question_options.size(); i++) {
		next_questions.push_back(p_conv.next_question_options[i]);
	}
	d["next_question_options"] = next_questions;
	d["structured_messages"] = p_conv.structured_messages;

	Array msgs;
	for (int i = 0; i < p_conv.messages.size(); i++) {
		const ConversationMessage &m = p_conv.messages[i];
		Dictionary md;
		md["is_user"] = m.is_user;
		md["is_summary"] = m.is_summary;
		md["content"] = m.content;
		md["think_content"] = m.think_content;
		md["think_time"] = m.think_time_seconds;
		md["prompt_tokens"] = m.prompt_tokens;
		md["completion_tokens"] = m.completion_tokens;
		msgs.push_back(md);
	}
	d["messages"] = msgs;
	return d;
}

AIChatPanel::Conversation AIChatPanel::Conversation::from_dict(const Dictionary &p_dict) {
	Conversation conv;
	conv.id = p_dict.get("id", String());
	conv.title = p_dict.get("title", TTR("Untitled Chat"));
	conv.context_mode = String(p_dict.get("context_mode", "project")) == "engine" ? AIContextMode::ENGINE : AIContextMode::PROJECT;
	conv.created_at = (uint64_t)p_dict.get("created_at", 0);
	conv.updated_at = (uint64_t)p_dict.get("updated_at", 0);
	conv.tool_limit_options_available = p_dict.get("tool_limit_options_available", false);
	conv.tool_limit_options_collapsed = p_dict.get("tool_limit_options_collapsed", false);
	conv.tool_limit_options_due_to_limit = p_dict.get("tool_limit_options_due_to_limit", false);
	Variant structured_messages_var = p_dict.get("structured_messages", Array());
	if (structured_messages_var.get_type() == Variant::ARRAY) {
		conv.structured_messages = ((Array)structured_messages_var).duplicate(true);
	}
	Array next_questions = p_dict.get("next_question_options", Array());
	for (int i = 0; i < next_questions.size(); i++) {
		const String question = String(next_questions[i]).strip_edges();
		if (!question.is_empty()) {
			conv.next_question_options.push_back(question);
		}
	}

	Array msgs = p_dict.get("messages", Array());
	for (int i = 0; i < msgs.size(); i++) {
		Variant md_var = msgs[i];
		if (md_var.get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary md = md_var;
		ConversationMessage m;
		m.is_user = md.get("is_user", false);
		m.is_summary = md.get("is_summary", false);
		m.content = md.get("content", String());
		m.think_content = md.get("think_content", String());
		m.think_time_seconds = md.get("think_time", 0.0);
		m.prompt_tokens = md.get("prompt_tokens", 0);
		m.completion_tokens = md.get("completion_tokens", 0);
		conv.messages.push_back(m);
	}
	return conv;
}

String AIChatPanel::_get_conversations_file_path() const {
	if (!EditorPaths::get_singleton()) {
		return String();
	}
	return EditorPaths::get_singleton()->get_config_dir().path_join("ai_conversations.json");
}

String AIChatPanel::_generate_conversation_id() const {
	uint64_t t = Time::get_singleton()->get_unix_time_from_system();
	uint64_t r = (uint64_t)OS::get_singleton()->get_unix_time() ^ (uint64_t)OS::get_singleton()->get_process_id();
	return vformat("conv_%x_%x", (uint64_t)t, (uint64_t)r);
}

String AIChatPanel::_auto_generate_title(const Conversation &p_conv) const {
	for (int i = 0; i < p_conv.messages.size(); i++) {
		const ConversationMessage &m = p_conv.messages[i];
		if (m.is_user && !m.content.is_empty()) {
			String title = m.content.strip_edges();
			if (title.length() > 60) {
				title = title.substr(0, 57) + "...";
			}
			return title;
		}
	}
	return TTR("New Chat");
}

void AIChatPanel::_refresh_conversation_list_ui() {
	if (!conversation_list) {
		return;
	}
	conversation_list->clear();
	for (int i = 0; i < conversations.size(); i++) {
		String title = conversations[i].title;
		if (title.is_empty()) {
			title = TTR("New Chat");
		}
		const bool engine_mode = conversations[i].context_mode == AIContextMode::ENGINE;
		const String mode_label = engine_mode ? TTR("Engine") : TTR("Project");
		conversation_list->add_item(vformat("[%s] %s", mode_label, title));
		conversation_list->set_item_tooltip(i, vformat(TTR("%s mode conversation"), mode_label));
		if (conversations[i].id == active_conversation_id) {
			conversation_list->select(i);
		}
	}
}

void AIChatPanel::_serialize_current_messages() {
	if (active_conversation_id.is_empty()) {
		return;
	}

	int conv_idx = -1;
	for (int i = 0; i < conversations.size(); i++) {
		if (conversations[i].id == active_conversation_id) {
			conv_idx = i;
			break;
		}
	}
	if (conv_idx < 0) {
		return;
	}

	Conversation &conv = conversations.write[conv_idx];
	conv.messages.clear();
	if (tool_limit_options_panel && tool_limit_toggle_button) {
		conv.tool_limit_options_available = tool_limit_options_panel->is_visible() || tool_limit_toggle_button->is_visible();
		conv.tool_limit_options_collapsed = tool_limit_toggle_button->is_visible();
		conv.tool_limit_options_due_to_limit = tool_limit_options_due_to_limit;
		conv.next_question_options = next_question_options;
	}

	for (int i = 0; i < message_list->get_child_count(); i++) {
		// Skip suggestion cards and repair cards.
		AIChatMessage *msg = Object::cast_to<AIChatMessage>(message_list->get_child(i));
		if (!msg) {
			continue;
		}

		ConversationMessage cm;
		cm.is_user = msg->is_user_message();
		cm.is_summary = msg->is_summary_message();
		cm.content = msg->get_content();
		cm.think_content = msg->get_think_content();
		cm.think_time_seconds = msg->get_think_time_seconds();
		conv.messages.push_back(cm);
	}

	conv.updated_at = Time::get_singleton()->get_unix_time_from_system();

	// Auto-update title based on the first user message if still default.
	static const String default_title = TTR("New Chat");
	if (conv.title.is_empty() || conv.title == default_title) {
		conv.title = _auto_generate_title(conv);
	}
}

Array AIChatPanel::_get_structured_history() const {
	if (active_conversation_id.is_empty()) {
		return Array();
	}
	for (int i = 0; i < conversations.size(); i++) {
		if (conversations[i].id == active_conversation_id) {
			return conversations[i].structured_messages.duplicate(true);
		}
	}
	return Array();
}

void AIChatPanel::_store_structured_history(const Array &p_messages, const String &p_assistant_content) {
	if (active_conversation_id.is_empty()) {
		return;
	}

	Array stored_messages;
	for (int i = 0; i < p_messages.size(); i++) {
		if (p_messages[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary message = p_messages[i];
		if (String(message.get("role", String())) == "system") {
			continue;
		}
		stored_messages.push_back(message.duplicate(true));
	}

	if (!p_assistant_content.strip_edges().is_empty()) {
		Dictionary assistant_message;
		assistant_message["role"] = "assistant";
		assistant_message["content"] = p_assistant_content;
		stored_messages.push_back(assistant_message);
	}

	for (int i = 0; i < conversations.size(); i++) {
		if (conversations[i].id == active_conversation_id) {
			conversations.write[i].structured_messages = stored_messages;
			conversations.write[i].updated_at = Time::get_singleton()->get_unix_time_from_system();
			break;
		}
	}
}

void AIChatPanel::_clear_structured_history() {
	if (active_conversation_id.is_empty()) {
		return;
	}
	for (int i = 0; i < conversations.size(); i++) {
		if (conversations[i].id == active_conversation_id) {
			conversations.write[i].structured_messages.clear();
			break;
		}
	}
}

void AIChatPanel::_load_conversation_to_ui(const Conversation &p_conv) {
	_stop_build_status_poll();

	AISettingsData settings = AISettings::load();
	if (settings.context_mode != p_conv.context_mode) {
		settings.context_mode = p_conv.context_mode;
		AISettings::save(settings);
	}
	_update_mode_indicator();

	// Clear existing UI messages.
	for (int i = message_list->get_child_count() - 1; i >= 0; i--) {
		message_list->get_child(i)->queue_free();
	}

	// Clear suggestion / repair cards.
	suggestion_cards.clear();
	repair_cards.clear();
	_refresh_bulk_bar();

	// Reset tool loop and editing state.
	pending_tool_round = PendingToolRound();
	in_tool_loop = false;
	editing_message_index = -1;
	has_auto_titled = !p_conv.messages.is_empty();
	attachments.clear();
	_refresh_attachment_chips();
	input->clear();
	next_question_options = p_conv.next_question_options;
	_render_next_question_options();

	// Re-populate messages.
	for (int i = 0; i < p_conv.messages.size(); i++) {
		const ConversationMessage &cm = p_conv.messages[i];
		if (cm.is_summary) {
			_add_summary_message(cm.content);
		} else if (cm.is_user) {
			_add_user_message(cm.content);
		} else {
			_add_ai_message(cm.content, cm.think_content, cm.think_time_seconds, cm.prompt_tokens, cm.completion_tokens);
		}
	}

	_apply_tool_limit_options_state(p_conv);
	_update_auto_chat_display_scale();
}

void AIChatPanel::_select_conversation(const String &p_id) {
	if (chat_service && chat_service->is_requesting()) {
		status_label->set_text(TTR("Wait for the current AI response before switching conversations."));
		return;
	}

	// Save the active conversation first.
	_serialize_current_messages();

	// If the target conversation doesn't exist, do nothing.
	if (p_id.is_empty()) {
		return;
	}
	int idx = -1;
	for (int i = 0; i < conversations.size(); i++) {
		if (conversations[i].id == p_id) {
			idx = i;
			break;
		}
	}
	if (idx < 0) {
		return;
	}

	active_conversation_id = p_id;
	_load_conversation_to_ui(conversations[idx]);
	_refresh_conversation_list_ui();

	// Persist the change so that restart opens on the same conversation.
	_save_all_conversations();

	status_label->set_text(vformat(TTR("Switched to: %s"), conversations[idx].title));
}

void AIChatPanel::_conversation_selected(int p_index) {
	if (p_index < 0 || p_index >= conversations.size()) {
		return;
	}
	// Avoid bouncing off the same conversation (e.g. when the user clicks the
	// already selected item).
	if (conversations[p_index].id == active_conversation_id) {
		return;
	}
	_select_conversation(conversations[p_index].id);
}

void AIChatPanel::_new_conversation() {
	if (chat_service && chat_service->is_requesting()) {
		status_label->set_text(TTR("Wait for the current AI response before starting a new chat."));
		return;
	}

	// Serialize current conversation before switching.
	_serialize_current_messages();

	// Create a fresh conversation.
	Conversation conv;
	conv.id = _generate_conversation_id();
	conv.title = TTR("New Chat");
	conv.context_mode = AISettings::load().context_mode;
	conv.created_at = Time::get_singleton()->get_unix_time_from_system();
	conv.updated_at = conv.created_at;

	conversations.insert(0, conv);
	active_conversation_id = conv.id;

	_load_conversation_to_ui(conv);
	_refresh_conversation_list_ui();
	_save_all_conversations();

	status_label->set_text(TTR("Started a new chat."));
}

void AIChatPanel::_delete_current_conversation() {
	if (chat_service && chat_service->is_requesting()) {
		status_label->set_text(TTR("Wait for the current AI response before deleting conversations."));
		return;
	}

	if (active_conversation_id.is_empty()) {
		return;
	}

	int idx = -1;
	for (int i = 0; i < conversations.size(); i++) {
		if (conversations[i].id == active_conversation_id) {
			idx = i;
			break;
		}
	}
	if (idx < 0) {
		return;
	}

	conversations.remove_at(idx);

	// Pick the next conversation to show.
	if (conversations.size() > 0) {
		int next_idx = idx > 0 ? idx - 1 : 0;
		_select_conversation(conversations[next_idx].id);
	} else {
		// No conversation left �?create a default one.
		_new_conversation();
	}
}

void AIChatPanel::_save_all_conversations() const {
	// Make sure the currently active conversation is up-to-date before saving.
	// (This is a const method that serialises by working on a cached copy;
	// we instead rely on callers invoking _serialize_current_messages first.)

	String path = _get_conversations_file_path();
	if (path.is_empty()) {
		return;
	}

	Error err = DirAccess::make_dir_recursive_absolute(path.get_base_dir());
	if (err != OK && err != ERR_ALREADY_EXISTS) {
		return;
	}

	Dictionary root;
	root["version"] = 1;
	root["active_conversation_id"] = active_conversation_id;

	Array arr;
	for (int i = 0; i < conversations.size(); i++) {
		arr.push_back(Conversation::to_dict(conversations[i]));
	}
	root["conversations"] = arr;

	Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
	if (f.is_null()) {
		return;
	}
	f->store_string(JSON::stringify(root, "\t"));
}

void AIChatPanel::_load_all_conversations() {
	conversations.clear();
	active_conversation_id = String();

	String path = _get_conversations_file_path();
	if (path.is_empty() || !FileAccess::exists(path)) {
		// No saved conversations �?create an initial empty conversation.
		_new_conversation();
		return;
	}

	Error err = OK;
	String content = FileAccess::get_file_as_string(path, &err);
	if (err != OK || content.is_empty()) {
		_new_conversation();
		return;
	}

	JSON json;
	err = json.parse(content);
	if (err != OK) {
		_new_conversation();
		return;
	}

	Variant json_data = json.get_data();
	if (json_data.get_type() != Variant::DICTIONARY) {
		_new_conversation();
		return;
	}

	Dictionary root = json_data;
	active_conversation_id = root.get("active_conversation_id", String());

	Array arr = root.get("conversations", Array());
	for (int i = 0; i < arr.size(); i++) {
		Variant d_var = arr[i];
		if (d_var.get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary d = d_var;
		Conversation conv = Conversation::from_dict(d);
		if (!d.has("context_mode")) {
			// Legacy conversations predate per-conversation mode persistence.
			// Keep them in the mode that was active when the editor loaded.
			conv.context_mode = AISettings::load().context_mode;
		}
		if (!conv.id.is_empty()) {
			conversations.push_back(conv);
		}
	}

	// Sort by updated_at descending so most recent is at the top.
	for (int i = 0; i < conversations.size() - 1; i++) {
		for (int j = i + 1; j < conversations.size(); j++) {
			if (conversations[j].updated_at > conversations[i].updated_at) {
				SWAP(conversations.write[i], conversations.write[j]);
			}
		}
	}

	// Ensure there is at least one conversation and a valid active one.
	if (conversations.is_empty()) {
		_new_conversation();
		return;
	}

	bool valid = false;
	for (int i = 0; i < conversations.size(); i++) {
		if (conversations[i].id == active_conversation_id) {
			valid = true;
			break;
		}
	}
	if (!valid) {
		active_conversation_id = conversations[0].id;
	}
}

// ==========================================================================
// End multi-conversation support
// ==========================================================================

void AIChatPanel::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		callable_mp(this, &AIChatPanel::_update_mode_indicator).call_deferred();
		Window *root_window = get_tree() ? get_tree()->get_root() : nullptr;
		if (root_window && !root_window->is_connected(SNAME("files_dropped"), callable_mp(this, &AIChatPanel::_window_files_dropped))) {
			root_window->connect(SNAME("files_dropped"), callable_mp(this, &AIChatPanel::_window_files_dropped));
		}
	}
	if (p_what == NOTIFICATION_TRANSLATION_CHANGED) {
		_update_translations();
	}
	if (p_what == NOTIFICATION_RESIZED) {
		_update_auto_chat_display_scale();
	}
	if (p_what == NOTIFICATION_THEME_CHANGED) {
		Color base = get_theme_color(SNAME("base_color"), SNAME("Editor"));
		Color dark = get_theme_color(SNAME("dark_color_1"), SNAME("Editor"));
		Color accent = get_theme_color(SNAME("accent_color"), SNAME("Editor"));
		Color font = get_theme_color(SNAME("font_color"), SNAME("Editor"));
		Color muted_font = font * Color(1, 1, 1, 0.62f);
		Color sidebar_bg = dark.darkened(0.14f);
		Color surface_bg = base.darkened(0.055f);
		Color composer_bg = base.lightened(0.025f);
		Color subtle_border = base.lightened(0.10f);

		if (sidebar_panel) {
			sidebar_panel->add_theme_style_override(SNAME("panel"), _ai_chat_make_panel_style(sidebar_bg, subtle_border, 0.0f, 12.0f));
		}
		if (chat_surface_panel) {
			chat_surface_panel->add_theme_style_override(SNAME("panel"), _ai_chat_make_panel_style(surface_bg, surface_bg, 0.0f, 0.0f));
		}
		if (composer_panel) {
			composer_panel->add_theme_style_override(SNAME("panel"), _ai_chat_make_panel_style(composer_bg, subtle_border, 14.0f, 12.0f));
		}
		if (input) {
			Ref<StyleBoxFlat> input_style = _ai_chat_make_panel_style(composer_bg, composer_bg, 10.0f, 8.0f);
			input->add_theme_style_override(CoreStringName(normal), input_style);
			input->add_theme_color_override(SNAME("font_color"), font);
		}
		if (conversation_list) {
			conversation_list->add_theme_color_override(SNAME("font_color"), muted_font);
			conversation_list->add_theme_color_override(SNAME("font_selected_color"), font);
			conversation_list->add_theme_color_override(SNAME("guide_color"), Color(0, 0, 0, 0));
		}
		if (new_conversation_button) {
			Ref<StyleBoxFlat> normal = _ai_chat_make_button_style(Color(0, 0, 0, 0), subtle_border, 8.0f);
			Ref<StyleBoxFlat> hover = _ai_chat_make_button_style(base.lightened(0.035f), subtle_border, 8.0f);
			new_conversation_button->add_theme_style_override(SNAME("normal"), normal);
			new_conversation_button->add_theme_style_override(SNAME("hover"), hover);
			new_conversation_button->add_theme_color_override(SNAME("font_color"), font);
		}
		if (delete_conversation_button) {
			delete_conversation_button->add_theme_color_override(SNAME("font_color"), muted_font);
		}
		if (send_button) {
			Ref<StyleBoxFlat> send_normal = _ai_chat_make_button_style(accent, accent, 10.0f);
			Ref<StyleBoxFlat> send_hover = _ai_chat_make_button_style(accent.lightened(0.08f), accent.lightened(0.08f), 10.0f);
			send_button->add_theme_style_override(SNAME("normal"), send_normal);
			send_button->add_theme_style_override(SNAME("hover"), send_hover);
			send_button->add_theme_color_override(SNAME("font_color"), Color(1, 1, 1));
		}
		if (add_file_menu) {
			add_file_menu->add_theme_color_override(SNAME("font_color"), muted_font);
		}
		if (clear_button) {
			clear_button->add_theme_color_override(SNAME("font_color"), muted_font);
		}
		if (cancel_button) {
			cancel_button->add_theme_color_override(SNAME("font_color"), muted_font);
		}
		if (status_label) {
			status_label->add_theme_color_override(SNAME("font_color"), muted_font);
		}
		_update_auto_chat_display_scale();
	}
	if (p_what == NOTIFICATION_PREDELETE) {
		Window *root_window = get_tree() ? get_tree()->get_root() : nullptr;
		if (root_window && root_window->is_connected(SNAME("files_dropped"), callable_mp(this, &AIChatPanel::_window_files_dropped))) {
			root_window->disconnect(SNAME("files_dropped"), callable_mp(this, &AIChatPanel::_window_files_dropped));
		}
		if (tool_execution_thread.is_started()) {
			tool_execution_thread.wait_to_finish();
		}
	}
}

Array AIChatPanel::_build_message_history() const {
	Array history;
	for (int i = 0; i < message_list->get_child_count(); i++) {
		AIChatMessage *msg = Object::cast_to<AIChatMessage>(message_list->get_child(i));
		if (!msg) {
			continue;
		}
		Dictionary entry;
		entry["role"] = msg->is_user_message() ? "user" : "assistant";
		entry["content"] = msg->get_content();
		history.push_back(entry);
	}
	return history;
}

String AIChatPanel::_detect_mode_prompt(const String &p_user_message) const {
	// Auto-detect the most appropriate mode based on user input keywords.
	String msg = p_user_message.to_lower();

	// Defect-related keywords.
	static const char *defect_keywords[] = {
		"error", "bug", "crash", "issue", "problem", "fail", "broken", "exception",
		"leak", "slow", "perf", "BUG", nullptr
	};

	// Feature-related keywords.
	static const char *feature_keywords[] = {
		"feature", "add", "implement", "support", "new", "create", "build", nullptr
	};

	int defect_score = 0;
	int feature_score = 0;

	for (const char **kw = defect_keywords; *kw != nullptr; kw++) {
		if (msg.contains(*kw)) {
			defect_score++;
		}
	}
	for (const char **kw = feature_keywords; *kw != nullptr; kw++) {
		if (msg.contains(*kw)) {
			feature_score++;
		}
	}

	if (defect_score > 0 && defect_score >= feature_score) {
		return TTR("Mode: Defect discovery. Prioritize engine logs, crash reports, code patterns, root causes, and performance bottlenecks.");
	} else if (feature_score > 0 && feature_score > defect_score) {
		return TTR("Mode: Feature evaluation. Judge whether a requested feature is generally useful and necessary before expanding scope.");
	}
	// Default / hybrid.
	return TTR("Mode: Hybrid assistant. Help the developer by analyzing context, proposing changes, and collaborating on solutions.");
}

void AIChatPanel::_switch_to_engine() {
	AISettingsData s = AISettings::load();
	s.context_mode = AIContextMode::ENGINE;
	AISettings::save(s);
	for (int i = 0; i < conversations.size(); i++) {
		if (conversations[i].id == active_conversation_id) {
			conversations.write[i].context_mode = AIContextMode::ENGINE;
			conversations.write[i].updated_at = Time::get_singleton()->get_unix_time_from_system();
			break;
		}
	}
	_update_mode_indicator();
	_refresh_conversation_list_ui();
	_save_all_conversations();
	status_label->set_text(TTR("Switched to ENGINE mode: engine source context."));
}

void AIChatPanel::_switch_to_project() {
	AISettingsData s = AISettings::load();
	s.context_mode = AIContextMode::PROJECT;
	AISettings::save(s);
	for (int i = 0; i < conversations.size(); i++) {
		if (conversations[i].id == active_conversation_id) {
			conversations.write[i].context_mode = AIContextMode::PROJECT;
			conversations.write[i].updated_at = Time::get_singleton()->get_unix_time_from_system();
			break;
		}
	}
	_update_mode_indicator();
	_refresh_conversation_list_ui();
	_save_all_conversations();
	status_label->set_text(TTR("Switched to PROJECT mode: game project context."));
}

void AIChatPanel::_update_mode_indicator() {
	if (!engine_mode_btn || !project_mode_btn) {
		return;
	}
	if (!engine_mode_btn->is_inside_tree() || !project_mode_btn->is_inside_tree()) {
		return;
	}

	AISettingsData s = AISettings::load();
	if (s.context_mode == AIContextMode::ENGINE) {
		engine_mode_btn->add_theme_color_override("font_color", Color(1.0f, 0.4f, 0.4f));
		project_mode_btn->add_theme_color_override("font_color", Color(0.7f, 0.7f, 0.7f));
	} else {
		project_mode_btn->add_theme_color_override("font_color", Color(0.4f, 1.0f, 0.4f));
		engine_mode_btn->add_theme_color_override("font_color", Color(0.7f, 0.7f, 0.7f));
	}
}

void AIChatPanel::_update_translations() {
	set_name(TTRC("Chat"));
	input->set_placeholder(TTR("Message Jundot AI..."));
	add_file_menu->set_text(TTR("+"));
	add_file_menu->set_tooltip_text(TTR("Attach or reference a text file"));
	PopupMenu *file_popup = add_file_menu->get_popup();
	file_popup->set_item_text(file_popup->get_item_index(FILE_MENU_REFERENCE_PROJECT), TTR("Reference Project File"));
	file_popup->set_item_text(file_popup->get_item_index(FILE_MENU_UPLOAD_TEXT), TTR("Upload Text File"));
	file_popup->set_item_text(file_popup->get_item_index(FILE_MENU_IMPORT), TTR("Import Skill / MCP / Memory..."));
	clear_button->set_text(TTR("Clear"));
	clear_button->set_tooltip_text(TTR("Clear input and attachments"));
	cancel_button->set_text(TTR("Cancel"));
	send_button->set_text(TTR("Send"));
	if (tool_limit_toggle_button) {
		tool_limit_toggle_button->set_text(TTR("^"));
		tool_limit_toggle_button->set_tooltip_text(TTR("Show next-step options"));
	}
	if (tool_limit_options_title) {
		if (tool_limit_options_due_to_limit) {
			tool_limit_options_title->set_text(TTR("The AI stopped because the tool call limit was reached. What would you like to do next?"));
		} else {
			tool_limit_options_title->set_text(TTR("The AI response has finished. What would you like to do next?"));
		}
	}
	if (tool_limit_continue_button) {
		tool_limit_continue_button->set_text(TTR("Continue"));
	}
	if (tool_limit_custom_button) {
		tool_limit_custom_button->set_text(TTR("Send Another Message"));
	}
	if (tool_limit_stop_button) {
		tool_limit_stop_button->set_text(TTR("Stop Here"));
	}
	if (tool_limit_collapse_button) {
		tool_limit_collapse_button->set_text(TTR("Collapse"));
	}
	reference_file_dialog->set_title(TTR("Reference Project File"));
	upload_file_dialog->set_title(TTR("Upload Text File"));
	import_file_dialog->set_title(TTR("Import Skill / MCP / Memory"));
	add_all_button->set_text(TTR("Add All"));
	dismiss_all_button->set_text(TTR("Dismiss All"));
	if (new_conversation_button) {
		new_conversation_button->set_text(TTR("+ New Chat"));
	}
	if (delete_conversation_button) {
		delete_conversation_button->set_text(TTR("Delete"));
	}
	_refresh_attachment_chips();
}

void AIChatPanel::_set_requesting(bool p_requesting) {
	send_button->set_disabled(p_requesting);
	cancel_button->set_disabled(!p_requesting);
	if (conversation_list) {
		conversation_list->set_mouse_filter(p_requesting ? Control::MOUSE_FILTER_IGNORE : Control::MOUSE_FILTER_STOP);
	}
	if (new_conversation_button) {
		new_conversation_button->set_disabled(p_requesting);
	}
	if (delete_conversation_button) {
		delete_conversation_button->set_disabled(p_requesting);
	}
}

bool AIChatPanel::_ensure_usage_agreement() {
	const AISettingsData settings = AISettings::load();
	if (AISettings::is_usage_agreement_current(settings)) {
		return true;
	}

	status_label->set_text(TTR("Please review and accept the AI usage agreement before sending messages."));
	usage_agreement_dialog->popup_centered(Size2(420, 220) * EDSCALE);
	return false;
}

void AIChatPanel::_usage_agreement_accepted() {
	status_label->set_text(TTR("AI usage agreement accepted. Send your message again to continue."));
}

void AIChatPanel::_usage_agreement_rejected() {
	status_label->set_text(TTR("AI message was not sent because the usage agreement was not accepted."));
}

void AIChatPanel::_show_tool_limit_options(bool p_due_to_limit) {
	if (!tool_limit_options_panel || !tool_limit_toggle_button) {
		return;
	}

	if (!p_due_to_limit && next_question_options.is_empty()) {
		_hide_tool_limit_options();
		return;
	}

	tool_limit_options_due_to_limit = p_due_to_limit;
	_update_translations();
	tool_limit_options_panel->set_visible(true);
	tool_limit_toggle_button->set_visible(false);
	_store_tool_limit_options_state();
	message_scroll->set_deferred(SNAME("scroll_vertical"), message_scroll->get_v_scroll_bar()->get_max());
}

void AIChatPanel::_hide_tool_limit_options() {
	if (tool_limit_options_panel) {
		tool_limit_options_panel->set_visible(false);
	}
	if (tool_limit_toggle_button) {
		tool_limit_toggle_button->set_visible(false);
	}
	_store_tool_limit_options_state();
}

void AIChatPanel::_set_tool_limit_options_collapsed(bool p_collapsed) {
	if (!tool_limit_options_panel || !tool_limit_toggle_button) {
		return;
	}

	tool_limit_options_panel->set_visible(!p_collapsed);
	tool_limit_toggle_button->set_visible(p_collapsed);
	_store_tool_limit_options_state();
	if (!p_collapsed) {
		message_scroll->set_deferred(SNAME("scroll_vertical"), message_scroll->get_v_scroll_bar()->get_max());
	}
}

void AIChatPanel::_store_tool_limit_options_state(bool p_save) {
	if (active_conversation_id.is_empty() || !tool_limit_options_panel || !tool_limit_toggle_button) {
		return;
	}

	for (int i = 0; i < conversations.size(); i++) {
		if (conversations[i].id == active_conversation_id) {
			conversations.write[i].tool_limit_options_available = tool_limit_options_panel->is_visible() || tool_limit_toggle_button->is_visible();
			conversations.write[i].tool_limit_options_collapsed = tool_limit_toggle_button->is_visible();
			conversations.write[i].tool_limit_options_due_to_limit = tool_limit_options_due_to_limit;
			conversations.write[i].next_question_options = next_question_options;
			conversations.write[i].updated_at = Time::get_singleton()->get_unix_time_from_system();
			if (p_save) {
				_save_all_conversations();
			}
			break;
		}
	}
}

void AIChatPanel::_apply_tool_limit_options_state(const Conversation &p_conv) {
	if (!tool_limit_options_panel || !tool_limit_toggle_button) {
		return;
	}

	next_question_options = p_conv.next_question_options;
	_render_next_question_options();
	tool_limit_options_due_to_limit = p_conv.tool_limit_options_due_to_limit;
	_update_translations();
	if (!p_conv.tool_limit_options_available) {
		tool_limit_options_panel->set_visible(false);
		tool_limit_toggle_button->set_visible(false);
		return;
	}

	tool_limit_options_panel->set_visible(!p_conv.tool_limit_options_collapsed);
	tool_limit_toggle_button->set_visible(p_conv.tool_limit_options_collapsed);
	if (!p_conv.tool_limit_options_collapsed) {
		message_scroll->set_deferred(SNAME("scroll_vertical"), message_scroll->get_v_scroll_bar()->get_max());
	}
}

void AIChatPanel::_set_next_question_options(const Vector<String> &p_questions, bool p_save) {
	next_question_options.clear();
	for (int i = 0; i < p_questions.size() && next_question_options.size() < 4; i++) {
		const String question = p_questions[i].strip_edges();
		if (!question.is_empty()) {
			next_question_options.push_back(question);
		}
	}
	_render_next_question_options();

	if (active_conversation_id.is_empty()) {
		return;
	}
	for (int i = 0; i < conversations.size(); i++) {
		if (conversations[i].id == active_conversation_id) {
			conversations.write[i].next_question_options = next_question_options;
			conversations.write[i].updated_at = Time::get_singleton()->get_unix_time_from_system();
			if (p_save) {
				_save_all_conversations();
			}
			break;
		}
	}
}

void AIChatPanel::_render_next_question_options() {
	if (!next_question_options_box) {
		return;
	}

	for (int i = next_question_options_box->get_child_count() - 1; i >= 0; i--) {
		next_question_options_box->get_child(i)->queue_free();
	}

	next_question_options_box->set_visible(!next_question_options.is_empty());
	for (int i = 0; i < next_question_options.size(); i++) {
		Button *question_button = memnew(Button);
		question_button->set_text(next_question_options[i]);
		question_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		question_button->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		question_button->set_text_overrun_behavior(TextServer::OVERRUN_NO_TRIMMING);
		question_button->set_custom_minimum_size(Size2(0, Math::round(38 * chat_display_scale)) * EDSCALE);
		question_button->add_theme_font_size_override(SceneStringName(font_size), Math::round(14 * chat_display_scale * EDSCALE));
		question_button->set_tooltip_text(next_question_options[i]);
		question_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_use_next_question_option).bind(next_question_options[i]));
		next_question_options_box->add_child(question_button);
	}
}

void AIChatPanel::_use_next_question_option(const String &p_question) {
	if (!input) {
		return;
	}
	input->set_text(p_question);
	input->grab_focus();
	_hide_tool_limit_options();
	_set_next_question_options(Vector<String>(), true);
}

void AIChatPanel::_send_hidden_followup(const String &p_instruction) {
	if (chat_service && chat_service->is_requesting()) {
		status_label->set_text(TTR("Wait for the current AI request to finish before continuing."));
		return;
	}

	const String instruction = p_instruction.strip_edges();
	if (instruction.is_empty()) {
		return;
	}

	AISettingsData settings = AISettings::load();
	if (settings.backend_type == AIBackendType::LEGACY_OPENAI && settings.api_key.is_empty()) {
		status_label->set_text(TTR("Configure an API key before sending AI messages."));
		return;
	}
	if (settings.backend_type == AIBackendType::JUNDOT_PLUGIN && (settings.jundot_ai_plugin_id.strip_edges().is_empty() || settings.jundot_ai_plugin_url.strip_edges().is_empty())) {
		status_label->set_text(TTR("Configure the jundot AI plugin before sending AI messages."));
		return;
	}
	if (!_ensure_usage_agreement()) {
		return;
	}

	_hide_tool_limit_options();
	request_conversation_id = active_conversation_id;

	String configured_system_prompt = AISettings::get_effective_system_prompt(settings);
	settings.system_prompt = configured_system_prompt;
	settings.system_prompt += "\n\n" + _ai_chat_next_question_protocol();
	const String ai_context = AIContextBuilder::build_context(settings.include_project_memories, settings.include_tool_context, settings.context_char_budget, settings.auto_suggest_entries);
	if (!ai_context.is_empty()) {
		settings.system_prompt += "\n\n" + ai_context;
	}

	Array messages;
	{
		Dictionary system_msg;
		system_msg["role"] = "system";
		system_msg["content"] = settings.system_prompt;
		messages.push_back(system_msg);
	}

	Array structured_history = _get_structured_history();
	if (!structured_history.is_empty()) {
		for (int i = 0; i < structured_history.size(); i++) {
			messages.push_back(structured_history[i]);
		}
	} else {
		Array history = _build_message_history();
		for (int i = 0; i < history.size(); i++) {
			messages.push_back(history[i]);
		}
	}

	Dictionary followup_msg;
	followup_msg["role"] = "user";
	followup_msg["content"] = instruction;
	messages.push_back(followup_msg);

	Array tools;
	if (settings.tools_enabled) {
		tools = AIToolDefs::get_tools_for_mode(settings.context_mode);
		if (settings.mcp_tools_enabled) {
			Array mcp_tools = AIToolDefs::get_mcp_tools();
			if (!mcp_tools.is_empty()) {
				tools.append_array(mcp_tools);
			}
		}
	}

	chat_service->configure(settings);
	active_settings = settings;
	pending_tool_round = PendingToolRound();
	pending_tool_round.max_iterations = settings.max_tool_iterations > 0 ? settings.max_tool_iterations : 10;
	pending_tool_round.original_messages = messages.duplicate(true);
	pending_tool_round.original_tools = tools.duplicate(true);
	in_tool_loop = false;
	_start_response_tracking();

	const Error err = chat_service->send_messages(messages, tools);
	if (err != OK) {
		_clear_response_tracking();
		status_label->set_text(err == ERR_UNAUTHORIZED ? TTR("Please check and accept the AI usage agreement before continuing.") : TTR("AI continuation request could not start."));
		request_conversation_id = String();
		return;
	}

	status_label->set_text(TTR("Continuing AI response..."));
	_set_requesting(true);
}

void AIChatPanel::_continue_after_tool_limit() {
	_send_hidden_followup(TTR("Please continue from where you stopped. Do not repeat earlier content; continue the same task from the current conversation context."));
}

void AIChatPanel::_focus_custom_tool_limit_message() {
	_set_tool_limit_options_collapsed(true);
	status_label->set_text(TTR("Type the next message, then send it to continue the conversation."));
	input->grab_focus();
}

void AIChatPanel::_dismiss_tool_limit_options() {
	_hide_tool_limit_options();
	status_label->set_text(TTR("AI tool iteration limit options dismissed."));
}

void AIChatPanel::_add_user_message(const String &p_text) {
	AIChatMessage *message = memnew(AIChatMessage);
	message->setup_user(p_text);
	message->set_display_scale(chat_display_scale);
	message_list->add_child(message);
	message_scroll->set_deferred(SNAME("scroll_vertical"), message_scroll->get_v_scroll_bar()->get_max());
}

void AIChatPanel::_add_ai_message(const String &p_content, const String &p_think_content, double p_think_time, int p_prompt_tokens, int p_completion_tokens) {
	AIChatMessage *message = memnew(AIChatMessage);
	message->setup_ai(p_content, p_think_content, p_think_time, p_prompt_tokens, p_completion_tokens);
	message->connect(SNAME("edit_requested"), callable_mp(this, &AIChatPanel::_on_edit_requested));
	message->set_display_scale(chat_display_scale);
	message_list->add_child(message);
	message_scroll->set_deferred(SNAME("scroll_vertical"), message_scroll->get_v_scroll_bar()->get_max());
}

void AIChatPanel::_show_task_plans(const Vector<AITaskPlan> &p_task_plans) {
	for (int i = 0; i < p_task_plans.size(); i++) {
		const AITaskPlan &plan = p_task_plans[i];
		if (plan.steps.is_empty()) {
			continue;
		}

		String content = TTR("Task Breakdown");
		if (!plan.title.strip_edges().is_empty()) {
			content += ": " + plan.title.strip_edges();
		}
		content += "\n";

		for (int j = 0; j < plan.steps.size(); j++) {
			const AITaskPlan::Step &step = plan.steps[j];
			const String status = step.status.strip_edges().is_empty() ? String("pending") : step.status.strip_edges();
			content += vformat("\n%d. [%s] %s", j + 1, status, step.title.strip_edges());
			if (!step.detail.strip_edges().is_empty()) {
				content += "\n   " + step.detail.strip_edges();
			}
		}

		_add_ai_message(content, String(), 0.0, 0, 0);
	}
}

void AIChatPanel::_on_edit_requested(const String &p_content) {
	editing_message_index = -1;
	for (int i = 0; i < message_list->get_child_count(); i++) {
		AIChatMessage *msg = Object::cast_to<AIChatMessage>(message_list->get_child(i));
		if (msg && msg->get_content() == p_content) {
			editing_message_index = i;
			break;
		}
	}
	input->set_text(p_content);
	input->grab_focus();
}

void AIChatPanel::_add_file_menu_id_pressed(int p_id) {
	if (p_id == FILE_MENU_REFERENCE_PROJECT) {
		reference_file_dialog->popup_file_dialog();
	} else if (p_id == FILE_MENU_UPLOAD_TEXT) {
		upload_file_dialog->popup_file_dialog();
	} else if (p_id == FILE_MENU_IMPORT) {
		import_file_dialog->popup_file_dialog();
	}
}

void AIChatPanel::_project_file_selected(const String &p_path) {
	_add_attachment(p_path, false);
}

void AIChatPanel::_external_file_selected(const String &p_path) {
	_add_attachment(p_path, true);
}

void AIChatPanel::_add_dropped_files(const Vector<String> &p_files) {
	int added = 0;
	for (const String &path : p_files) {
		const bool external = !path.begins_with("res://");
		const int before = attachments.size();
		_add_attachment(path, external);
		if (attachments.size() > before) {
			added++;
		}
	}
	if (added > 1) {
		status_label->set_text(vformat(TTR("Attached %d files."), added));
	}
}

void AIChatPanel::_window_files_dropped(const Vector<String> &p_files) {
	if (!is_visible_in_tree() || !composer_panel || !input) {
		return;
	}
	const Vector2 mouse_pos = get_global_mouse_position();
	if (!composer_panel->get_global_rect().has_point(mouse_pos) && !input->get_global_rect().has_point(mouse_pos)) {
		return;
	}
	_add_dropped_files(p_files);
	input->grab_focus();
}

bool AIChatPanel::_can_drop_files_fw(const Point2 &p_point, const Variant &p_data, Control *p_from) const {
	if (p_data.get_type() != Variant::DICTIONARY) {
		return false;
	}
	const Dictionary d = p_data;
	if (!d.has("type") || String(d["type"]) != "files" || !d.has("files")) {
		return false;
	}
	Vector<String> files = d["files"];
	return !files.is_empty();
}

void AIChatPanel::_drop_files_fw(const Point2 &p_point, const Variant &p_data, Control *p_from) {
	if (!_can_drop_files_fw(p_point, p_data, p_from)) {
		return;
	}
	const Dictionary d = p_data;
	Vector<String> files = d["files"];
	_add_dropped_files(files);
	input->grab_focus();
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

void AIChatPanel::_set_chat_display_scale(float p_scale) {
	chat_display_scale = CLAMP(p_scale, 0.78f, 1.05f);
	if (input) {
		input->add_theme_font_size_override(SceneStringName(font_size), Math::round(14 * chat_display_scale * EDSCALE));
	}
	if (tool_limit_options_panel) {
		tool_limit_options_panel->set_custom_minimum_size(Size2(0, Math::round(64 * chat_display_scale)) * EDSCALE);
	}
	if (tool_limit_options_title) {
		tool_limit_options_title->add_theme_font_size_override(SceneStringName(font_size), Math::round(14 * chat_display_scale * EDSCALE));
	}
	if (next_question_options_box) {
		next_question_options_box->add_theme_constant_override("separation", Math::round(6 * chat_display_scale * EDSCALE));
		for (int i = 0; i < next_question_options_box->get_child_count(); i++) {
			Button *question_button = Object::cast_to<Button>(next_question_options_box->get_child(i));
			if (!question_button) {
				continue;
			}
			question_button->set_custom_minimum_size(Size2(0, Math::round(38 * chat_display_scale)) * EDSCALE);
			question_button->add_theme_font_size_override(SceneStringName(font_size), Math::round(14 * chat_display_scale * EDSCALE));
		}
	}
	Button *option_action_buttons[] = {
		tool_limit_continue_button,
		tool_limit_custom_button,
		tool_limit_stop_button,
		tool_limit_collapse_button,
	};
	for (Button *button : option_action_buttons) {
		if (!button) {
			continue;
		}
		button->set_custom_minimum_size(Size2(0, Math::round(34 * chat_display_scale)) * EDSCALE);
		button->add_theme_font_size_override(SceneStringName(font_size), Math::round(13 * chat_display_scale * EDSCALE));
	}
	if (tool_limit_toggle_button) {
		tool_limit_toggle_button->set_custom_minimum_size(Size2(Math::round(42 * chat_display_scale), Math::round(36 * chat_display_scale)) * EDSCALE);
	}
	if (!message_list) {
		return;
	}
	for (int i = 0; i < message_list->get_child_count(); i++) {
		AIChatMessage *msg = Object::cast_to<AIChatMessage>(message_list->get_child(i));
		if (msg) {
			msg->set_display_scale(chat_display_scale);
		}
	}
}

void AIChatPanel::_update_auto_chat_display_scale() {
	float width = chat_surface_panel ? chat_surface_panel->get_size().x : get_size().x;
	float scale = 0.92f;
	if (width > 1100 * EDSCALE) {
		scale = 1.0f;
	} else if (width > 850 * EDSCALE) {
		scale = 0.96f;
	} else if (width < 560 * EDSCALE) {
		scale = 0.82f;
	} else if (width < 700 * EDSCALE) {
		scale = 0.88f;
	}
	_set_chat_display_scale(scale);
}

void AIChatPanel::_send_message() {
	if (chat_service && chat_service->is_requesting()) {
		status_label->set_text(TTR("Wait for the current AI request to finish before sending another message."));
		return;
	}

	const String text = input->get_text().strip_edges();
	if (text.is_empty()) {
		return;
	}
	_hide_tool_limit_options();

	AISettingsData settings = AISettings::load();
	if (settings.backend_type == AIBackendType::LEGACY_OPENAI && settings.api_key.is_empty()) {
		status_label->set_text(TTR("Configure an API key before sending AI messages."));
		return;
	}
	if (settings.backend_type == AIBackendType::JUNDOT_PLUGIN && (settings.jundot_ai_plugin_id.strip_edges().is_empty() || settings.jundot_ai_plugin_url.strip_edges().is_empty())) {
		status_label->set_text(TTR("Configure the jundot AI plugin before sending AI messages."));
		return;
	}

	if (!_ensure_usage_agreement()) {
		return;
	}

	// In edit mode, update the existing user message in-place and remove
	// all messages after it so the upcoming AI reply overwrites the old one.
	const bool editing_existing_message = editing_message_index >= 0;
	if (editing_existing_message) {
		AIChatMessage *user_msg = Object::cast_to<AIChatMessage>(message_list->get_child(editing_message_index));
		if (user_msg) {
			user_msg->set_content(text);
		}
		for (int i = message_list->get_child_count() - 1; i > editing_message_index; i--) {
			message_list->get_child(i)->queue_free();
		}
		editing_message_index = -1;
		_clear_structured_history();
	} else {
		_add_user_message(text);
	}

	// Persist the conversation after the user message has been added so that
	// a crash or request failure does not lose the message.
	request_conversation_id = active_conversation_id;
	_serialize_current_messages();
	_refresh_conversation_list_ui();
	_save_all_conversations();

	// Build the full messages array with conversation history.
	Array history = _build_message_history();

	// Auto-title: on the first user message of a conversation with no
	// manually-set title, ask the AI to summarise a conversation title
	// once the normal response has been received.
	bool needs_auto_title = !has_auto_titled &&
			(!conversation_name_edit || conversation_name_edit->get_text().strip_edges().is_empty()) &&
			!is_summarizing && !is_titling;

	// Check if history exceeds the character budget �?trigger summarization.
	if (settings.history_char_budget > 0) {
		int total_chars = 0;
		for (int i = 0; i < history.size(); i++) {
			Dictionary entry = history[i];
			total_chars += String(entry["role"]).length() + 2;
			total_chars += String(entry["content"]).length();
		}
		if (total_chars > settings.history_char_budget) {
			pending_user_message = text;
			pending_attachments = attachments;
			_start_summarization(history, settings.history_char_budget);
			return;
		}
	}

	// Queue an auto-title request for after this round completes.
	if (needs_auto_title) {
		pending_title_text = text;
		pending_title_attachments = attachments;
	}

	// Build system prompt + context. Use the effective prompt based on the current context mode.
	String configured_system_prompt = AISettings::get_effective_system_prompt(settings);
	settings.system_prompt = configured_system_prompt;
	settings.system_prompt += "\n\n" + _ai_chat_next_question_protocol();
	const String ai_context = AIContextBuilder::build_context(settings.include_project_memories, settings.include_tool_context, settings.context_char_budget, settings.auto_suggest_entries);
	if (!ai_context.is_empty()) {
		settings.system_prompt += "\n\n" + ai_context;
	}
	chat_service->configure(settings);
	active_settings = settings; // Cache for tool loop reuse.

	Array messages;
	{
		Dictionary system_msg;
		system_msg["role"] = "system";
		system_msg["content"] = settings.system_prompt;
		messages.push_back(system_msg);
	}

	String request_text = text;
	const String attachment_context = _build_attachment_context();
	if (!attachment_context.is_empty()) {
		request_text += "\n\n" + attachment_context;
	}

	Array structured_history = _get_structured_history();
	if (!structured_history.is_empty()) {
		for (int i = 0; i < structured_history.size(); i++) {
			messages.push_back(structured_history[i]);
		}
		Dictionary user_message;
		user_message["role"] = "user";
		user_message["content"] = request_text;
		messages.push_back(user_message);
	} else {
		for (int i = 0; i < history.size(); i++) {
			Dictionary entry = history[i];
			if (i == history.size() - 1 && String(entry["role"]) == "user") {
				entry["content"] = request_text;
			}
			messages.push_back(entry);
		}
	}

	// If tools are enabled, include tool definitions filtered by the current context mode.
	Array tools;
	if (settings.tools_enabled) {
		tools = AIToolDefs::get_tools_for_mode(settings.context_mode);
		if (settings.mcp_tools_enabled) {
			Array mcp_tools = AIToolDefs::get_mcp_tools();
			if (!mcp_tools.is_empty()) {
				tools.append_array(mcp_tools);
			}
		}
	}

	pending_tool_round = PendingToolRound();
	pending_tool_round.max_iterations = settings.max_tool_iterations > 0 ? settings.max_tool_iterations : 10;
	pending_tool_round.original_messages = messages.duplicate(true);
	pending_tool_round.original_tools = tools.duplicate(true);
	in_tool_loop = false;
	_start_response_tracking();

	const Error err = chat_service->send_messages(messages, tools);
	if (err != OK) {
		_clear_response_tracking();
		status_label->set_text(err == ERR_UNAUTHORIZED ? TTR("Please check and accept the AI usage agreement before sending messages.") : TTR("AI request could not start."));
		request_conversation_id = String();
		return;
	}

	input->clear();
	attachments.clear();
	_refresh_attachment_chips();
	status_label->set_text(TTR("Waiting for AI response..."));
	_set_requesting(true);
}

void AIChatPanel::_start_summarization(const Array &p_history, int p_budget) {
	// Build summary request: send oldest messages to AI for summarization.
	Array summary_messages;
	{
		Dictionary summary_system;
		summary_system["role"] = "system";
		summary_system["content"] = TTR("You are a conversation summarizer. Summarize the following conversation concisely, preserving key decisions, code changes, root causes, and current state. Keep the summary under 500 characters.");
		summary_messages.push_back(summary_system);
	}

	// Collect the messages that need summarization (everything except the last user+assistant pair).
	int char_count = 0;
	int summarize_end = 0;
	for (int i = 0; i < p_history.size() - 1; i++) {
		Dictionary entry = p_history[i];
		int entry_size = String(entry["role"]).length() + 2 + String(entry["content"]).length();
		char_count += entry_size;
		// Stop when adding the next entry would exceed the remaining budget
		// (reserve ~200 chars for the summary itself).
		if (p_budget > 0 && (char_count + 200) > p_budget) {
			break;
		}
		summarize_end = i + 1;
	}

	// Build the summarization request body.
	String summary_text;
	for (int i = 0; i < summarize_end && i < p_history.size(); i++) {
		Dictionary entry = p_history[i];
		summary_text += String(entry["role"]) + ": " + String(entry["content"]) + "\n";
	}

	Dictionary summary_request;
	summary_request["role"] = "user";
	summary_request["content"] = TTR("Please summarize the following conversation:") + "\n\n" + summary_text;
	summary_messages.push_back(summary_request);

	// Configure service with minimal token limit for summary.
	AISettingsData summary_settings = AISettings::load();
	summary_settings.max_tokens = 256;
	summary_settings.temperature = 0.3; // Lower temperature for more deterministic summary.
	chat_service->configure(summary_settings);

	is_summarizing = true;
	status_label->set_text(TTR("Compressing conversation history..."));
	_set_requesting(true);

	const Error err = chat_service->send_messages(summary_messages);
	if (err != OK) {
		// Fall back to truncation if the summary request fails to send.
		_summary_completed(String());
	}
}

void AIChatPanel::_summary_completed(const String &p_summary_text) {
	is_summarizing = false;

	// Find which messages to replace (all non-summary messages before the last user message).
	int replace_end = -1;
	for (int i = message_list->get_child_count() - 1; i >= 0; i--) {
		AIChatMessage *msg = Object::cast_to<AIChatMessage>(message_list->get_child(i));
		if (msg && msg->is_summary_message()) {
			// Avoid replacing existing summary messages.
			replace_end = i;
			break;
		}
	}
	if (replace_end < 0) {
		replace_end = message_list->get_child_count();
	}

	if (!p_summary_text.is_empty()) {
		// Summary success: remove old messages and insert summary.
		// Keep at most the last pair (user + assistant) of non-summary messages.
		int keep_start = message_list->get_child_count() - 1;
		// Count backwards to find the last user message.
		int last_user_idx = -1;
		for (int i = keep_start; i >= 0; i--) {
			AIChatMessage *msg = Object::cast_to<AIChatMessage>(message_list->get_child(i));
			if (msg && msg->is_user_message() && !msg->is_summary_message()) {
				last_user_idx = i;
				break;
			}
		}

		// Remove messages before the last user message (except summaries).
		for (int i = replace_end - 1; i >= 0; i--) {
			AIChatMessage *msg = Object::cast_to<AIChatMessage>(message_list->get_child(i));
			if (msg && !msg->is_summary_message()) {
				// Stop if we've reached the last user message or its preceding messages.
				if (i < last_user_idx - 1) {
					message_list->get_child(i)->queue_free();
				}
			}
		}

		// Insert summary message at the beginning.
		_add_summary_message(p_summary_text.strip_edges());
	} else {
		// Summary failed: fall back to truncation.
		// Remove the oldest ~1/3 of messages.
		int remove_count = MAX(1, message_list->get_child_count() / 3);
		for (int i = remove_count - 1; i >= 0; i--) {
			AIChatMessage *msg = Object::cast_to<AIChatMessage>(message_list->get_child(i));
			if (msg && !msg->is_summary_message()) {
				message_list->get_child(i)->queue_free();
			}
		}
		status_label->set_text(TTR("Failed to compress history, truncated oldest messages."));
	}

	// Restore attachments and re-send.
	attachments = pending_attachments;
	_refresh_attachment_chips();
	input->set_text(pending_user_message);
	pending_user_message = String();
	pending_attachments.clear();

	// Re-send the actual message (this time budget should not be exceeded).
	editing_message_index = -1; // Not in edit mode for resume.
	_send_message();
}

void AIChatPanel::_add_summary_message(const String &p_content) {
	AIChatMessage *message = memnew(AIChatMessage);
	message->setup_summary(p_content);
	message->set_display_scale(chat_display_scale);
	// Insert at the beginning of the message list, after any existing summaries.
	int insert_pos = 0;
	for (int i = 0; i < message_list->get_child_count(); i++) {
		AIChatMessage *msg = Object::cast_to<AIChatMessage>(message_list->get_child(i));
		if (msg && msg->is_summary_message()) {
			insert_pos = i + 1;
		} else {
			break;
		}
	}
	message_list->add_child(message);
	message->move_to_front(); // Simplified: just add to front.
	message_scroll->set_deferred(SNAME("scroll_vertical"), message_scroll->get_v_scroll_bar()->get_max());
}

void AIChatPanel::_try_auto_title(const String &p_user_first_message, const Array &p_history) {
	(void)p_history;

	if (conversation_name_edit && !conversation_name_edit->get_text().strip_edges().is_empty()) {
		has_auto_titled = true;
		return;
	}

	String title = p_user_first_message.strip_edges();
	const int newline_pos = title.find_char('\n');
	if (newline_pos >= 0) {
		title = title.substr(0, newline_pos).strip_edges();
	}
	if (title.length() > 60) {
		title = title.substr(0, 57).strip_edges() + "...";
	}
	if (title.is_empty()) {
		return;
	}

	if (conversation_name_edit) {
		conversation_name_edit->set_text(title);
	}
	for (int i = 0; i < conversations.size(); i++) {
		if (conversations[i].id == active_conversation_id) {
			conversations.write[i].title = title;
			conversations.write[i].updated_at = Time::get_singleton()->get_unix_time_from_system();
			break;
		}
	}
	has_auto_titled = true;
	_refresh_conversation_list_ui();
	_save_all_conversations();
}

void AIChatPanel::_title_completed(int p_result, int p_response_code, const String &p_title_content) {
	is_titling = false;

	if (p_result != HTTPRequest::RESULT_SUCCESS || p_response_code >= HTTPClient::RESPONSE_BAD_REQUEST) {
		// Silent failure: leave the title blank for the user to set.
		title_request_conversation_id = String();
		return;
	}

	String title = p_title_content.strip_edges();
	// Strip surrounding quotes that the model sometimes emits.
	if (title.length() >= 2) {
		// ASCII quotes.
		if ((title[0] == '"' && title[title.length() - 1] == '"') ||
				(title[0] == '\'' && title[title.length() - 1] == '\'') ||
				(title[0] == 0x201C && title[title.length() - 1] == 0x201D)) {
			title = title.substr(1, title.length() - 2).strip_edges();
		}
	}
	// Take the first line only �?some models emit a short paragraph.
	int newline_pos = title.find_char('\n');
	if (newline_pos >= 0) {
		title = title.substr(0, newline_pos).strip_edges();
	}

	// Hard cap the display length.
	if (title.length() > 120) {
		title = title.substr(0, 120).strip_edges() + "...";
	}

	if (!title.is_empty()) {
		// Only overwrite the title field if the user has not typed one yet.
		if (conversation_name_edit && conversation_name_edit->get_text().strip_edges().is_empty()) {
			conversation_name_edit->set_text(title);
			has_auto_titled = true;
		}
		for (int i = 0; i < conversations.size(); i++) {
			if (conversations[i].id == title_request_conversation_id) {
				conversations.write[i].title = title;
				conversations.write[i].updated_at = Time::get_singleton()->get_unix_time_from_system();
				has_auto_titled = true;
				_refresh_conversation_list_ui();
				_save_all_conversations();
				break;
			}
		}
	}
	title_request_conversation_id = String();
}

void AIChatPanel::_cancel_request() {
	_stop_build_status_poll();

	if (is_summarizing) {
		is_summarizing = false;
		pending_user_message = String();
		pending_attachments.clear();
	}
	if (is_titling) {
		is_titling = false;
		title_request_conversation_id = String();
	}
	chat_service->cancel_request();
	if (tool_execution_running) {
		tool_execution_cancelled = true;
	}
	_hide_tool_limit_options();
	if (streaming_message) {
		streaming_message->queue_free();
		streaming_message = nullptr;
	}
	pending_tool_round = PendingToolRound();
	in_tool_loop = false;
	_clear_response_tracking();
	request_conversation_id = String();
	status_label->set_text(TTR("AI request cancelled."));
	_set_requesting(false);
}

void AIChatPanel::_clear_input() {
	input->clear();
	attachments.clear();
	_refresh_attachment_chips();
	input->grab_focus();
	status_label->set_text(TTR("Input cleared."));
}

void AIChatPanel::_clear_messages() {
	_stop_build_status_poll();

	is_summarizing = false;
	is_titling = false;
	pending_user_message = String();
	pending_attachments.clear();
	pending_title_text = String();
	pending_title_attachments.clear();
	for (int i = message_list->get_child_count() - 1; i >= 0; i--) {
		message_list->get_child(i)->queue_free();
	}
	editing_message_index = -1;
	_clear_suggestions();
	_clear_repair_cards();
	pending_tool_round = PendingToolRound();
	in_tool_loop = false;
	_clear_structured_history();
	if (tool_execution_running) {
		tool_execution_cancelled = true;
	}
	_hide_tool_limit_options();
	if (conversation_name_edit) {
		conversation_name_edit->set_text("");
	}
	for (int i = 0; i < conversations.size(); i++) {
		if (conversations[i].id == active_conversation_id) {
			conversations.write[i].title = TTR("New Chat");
			break;
		}
	}
	has_auto_titled = false;
	_serialize_current_messages();
	_refresh_conversation_list_ui();
	_save_all_conversations();
	status_label->set_text(TTR("AI assistant ready."));
}

void AIChatPanel::_chat_stream_data(const String &p_delta, const String &p_full_content, int p_completion_tokens) {
	if (is_titling || is_summarizing) {
		return;
	}
	if (!request_conversation_id.is_empty() && request_conversation_id != active_conversation_id) {
		return;
	}

	if (!streaming_message) {
		streaming_message = memnew(AIChatMessage);
		streaming_message->setup_ai(p_full_content, String(), _get_response_elapsed(0.0), 0, p_completion_tokens);
		streaming_message->connect(SNAME("edit_requested"), callable_mp(this, &AIChatPanel::_on_edit_requested));
		streaming_message->set_display_scale(chat_display_scale);
		message_list->add_child(streaming_message);
	} else {
		streaming_message->set_markdown_content(p_full_content);
	}
	message_scroll->set_deferred(SNAME("scroll_vertical"), message_scroll->get_v_scroll_bar()->get_max());
}

Array AIChatPanel::_get_available_tools_for_active_settings() const {
	Array available_tools = pending_tool_round.original_tools;
	if (available_tools.is_empty()) {
		available_tools = AIToolDefs::get_tools_for_mode(active_settings.context_mode);
		if (active_settings.mcp_tools_enabled) {
			Array mcp_tools = AIToolDefs::get_mcp_tools();
			if (!mcp_tools.is_empty()) {
				available_tools.append_array(mcp_tools);
			}
		}
	}
	return available_tools;
}

bool AIChatPanel::_looks_like_tool_preamble(const String &p_content) const {
	if (!active_settings.tools_enabled || pending_tool_round.executed_tool_calls || in_tool_loop || _get_available_tools_for_active_settings().is_empty()) {
		return false;
	}

	const String text = p_content.strip_edges();
	if (text.is_empty()) {
		return false;
	}

	const String lower = text.to_lower();
	if (lower.contains("read_files") || lower.contains("write_file") || lower.contains("search_files") || lower.contains("list_files") ||
			lower.contains("grep_code") || lower.contains("run_build") || lower.contains("batch_tools") || lower.contains("tool call")) {
		return true;
	}

	if (text.contains(".cpp") || text.contains(".h") || text.contains(".gd") || text.contains(".tscn") || text.contains(".cs")) {
		return true;
	}

	if ((lower.contains("read") || lower.contains("inspect") || lower.contains("open")) &&
			(lower.contains("file") || lower.contains(".cpp") || lower.contains(".h") || lower.contains(".gd"))) {
		return true;
	}

	return false;
}

bool AIChatPanel::_extract_text_tool_calls(const String &p_content, Array &r_tool_calls) const {
	if (!active_settings.tools_enabled) {
		return false;
	}

	Array available_tools = _get_available_tools_for_active_settings();
	if (available_tools.is_empty()) {
		return false;
	}

	String content = p_content;
	content = content.replace("&lt;", "<");
	content = content.replace("&gt;", ">");
	content = content.replace("&quot;", "\"");
	content = content.replace("&apos;", "'");
	content = content.replace("&amp;", "&");

	int search_pos = 0;
	while (true) {
		const int block_start = content.find("<tool_call", search_pos);
		if (block_start == -1) {
			break;
		}

		const int block_open_end = content.find(">", block_start);
		const int block_end = content.find("</tool_call>", block_open_end + 1);
		if (block_open_end == -1 || block_end == -1) {
			break;
		}

		const String block = content.substr(block_open_end + 1, block_end - block_open_end - 1);
		const int fn_start = block.find("<function=");
		if (fn_start == -1) {
			search_pos = block_end + 12;
			continue;
		}

		const int fn_name_start = fn_start + String("<function=").length();
		const int fn_open_end = block.find(">", fn_name_start);
		const int fn_end = block.find("</function>", fn_open_end + 1);
		if (fn_open_end == -1 || fn_end == -1) {
			search_pos = block_end + 12;
			continue;
		}

		String fn_name = block.substr(fn_name_start, fn_open_end - fn_name_start).strip_edges();
		fn_name = fn_name.trim_prefix("\"").trim_suffix("\"").trim_prefix("'").trim_suffix("'");
		if (fn_name == "read_file") {
			fn_name = "read_files";
		}
		if (fn_name.is_empty()) {
			search_pos = block_end + 12;
			continue;
		}

		bool tool_available = false;
		for (int i = 0; i < available_tools.size(); i++) {
			if (available_tools[i].get_type() != Variant::DICTIONARY) {
				continue;
			}
			Dictionary tool = available_tools[i];
			Dictionary fn_def = tool.get("function", Dictionary());
			if (String(fn_def.get("name", String())) == fn_name) {
				tool_available = true;
				break;
			}
		}
		if (!tool_available) {
			search_pos = block_end + 12;
			continue;
		}

		const String fn_body = block.substr(fn_open_end + 1, fn_end - fn_open_end - 1);
		Dictionary args;
		int param_pos = 0;
		while (true) {
			const int param_start = fn_body.find("<parameter=", param_pos);
			if (param_start == -1) {
				break;
			}

			const int param_name_start = param_start + String("<parameter=").length();
			const int param_open_end = fn_body.find(">", param_name_start);
			if (param_open_end == -1) {
				break;
			}

			String param_name = fn_body.substr(param_name_start, param_open_end - param_name_start).strip_edges();
			param_name = param_name.trim_prefix("\"").trim_suffix("\"").trim_prefix("'").trim_suffix("'");
			const String close_tag = "</parameter>";
			const int param_end = fn_body.find(close_tag, param_open_end + 1);
			if (param_end == -1) {
				break;
			}

			const String raw_value = fn_body.substr(param_open_end + 1, param_end - param_open_end - 1).strip_edges();
			Variant parsed_value = JSON::parse_string(raw_value);
			if (parsed_value.get_type() == Variant::NIL && raw_value != "null") {
				String scalar = raw_value;
				if ((scalar.begins_with("\"") && scalar.ends_with("\"")) || (scalar.begins_with("'") && scalar.ends_with("'"))) {
					scalar = scalar.substr(1, scalar.length() - 2);
					scalar = scalar.replace("\\\\", "\\");
					scalar = scalar.replace("\\\"", "\"");
					scalar = scalar.replace("\\'", "'");
				}
				args[param_name] = scalar;
			} else {
				args[param_name] = parsed_value;
			}

			param_pos = param_end + close_tag.length();
		}

		// Be permissive with text-form tool calls generated by models. The
		// read_files tool requires an array named "paths", but models often emit
		// a single string or use "path" when producing XML-like pseudo calls.
		if (fn_name == "read_files") {
			if (!args.has("paths") && args.has("path")) {
				args["paths"] = args["path"];
				args.erase("path");
			}
			if (args.has("paths") && args["paths"].get_type() == Variant::STRING) {
				Array paths;
				paths.push_back(String(args["paths"]));
				args["paths"] = paths;
			}
		}

		Dictionary fn;
		fn["name"] = fn_name;
		fn["arguments"] = JSON::stringify(args);

		Dictionary tool_call;
		tool_call["id"] = "text-tool-" + itos(r_tool_calls.size());
		tool_call["type"] = "function";
		tool_call["function"] = fn;
		r_tool_calls.push_back(tool_call);

		search_pos = block_end + String("</tool_call>").length();
	}

	return !r_tool_calls.is_empty();
}

String AIChatPanel::_strip_text_tool_call_blocks(const String &p_content) const {
	String result = p_content;
	while (true) {
		const int block_start = result.find("<tool_call");
		if (block_start == -1) {
			break;
		}

		const int block_open_end = result.find(">", block_start);
		const int block_end = result.find("</tool_call>", block_open_end + 1);
		if (block_open_end == -1 || block_end == -1) {
			break;
		}

		result = result.substr(0, block_start) + result.substr(block_end + String("</tool_call>").length());
	}
	return result.strip_edges();
}

void AIChatPanel::_start_response_tracking() {
	response_started_usec = OS::get_singleton()->get_ticks_usec();
	accumulated_think_content.clear();

	if (!response_elapsed_timer) {
		response_elapsed_timer = memnew(Timer);
		response_elapsed_timer->set_wait_time(0.1);
		response_elapsed_timer->set_one_shot(false);
		response_elapsed_timer->connect("timeout", callable_mp(this, &AIChatPanel::_on_response_elapsed_tick));
		add_child(response_elapsed_timer, false, INTERNAL_MODE_BACK);
	}
	response_elapsed_timer->start();
	_on_response_elapsed_tick();
}

void AIChatPanel::_on_response_elapsed_tick() {
	if (response_started_usec == 0 || !is_inside_tree()) {
		return;
	}
	if (!request_conversation_id.is_empty() && request_conversation_id != active_conversation_id) {
		return;
	}

	const double elapsed = _get_response_elapsed(0.0);
	if (!streaming_message) {
		streaming_message = memnew(AIChatMessage);
		streaming_message->setup_ai(String(), String(), elapsed, 0, 0);
		streaming_message->connect(SNAME("edit_requested"), callable_mp(this, &AIChatPanel::_on_edit_requested));
		streaming_message->set_display_scale(chat_display_scale);
		message_list->add_child(streaming_message);
	} else {
		streaming_message->set_think_time_seconds(elapsed);
	}
	message_scroll->set_deferred(SNAME("scroll_vertical"), message_scroll->get_v_scroll_bar()->get_max());
}

void AIChatPanel::_append_response_thought(const String &p_content) {
	const String thought = p_content.strip_edges();
	if (thought.is_empty()) {
		return;
	}
	if (!accumulated_think_content.is_empty()) {
		accumulated_think_content += "\n\n";
	}
	accumulated_think_content += thought;
}

String AIChatPanel::_get_response_thought(const String &p_current_thought) const {
	String result = accumulated_think_content;
	const String current = p_current_thought.strip_edges();
	if (!current.is_empty()) {
		if (!result.is_empty()) {
			result += "\n\n";
		}
		result += current;
	}
	return result;
}

double AIChatPanel::_get_response_elapsed(double p_fallback_elapsed) const {
	if (response_started_usec == 0) {
		return p_fallback_elapsed;
	}
	return (OS::get_singleton()->get_ticks_usec() - response_started_usec) / 1000000.0;
}

void AIChatPanel::_clear_response_tracking() {
	if (response_elapsed_timer) {
		response_elapsed_timer->stop();
	}
	response_started_usec = 0;
	accumulated_think_content.clear();
	if (streaming_message && streaming_message->get_content().strip_edges().is_empty()) {
		streaming_message->queue_free();
		streaming_message = nullptr;
	}
}

bool AIChatPanel::_retry_after_missing_tool_call(const String &p_content) {
	if (pending_tool_round.missing_tool_retry_used || pending_tool_round.original_messages.is_empty()) {
		return false;
	}

	Array available_tools = _get_available_tools_for_active_settings();
	if (available_tools.is_empty()) {
		return false;
	}

	pending_tool_round.missing_tool_retry_used = true;
	pending_tool_round.original_tools = available_tools;

	Array messages = pending_tool_round.original_messages.duplicate(true);

	Dictionary assistant_msg;
	assistant_msg["role"] = "assistant";
	assistant_msg["content"] = p_content;
	messages.push_back(assistant_msg);

	Dictionary correction_msg;
	correction_msg["role"] = "user";
	correction_msg["content"] = TTR("Your previous response described using a tool, but it did not include a structured tool_calls response, so the editor could not execute anything. Do not describe the tool use in prose. Return the appropriate function call now using the available tools. If you need to inspect directories, call list_files with path and depth. If you need to read a file, call read_files with the exact relative path.");
	messages.push_back(correction_msg);

	pending_tool_round.original_messages = messages.duplicate(true);

	tool_call_label->set_visible(true);
	tool_call_label->set_text(TTR("AI described a tool action without calling it. Retrying once with a stricter tool-call instruction..."));
	status_label->set_text(TTR("Retrying AI request for a real tool call..."));

	chat_service->configure(active_settings);
	_set_requesting(true);
	Error err = chat_service->send_messages(messages, available_tools);
	if (err != OK) {
		_set_requesting(false);
		status_label->set_text(TTR("Tool-call retry could not start."));
		return false;
	}

	return true;
}

void AIChatPanel::_chat_completed(int p_result, int p_response_code, const String &p_content, const Dictionary &p_json, const String &p_raw_body, double p_elapsed_seconds, const String &p_think_content, int p_prompt_tokens, int p_completion_tokens) {
	// Handle title response first (auto-summary conversation title).
	if (is_titling) {
		_title_completed(p_result, p_response_code, p_content);
		return;
	}

	// Handle summarization response before normal response.
	if (is_summarizing) {
		if (p_result == HTTPRequest::RESULT_SUCCESS && p_response_code < HTTPClient::RESPONSE_BAD_REQUEST) {
			// Extract the summary text from the AI response.
			_summary_completed(p_content);
		} else {
			// Summarization failed �?fall back to truncation.
			_summary_completed(String());
		}
		return;
	}

	_set_requesting(false);
	if (p_result == HTTPRequest::RESULT_SUCCESS && p_response_code < HTTPClient::RESPONSE_BAD_REQUEST) {
		if (p_json.has("choices") && p_json["choices"].get_type() == Variant::ARRAY) {
			Array choices = p_json["choices"];
			if (choices.size() > 0 && choices[0].get_type() == Variant::DICTIONARY) {
				Dictionary first = choices[0];
				String finish_reason = first.get("finish_reason", String());
				Dictionary msg = first.get("message", Dictionary());
				bool has_tool_calls = (finish_reason == "tool_calls") ||
					(msg.has("tool_calls") && msg["tool_calls"].get_type() == Variant::ARRAY &&
						!((Array)msg["tool_calls"]).is_empty());
				if (has_tool_calls) {
					Vector<AITaskPlan> task_plans;
					AIChatParser::parse_task_plans(p_content, task_plans);
					String visible_content = AIChatParser::strip_next_question_blocks(AIChatParser::strip_task_plan_blocks(p_content));
					_append_response_thought(p_think_content);
					_append_response_thought(visible_content);
					if (!task_plans.is_empty()) {
						_show_task_plans(task_plans);
					}
					if (pending_tool_round.iteration_count >= pending_tool_round.max_iterations) {
						if (streaming_message) {
							if (streaming_message->get_content().strip_edges().is_empty()) {
								streaming_message->queue_free();
							}
							streaming_message = nullptr;
						}
						Array final_messages = pending_tool_round.original_messages.duplicate(true);
						Dictionary final_request;
						final_request["role"] = "user";
						final_request["content"] = TTR("The tool-call iteration limit has been reached. Stop requesting tools now. Based only on the tool results already in this conversation, provide a clear final response: summarize what you found, what was changed or should be changed, and any remaining next steps. Do not request or describe more tool calls.");
						final_messages.push_back(final_request);

						tool_call_label->set_visible(true);
						tool_call_label->set_text(TTR("Tool iteration limit reached. Asking AI to summarize results..."));
						in_tool_loop = false;
						pending_tool_round.original_messages = final_messages.duplicate(true);
						pending_tool_round.original_tools.clear();
						chat_service->configure(active_settings);
						_set_requesting(true);
						Error err = chat_service->send_messages(final_messages, Array());
						if (err != OK) {
							_set_requesting(false);
							_add_ai_message(vformat(TTR("Maximum tool call iterations (%d) reached, and the final summary request could not start. Please ask the AI to summarize the results manually."),
									active_settings.max_tool_iterations),
									String(), 0.0, p_prompt_tokens, p_completion_tokens);
							tool_call_label->set_visible(false);
							tool_call_label->set_text(String());
							status_label->set_text(TTR("AI tool iteration limit reached."));
							_set_next_question_options(Vector<String>(), false);
							_show_tool_limit_options(true);
							_serialize_current_messages();
							_refresh_conversation_list_ui();
							_save_all_conversations();
							request_conversation_id = String();
						}
						return;
					}
					if (streaming_message) {
						if (streaming_message->get_content().strip_edges().is_empty()) {
							streaming_message->queue_free();
						}
						streaming_message = nullptr;
					}
					_execute_tool_calls(p_json);
					return;
				}
			}
		}

		Array text_tool_calls;
		if (_extract_text_tool_calls(p_content, text_tool_calls)) {
			_append_response_thought(p_think_content);
			_append_response_thought(_strip_text_tool_call_blocks(
					AIChatParser::strip_next_question_blocks(AIChatParser::strip_task_plan_blocks(p_content))));
			if (streaming_message) {
				streaming_message->queue_free();
				streaming_message = nullptr;
			}

			Dictionary fn_msg;
			fn_msg["role"] = "assistant";
			fn_msg["content"] = String();
			fn_msg["tool_calls"] = text_tool_calls;

			Dictionary choice;
			choice["finish_reason"] = "tool_calls";
			choice["message"] = fn_msg;

			Array choices;
			choices.push_back(choice);

			Dictionary synthetic_json;
			synthetic_json["choices"] = choices;

			tool_call_label->set_visible(true);
			tool_call_label->set_text(TTR("AI returned a text tool call. Converting it to an executable tool call..."));
			_execute_tool_calls(synthetic_json);
			return;
		}

		if (_looks_like_tool_preamble(p_content)) {
			if (_retry_after_missing_tool_call(p_content)) {
				_append_response_thought(p_think_content);
				_append_response_thought(p_content);
				return;
			}
		}

		Vector<AITaskPlan> task_plans;
		AIChatParser::parse_task_plans(p_content, task_plans);
		Vector<String> generated_next_questions;
		AIChatParser::parse_next_questions(p_content, generated_next_questions);
		if (generated_next_questions.is_empty() && _looks_like_tool_preamble(p_content)) {
			generated_next_questions.push_back(TTR("Please continue from where you stopped, using function calling tools to perform the next concrete step."));
		}

		String final_content = _strip_text_tool_call_blocks(AIChatParser::strip_next_question_blocks(AIChatParser::strip_task_plan_blocks(p_content)));
		if (final_content.is_empty() && p_content.find("<tool_call") != -1) {
			if (!active_settings.tools_enabled) {
				final_content = TTR("AI returned a text tool call, but Function Calling tools are disabled. Enable tools in AI settings and try again.");
			} else if (_get_available_tools_for_active_settings().is_empty()) {
				final_content = TTR("AI returned a text tool call, but no tools are available for the current mode.");
			} else {
				final_content = TTR("AI returned a text tool call, but it could not be parsed or the requested tool is not available. Please try again with a valid tool call.");
			}
		}
		if (_looks_like_tool_preamble(p_content) && pending_tool_round.missing_tool_retry_used && !pending_tool_round.executed_tool_calls) {
			final_content += TTR("\n\n[Tool calling did not run: the model returned normal text instead of a structured tool_calls response. Use a model or API endpoint that supports OpenAI-compatible function calling, and make sure Function Calling tools are enabled.]");
		}
		if (generated_next_questions.is_empty() && !final_content.strip_edges().is_empty()) {
			// Some providers occasionally omit the hidden protocol block even
			// when it is required. Keep the editor-native follow-up UI usable
			// instead of making the entire options panel disappear.
			generated_next_questions.push_back(TTR("Please explain the most likely root cause in more detail."));
			generated_next_questions.push_back(TTR("Please inspect the relevant code and propose a concrete fix."));
			generated_next_questions.push_back(TTR("Please implement the fix and verify the result."));
		}

		const String response_thought = _get_response_thought(p_think_content);
		const double response_elapsed = _get_response_elapsed(p_elapsed_seconds);
		if (streaming_message) {
			streaming_message->setup_ai(final_content, response_thought, response_elapsed, p_prompt_tokens, p_completion_tokens);
			streaming_message = nullptr;
		} else if (!final_content.strip_edges().is_empty()) {
			_add_ai_message(final_content, response_thought, response_elapsed, p_prompt_tokens, p_completion_tokens);
		}
		if (!task_plans.is_empty()) {
			_show_task_plans(task_plans);
		}

		// Auto-title: if a title request was queued, fire it off now.
		if (!pending_title_text.is_empty() && !is_titling) {
			Array title_history = _build_message_history();
			String first_user_message = pending_title_text;
			String attachment_context = "";
			for (int i = 0; i < pending_title_attachments.size(); i++) {
				attachment_context += pending_title_attachments[i].display_name + "; ";
			}
			pending_title_text = String();
			pending_title_attachments.clear();
			_try_auto_title(first_user_message, title_history);
		}

		// Clear tool call display.
		tool_call_label->set_visible(false);
		tool_call_label->set_text(String());
		in_tool_loop = false;

		// Persist the conversation (and refresh the sidebar title since new
		// user + AI messages may have changed the auto title).
		_store_structured_history(pending_tool_round.original_messages, final_content);
		_set_next_question_options(generated_next_questions, false);
		_serialize_current_messages();
		_refresh_conversation_list_ui();
		_save_all_conversations();
		request_conversation_id = String();
		_clear_response_tracking();

		// Parse suggestions from the AI response.
		const AISettingsData settings = AISettings::load();
		if (settings.auto_suggest_entries) {
			Vector<AISuggestion> suggestions;
			AIChatParser::parse(final_content, suggestions);
			if (!suggestions.is_empty()) {
				_show_suggestions(suggestions);
			}

			Vector<AIFeatureGateResult> feature_gates;
			AIChatParser::parse_feature_gates(final_content, feature_gates);
			for (int i = 0; i < feature_gates.size(); i++) {
				AIFeatureGateResult gate = feature_gates[i];
				AIFeatureGate::evaluate(gate, settings);
				_add_ai_message(AIFeatureGate::build_report(gate), String(), 0.0, 0, 0);
			}

			Vector<AIRepairSuggestion> repair_tasks;
			AIChatParser::parse_repair_tasks(final_content, repair_tasks);
			if (!repair_tasks.is_empty()) {
				_show_repair_tasks(repair_tasks);
			}
		}
		status_label->set_text(TTR("AI response received."));
		_show_tool_limit_options(false);
		return;
	}

	if (streaming_message) {
		streaming_message->queue_free();
		streaming_message = nullptr;
	}
	String error_text = p_content;
	if (error_text.is_empty()) {
		error_text = vformat(TTR("AI request failed. HTTP %d."), p_response_code);
	}
	if (!p_raw_body.strip_edges().is_empty() && !error_text.contains(p_raw_body.strip_edges())) {
		String raw_preview = p_raw_body.strip_edges();
		if (raw_preview.length() > 1000) {
			raw_preview = raw_preview.substr(0, 1000) + "...";
		}
		error_text += "\n\n" + raw_preview;
	}
	_add_ai_message(error_text, String(), 0.0, 0, 0);
	// Keep any completed tool calls/results even when the provider fails
	// afterwards (for example due to rate limiting). The next continuation
	// can then resume from the actual inspected state instead of starting over.
	_store_structured_history(pending_tool_round.original_messages);
	Vector<String> retry_options;
	retry_options.push_back(TTR("Please continue from where you stopped, using function calling tools to perform the next concrete step."));
	_set_next_question_options(retry_options, false);
	_show_tool_limit_options(false);
	_serialize_current_messages();
	_refresh_conversation_list_ui();
	_save_all_conversations();
	request_conversation_id = String();
	_clear_response_tracking();
	status_label->set_text(error_text);
}

void AIChatPanel::_execute_tool_calls(const Dictionary &p_json) {
	Array choices = p_json["choices"];
	Dictionary first_choice = choices[0];
	Dictionary msg = first_choice["message"];
	Array tool_calls = msg["tool_calls"];

	// Store the tool calls JSON for reuse by the execution path.
	pending_tool_calls_json = p_json;
	pending_tool_round.executed_tool_calls = true;

	// Increment the iteration counter for multi-round iteration.
	pending_tool_round.iteration_count++;
	int current_iteration = pending_tool_round.iteration_count;
	int max_iterations = pending_tool_round.max_iterations;

	// Show status with iteration progress.
	status_label->set_text(vformat(TTR("AI tool call round %d of %d: executing %d tool call(s)..."),
			current_iteration, max_iterations, tool_calls.size()));

	// Build tool call display label.
	String tool_display;
	for (int i = 0; i < tool_calls.size(); i++) {
		Dictionary tc = tool_calls[i];
		Dictionary fn = tc.get("function", Dictionary());
		String name = fn.get("name", "?");
		if (i > 0) {
			tool_display += "\n";
		}
		tool_display += vformat(TTR("Calling tool %d/%d: %s (round %d/%d)"),
				i + 1, tool_calls.size(), name, current_iteration, max_iterations);
	}
	tool_call_label->set_visible(true);
	tool_call_label->set_text(tool_display);

	// Execute all requested tool calls immediately.
	_confirm_tool_execute(0);
}

bool AIChatPanel::_tool_result_needs_build_poll(const String &p_content) const {
	const String lower = p_content.to_lower();
	return lower.contains("build started in background") ||
			lower.contains("a build is already running in the background") ||
			lower.contains("build is still running in the background");
}

void AIChatPanel::_start_build_status_poll() {
	if (!build_status_poll_timer) {
		build_status_poll_timer = memnew(Timer);
		build_status_poll_timer->set_wait_time(5.0);
		build_status_poll_timer->set_one_shot(false);
		build_status_poll_timer->connect("timeout", callable_mp(this, &AIChatPanel::_on_build_status_poll_timeout));
		add_child(build_status_poll_timer, false, INTERNAL_MODE_BACK);
	}

	build_status_poll_count = 0;
	build_status_poll_timer->start();
	status_label->set_text(TTR("Build is running in the background. Monitoring until it completes..."));
	tool_call_label->set_visible(true);
	tool_call_label->set_text(TTR("Build started. Waiting for build status..."));
	_set_requesting(true);
}

void AIChatPanel::_stop_build_status_poll() {
	if (build_status_poll_timer) {
		build_status_poll_timer->stop();
	}
	build_status_poll_count = 0;
}

void AIChatPanel::_append_forced_build_status_check() {
	build_status_poll_count++;
	String call_id = vformat("auto_check_build_status_%d", build_status_poll_count);

	Dictionary fn;
	fn["name"] = AIToolNames::CHECK_BUILD_STATUS;
	fn["arguments"] = "{}";

	Dictionary tool_call;
	tool_call["id"] = call_id;
	tool_call["type"] = "function";
	tool_call["function"] = fn;

	Array tool_calls;
	tool_calls.push_back(tool_call);

	Dictionary assistant_msg;
	assistant_msg["role"] = "assistant";
	assistant_msg["content"] = String();
	assistant_msg["tool_calls"] = tool_calls;
	pending_tool_round.original_messages.push_back(assistant_msg);

	Dictionary result = AIToolExecutor::execute(tool_call);
	pending_tool_round.original_messages.push_back(result);
	_store_structured_history(pending_tool_round.original_messages);
	_save_all_conversations();

	String content = result.get("content", String());
	if (_tool_result_needs_build_poll(content)) {
		tool_call_label->set_text(vformat(TTR("Build is still running. Status check #%d..."), build_status_poll_count));
		status_label->set_text(TTR("Build is still running in the background..."));
		return;
	}

	_stop_build_status_poll();
	tool_call_label->set_visible(false);
	tool_call_label->set_text(String());
	_continue_after_build_poll();
}

void AIChatPanel::_on_build_status_poll_timeout() {
	_append_forced_build_status_check();
}

void AIChatPanel::_continue_after_build_poll() {
	AISettingsData settings = active_settings;
	Array tools = pending_tool_round.original_tools;
	if (tools.is_empty() && settings.tools_enabled) {
		tools = AIToolDefs::get_tools_for_mode(settings.context_mode);
		if (settings.mcp_tools_enabled) {
			Array mcp_tools = AIToolDefs::get_mcp_tools();
			if (!mcp_tools.is_empty()) {
				tools.append_array(mcp_tools);
			}
		}
		pending_tool_round.original_tools = tools;
	}

	in_tool_loop = true;
	chat_service->configure(settings);
	_set_requesting(true);
	Error err = chat_service->send_messages(pending_tool_round.original_messages, tools);
	if (err != OK) {
		status_label->set_text(TTR("Build-status continuation failed."));
		in_tool_loop = false;
		_set_requesting(false);
		_add_ai_message(TTR("[Build completed, but the AI continuation request could not start. Please ask the AI to continue or inspect the build status manually.]"), String(), 0.0, 0, 0);
	}
}

void AIChatPanel::_confirm_tool_execute(int p_tool_index) {
	Dictionary p_json = pending_tool_calls_json;

	Array choices = p_json["choices"];
	Dictionary first_choice = choices[0];
	Dictionary msg = first_choice["message"];
	Array tool_calls = msg["tool_calls"];

	// Rebuild messages array using the cached active_settings.
	AISettingsData settings = active_settings;
	Array messages = pending_tool_round.original_messages;

	if (messages.is_empty()) {
		// First round: build from scratch.
		String system_content = settings.system_prompt;

		Dictionary system_msg;
		system_msg["role"] = "system";
		system_msg["content"] = system_content;
		messages.push_back(system_msg);

		Array history = _build_message_history();
		for (int i = 0; i < history.size(); i++) {
			messages.push_back(history[i]);
		}
		history.clear();
	}

	// Add the assistant message with tool_calls.
	Dictionary clean_msg;
	clean_msg["role"] = "assistant";
	if (msg.has("content") && msg["content"].get_type() != Variant::NIL) {
		clean_msg["content"] = msg["content"];
	}
	clean_msg["tool_calls"] = tool_calls;
	messages.push_back(clean_msg);

	// Build tools array.
	Array tools;
	if (settings.tools_enabled) {
		tools = AIToolDefs::get_tools_for_mode(settings.context_mode);
		if (settings.mcp_tools_enabled) {
			Array mcp_tools = AIToolDefs::get_mcp_tools();
			if (!mcp_tools.is_empty()) {
				tools.append_array(mcp_tools);
			}
		}
	}

	if (tool_execution_running) {
		status_label->set_text(TTR("AI tools are already running. Please wait..."));
		return;
	}
	if (tool_execution_thread.is_started()) {
		tool_execution_thread.wait_to_finish();
	}

	tool_execution_messages = messages;
	tool_execution_tools = tools;
	tool_execution_tool_calls = tool_calls;
	tool_execution_build_poll_needed = false;
	tool_execution_cancelled = false;
	tool_execution_running = true;
	in_tool_loop = true;

	tool_call_label->set_text(vformat(TTR("Executing %d tool(s)..."), tool_calls.size()));
	status_label->set_text(TTR("Executing AI tools in the background..."));
	_set_requesting(true);

	tool_execution_thread.start(_tool_execution_thread_func, this);
}

void AIChatPanel::_tool_execution_thread_func(void *p_userdata) {
	AIChatPanel *panel = static_cast<AIChatPanel *>(p_userdata);
	Array tool_calls = panel->tool_execution_tool_calls;
	Array messages = panel->tool_execution_messages;
	bool build_poll_needed = false;
	for (int i = 0; i < tool_calls.size(); i++) {
		Dictionary tc = tool_calls[i];
		Dictionary result = AIToolExecutor::execute(tc);
		messages.push_back(result);
		String result_content = result.get("content", String());
		if (panel->_tool_result_needs_build_poll(result_content)) {
			build_poll_needed = true;
		}
	}

	panel->tool_execution_messages = messages;
	panel->tool_execution_build_poll_needed = build_poll_needed;
	callable_mp(panel, &AIChatPanel::_finish_tool_execution_thread).call_deferred();
}

void AIChatPanel::_finish_tool_execution_thread() {
	if (tool_execution_thread.is_started()) {
		tool_execution_thread.wait_to_finish();
	}
	tool_execution_running = false;

	if (tool_execution_cancelled) {
		tool_execution_cancelled = false;
		tool_execution_messages.clear();
		tool_execution_tools.clear();
		tool_execution_tool_calls.clear();
		status_label->set_text(TTR("AI request cancelled."));
		_set_requesting(false);
		return;
	}

	Array messages = tool_execution_messages;
	Array tools = tool_execution_tools;
	const bool build_poll_needed = tool_execution_build_poll_needed;

	pending_tool_round.original_messages = messages;
	pending_tool_round.original_tools = tools;
	in_tool_loop = true;
	_store_structured_history(pending_tool_round.original_messages);
	_save_all_conversations();

	tool_call_label->set_visible(false);
	tool_call_label->set_text(String());

	if (build_poll_needed) {
		_start_build_status_poll();
		return;
	}

	chat_service->configure(active_settings);
	_set_requesting(true);
	Error err = chat_service->send_messages(messages, tools);
	if (err != OK) {
		status_label->set_text(TTR("Tool call continuation failed."));
		in_tool_loop = false;
		_set_requesting(false);
		_add_ai_message(TTR("[Tool calls executed, but continuation failed.]"), String(), 0.0, 0, 0);
	}
}

void AIChatPanel::_confirm_tool_skip(int p_tool_index) {
	// User skipped this tool, continue to next round with empty result.
	Dictionary p_json = pending_tool_calls_json;

	Array choices = p_json["choices"];
	Dictionary first_choice = choices[0];
	Dictionary msg = first_choice["message"];
	Array tool_calls = msg["tool_calls"];

	AISettingsData settings = active_settings;
	Array messages = pending_tool_round.original_messages;

	if (messages.is_empty()) {
		String system_content = settings.system_prompt;
		Dictionary system_msg;
		system_msg["role"] = "system";
		system_msg["content"] = system_content;
		messages.push_back(system_msg);

		Array history = _build_message_history();
		for (int i = 0; i < history.size(); i++) {
			messages.push_back(history[i]);
		}
		history.clear();
	}

	// Add the assistant message.
	Dictionary clean_msg;
	clean_msg["role"] = "assistant";
	if (msg.has("content") && msg["content"].get_type() != Variant::NIL) {
		clean_msg["content"] = msg["content"];
	}
	clean_msg["tool_calls"] = tool_calls;
	messages.push_back(clean_msg);

	// Add skipped tool result.
	Dictionary skipped_result;
	skipped_result["role"] = "tool";
	Dictionary tc = tool_calls[p_tool_index];
	skipped_result["tool_call_id"] = tc.get("id", "");
	skipped_result["content"] = "[Skipped by user]";
	messages.push_back(skipped_result);

	// Build tools array.
	Array tools;
	if (settings.tools_enabled) {
		tools = AIToolDefs::get_tools_for_mode(settings.context_mode);
		if (settings.mcp_tools_enabled) {
			Array mcp_tools = AIToolDefs::get_mcp_tools();
			if (!mcp_tools.is_empty()) {
				tools.append_array(mcp_tools);
			}
		}
	}

	// Save state.
	pending_tool_round.original_messages = messages;
	pending_tool_round.original_tools = tools;
	in_tool_loop = true;

	tool_call_label->set_visible(false);
	tool_call_label->set_text(String());
	status_label->set_text(TTR("Tool skipped. Sending result to AI..."));

	// Continue conversation.
	chat_service->configure(settings);
	_set_requesting(true);
	Error err = chat_service->send_messages(messages, tools);
	if (err != OK) {
		status_label->set_text(TTR("Continuation failed after skip."));
		in_tool_loop = false;
		_set_requesting(false);
	}
}

void AIChatPanel::_confirm_tool_cancel_all() {
	// User cancelled all tool calls.
	status_label->set_text(TTR("Tool execution cancelled by user."));
	in_tool_loop = false;
	_set_requesting(false);
	tool_call_label->set_visible(false);
	tool_call_label->set_text(String());

	// Add a message to indicate cancellation.
	_add_ai_message(TTR("[Tool calls were cancelled by the user.]"), String(), 0.0, 0, 0);
}

void AIChatPanel::_suggestion_accepted(AISuggestionCard *p_card) {
	ERR_FAIL_NULL(p_card);
	const AISuggestion s = p_card->get_suggestion();

	switch (s.type) {
		case AISuggestion::TYPE_SKILL: {
			Vector<AISkillEntry> skills;
			Vector<AIMCPServerEntry> mcp_servers;
			AIToolRegistry::load(skills, mcp_servers);
			skills.push_back(s.skill);
			AIToolRegistry::save(skills, mcp_servers);
			status_label->set_text(vformat(TTR("Skill \"%s\" added."), s.skill.name));
		} break;
		case AISuggestion::TYPE_MCP_SERVER: {
			Vector<AISkillEntry> skills;
			Vector<AIMCPServerEntry> mcp_servers;
			AIToolRegistry::load(skills, mcp_servers);
			mcp_servers.push_back(s.mcp_server);
			AIToolRegistry::save(skills, mcp_servers);
			status_label->set_text(vformat(TTR("MCP server \"%s\" added."), s.mcp_server.name));
		} break;
		case AISuggestion::TYPE_MEMORY: {
			Vector<AIMemoryEntry> entries;
			AIMemoryStore::load(entries);
			entries.push_back(s.memory);
			AIMemoryStore::save(entries);
			status_label->set_text(vformat(TTR("Memory \"%s\" added."), s.memory.title));
		} break;
	}

	int idx = suggestion_cards.find(p_card);
	if (idx >= 0) {
		suggestion_cards.remove_at(idx);
	}
	p_card->queue_free();
	_refresh_bulk_bar();
}

void AIChatPanel::_suggestion_rejected(AISuggestionCard *p_card) {
	ERR_FAIL_NULL(p_card);
	int idx = suggestion_cards.find(p_card);
	if (idx >= 0) {
		suggestion_cards.remove_at(idx);
	}
	p_card->queue_free();
	_refresh_bulk_bar();
}

void AIChatPanel::_add_all_suggestions() {
	// Copy the list since _suggestion_accepted mutates it.
	const Vector<AISuggestionCard *> cards = suggestion_cards;
	for (int i = 0; i < cards.size(); i++) {
		_suggestion_accepted(cards[i]);
	}
}

void AIChatPanel::_dismiss_all_suggestions() {
	const Vector<AISuggestionCard *> cards = suggestion_cards;
	for (int i = 0; i < cards.size(); i++) {
		_suggestion_rejected(cards[i]);
	}
}

void AIChatPanel::_show_suggestions(const Vector<AISuggestion> &p_suggestions) {
	_clear_suggestions();

	for (int i = 0; i < p_suggestions.size(); i++) {
		AISuggestionCard *card = memnew(AISuggestionCard);
		card->setup(p_suggestions[i]);
		card->connect(SNAME("suggestion_accepted"), callable_mp(this, &AIChatPanel::_suggestion_accepted).bind(card));
		card->connect(SNAME("suggestion_rejected"), callable_mp(this, &AIChatPanel::_suggestion_rejected).bind(card));
		message_list->add_child(card);
		suggestion_cards.push_back(card);
	}

	_refresh_bulk_bar();
	message_scroll->set_deferred(SNAME("scroll_vertical"), message_scroll->get_v_scroll_bar()->get_max());
}

void AIChatPanel::_clear_suggestions() {
	for (int i = suggestion_cards.size() - 1; i >= 0; i--) {
		if (suggestion_cards[i]) {
			suggestion_cards[i]->queue_free();
		}
	}
	suggestion_cards.clear();
	_refresh_bulk_bar();
}

void AIChatPanel::_refresh_bulk_bar() {
	const bool show_bulk_bar = suggestion_cards.size() >= 2;
	bulk_action_bar->set_visible(show_bulk_bar);
}

void AIChatPanel::_import_file_selected(const String &p_path) {
	Vector<AISuggestion> suggestions;
	const Error err = AIImporter::preview_from_file(p_path, suggestions);
	if (err != OK || suggestions.is_empty()) {
		status_label->set_text(TTR("No importable entries found in the selected file."));
		return;
	}
	_show_suggestions(suggestions);
	status_label->set_text(vformat(TTR("Imported %d entry(s) from file. Confirm to add."), suggestions.size()));
}

// --- Repair card callbacks ---

void AIChatPanel::_repair_apply_patch(AIRepairCard *p_card) {
	ERR_FAIL_NULL(p_card);
	AIRepairTask task = p_card->get_task();

	// Check dirty worktree before applying
	Vector<String> dirty = AIRepairWorkflow::check_dirty_worktree(task.candidate_files);
	if (!dirty.is_empty()) {
		String warning;
		for (int i = 0; i < dirty.size() && i < 5; i++) {
			if (i > 0) {
				warning += ", ";
			}
			warning += dirty[i];
		}
		if (dirty.size() > 5) {
			warning += vformat(" (+%d more)", dirty.size() - 5);
		}
		p_card->set_dirty_warning(TTR("Unrelated files have uncommitted changes:") + " " + warning);
		status_label->set_text(TTR("Dirty worktree detected. Review warnings before applying the patch."));
		return;
	}

	// Step 1: Download remote source files if needed.
	if (!task.fetch_urls.is_empty() && !task.candidate_files.is_empty()) {
		status_label->set_text(TTR("Fetching remote source files..."));
		Vector<String> fetch_errors;
		Error fetch_err = AICodeFetcher::fetch_files(task.fetch_urls, task.candidate_files, fetch_errors);
		if (fetch_err != OK) {
			String err_detail;
			for (int i = 0; i < fetch_errors.size(); i++) {
				if (i > 0) {
					err_detail += "; ";
				}
				err_detail += fetch_errors[i];
			}
			status_label->set_text(vformat(TTR("Failed to download remote source files: %s"), err_detail));
			return;
		}
	}

	// Step 2: Record pre-patch snapshot (before writing).
	String snapshot = AIRepairWorkflow::record_pre_patch_snapshot(task.candidate_files);
	if (!snapshot.is_empty()) {
		String snapshot_path = EditorPaths::get_singleton()->get_config_dir().path_join("ai_patch_snapshots").path_join(task.id + ".diff");
		Ref<DirAccess> da = DirAccess::create_for_path(snapshot_path.get_base_dir());
		if (da.is_valid()) {
			da->make_dir_recursive(snapshot_path.get_base_dir());
			Ref<FileAccess> f = FileAccess::open(snapshot_path, FileAccess::WRITE);
			if (f.is_valid()) {
				f->store_string(snapshot);
			}
		}
	}

	// Step 3: Actually write the patch to disk (AIPatchApplier).
	if (!task.patch_code.is_empty()) {
		AIPatchApplier::PatchResult result = AIPatchApplier::apply_patch(task, false);
		if (!result.valid) {
			String err_msg = vformat(TTR("Failed to apply patch: %s"), result.error);
			// Attempt rollback.
			if (!result.backup_paths.is_empty()) {
				AIPatchApplier::rollback(result.backup_paths);
				err_msg += TTR(" Changes have been rolled back.");
			}
			status_label->set_text(err_msg);
			return;
		}
		status_label->set_text(vformat(TTR("Patch written to %d file(s)."), result.dirty_files.size()));
	} else {
		status_label->set_text(TTR("No patch code provided. Only snapshot recorded (no files changed)."));
	}

	AIRepairWorkflow::update_task(task.id, AIRepairTask::STATE_APPLIED);
	p_card->setup(task);
	status_label->set_text(vformat(TTR("Patch applied for: %s. Use 'Run Tests' to verify."), task.title));
}

void AIChatPanel::_repair_run_tests(AIRepairCard *p_card) {
	ERR_FAIL_NULL(p_card);
	AIRepairTask task = p_card->get_task();

	String test_cmd = task.test_command;
	if (test_cmd.is_empty()) {
		test_cmd = AIRepairWorkflow::suggest_default_test(task.candidate_files);
		if (test_cmd.is_empty()) {
			status_label->set_text(TTR("No test command available. Please configure one for this task."));
			return;
		}
		status_label->set_text(vformat(TTR("No test command specified. Using default: %s"), test_cmd));
	}

	String output;
	int exit_code = -1;
	Error err = AIRepairWorkflow::run_repair_tests(test_cmd, output, exit_code);

	AIRepairTask::State new_state = (err == OK && exit_code == 0)
		? AIRepairTask::STATE_TESTS_PASSED
		: AIRepairTask::STATE_TESTS_FAILED;

	String error_detail = (new_state == AIRepairTask::STATE_TESTS_FAILED)
		? vformat("Test exited with code %d. %s", exit_code, output.substr(0, 256))
		: String();

	AIRepairWorkflow::update_task(task.id, new_state, error_detail);
	task.state = new_state;
	task.last_error = error_detail;
	p_card->setup(task);
	p_card->set_test_result(output, exit_code);

	if (new_state == AIRepairTask::STATE_TESTS_PASSED) {
		status_label->set_text(TTR("Tests passed. You can now build the fix."));
	} else {
		status_label->set_text(TTR("Tests failed. Review the output and use 'Ask AI' to continue debugging."));
	}
}

void AIChatPanel::_repair_open_files(AIRepairCard *p_card) {
	ERR_FAIL_NULL(p_card);
	AIRepairTask task = p_card->get_task();

	int opened = 0;
	for (int i = 0; i < task.candidate_files.size(); i++) {
		String path = task.candidate_files[i];
		// Resolve relative paths against project root
		if (path.is_relative_path()) {
			path = OS::get_singleton()->get_executable_path().get_base_dir().path_join(path);
		}
		if (FileAccess::exists(path)) {
			OS::get_singleton()->shell_open(path);
			opened++;
		}
	}
	status_label->set_text(vformat(TTR("Opened %d of %d candidate file(s) in system editor."), opened, task.candidate_files.size()));
}

void AIChatPanel::_repair_retry_ai(AIRepairCard *p_card) {
	ERR_FAIL_NULL(p_card);
	AIRepairTask task = p_card->get_task();

	// Build a feedback message for AI to continue debugging
	String feedback;
	feedback += "The fix for \"" + task.title + "\" was applied but ";
	if (task.state == AIRepairTask::STATE_TESTS_FAILED) {
		feedback += "tests failed:\n";
		feedback += task.last_error + "\n\n";
	} else if (task.state == AIRepairTask::STATE_BUILD_FAILED) {
		feedback += "the build failed:\n";
		feedback += task.last_error + "\n\n";
	}
	feedback += "Please analyze the failure and suggest an updated fix for:\n";
	feedback += "- Files: ";
	for (int i = 0; i < task.candidate_files.size(); i++) {
		if (i > 0) {
			feedback += ", ";
		}
		feedback += task.candidate_files[i];
	}
	feedback += "\n- Root cause: " + task.root_cause;
	feedback += "\n- Reproduction: " + task.reproduction;

	// Send as user message to AI by setting input text and triggering send
	input->set_text(feedback);
	_send_message();
	status_label->set_text(TTR("Sent failure feedback to AI for further analysis."));
}

void AIChatPanel::_repair_skip(AIRepairCard *p_card) {
	ERR_FAIL_NULL(p_card);
	int idx = repair_cards.find(p_card);
	if (idx >= 0) {
		repair_cards.remove_at(idx);
	}
	p_card->queue_free();
	status_label->set_text(TTR("Repair task dismissed."));
}

void AIChatPanel::_repair_rebuild(AIRepairCard *p_card) {
	ERR_FAIL_NULL(p_card);
	AIRepairTask task = p_card->get_task();
	AIRepairWorkflow::update_task(task.id, AIRepairTask::STATE_BUILD_TRIGGERED);
	p_card->setup(task);

	// Write build request and launch PackageBuilder
	AIBuildBridge::write_build_request();
	Error err = AIBuildBridge::launch_package_builder();
	if (err != OK) {
		status_label->set_text(vformat(TTR("Could not launch PackageBuilder. Please run it manually from %s"), AIBuildBridge::detect_repo_root().path_join("tools/PackageBuilder")));
	} else {
		status_label->set_text(TTR("PackageBuilder launched. Complete the build and click 'Publish' when ready."));
		// Set current task info for post-restart question.
		AINewBuildNotifier::get_singleton()->set_current_task(task.id, task.patch_summary);
		// Start polling for build completion �?will show restart dialog when ready.
		AINewBuildNotifier::start_polling();
	}
}

void AIChatPanel::_repair_publish(AIRepairCard *p_card) {
	ERR_FAIL_NULL(p_card);
	AIRepairTask task = p_card->get_task();
	AIRepairWorkflow::update_task(task.id, AIRepairTask::STATE_PUBLISHED);
	p_card->setup(task);
	status_label->set_text(vformat(TTR("Publish request sent for: %s."), task.title));
}

void AIChatPanel::send_post_restart_question() {
	// Ensure AI Chat is visible.
	TabContainer *parent_tabs = Object::cast_to<TabContainer>(get_parent());
	if (parent_tabs) {
		parent_tabs->set_current_tab(get_index());

		EditorDock *dock = Object::cast_to<EditorDock>(parent_tabs->get_parent());
		if (dock) {
			dock->make_visible();
		} else {
			parent_tabs->show();
		}
	}
	show();

	// Build the question message.
	String question = TTR("The previous AI repair build has finished. Is the feature working correctly now? Are there any errors?");

	// Add AI message to the chat.
	_add_ai_message(question, String(), 0.0, 0, 0);

	// Cleanup the state file after sending question.
	AIRestartHelper::cleanup_state_file();

	status_label->set_text(TTR("Post-restart verification question sent."));
}

void AIChatPanel::_show_repair_tasks(const Vector<AIRepairSuggestion> &p_repairs) {
	_clear_repair_cards();

	for (int i = 0; i < p_repairs.size(); i++) {
		const AIRepairSuggestion &suggestion = p_repairs[i];
		AIRepairTask task;
		task.id = "repair-" + itos(static_cast<int64_t>(OS::get_singleton()->get_unix_time())) + "-" + itos(i);
		task.issue_type = suggestion.issue_type;
		task.title = suggestion.title;
		task.reproduction = suggestion.reproduction;
		task.root_cause = suggestion.root_cause;
		task.candidate_files = suggestion.candidate_files;
		task.patch_summary = suggestion.patch_summary;
		task.patch_type = suggestion.patch_type;
		task.patch_code = suggestion.patch_code;
		task.test_command = suggestion.test_command;
		task.risk = suggestion.risk;
		task.state = AIRepairTask::STATE_PENDING;
		task.created_at = itos(static_cast<int64_t>(OS::get_singleton()->get_unix_time()));

		AIRepairWorkflow::append(task);

		AIRepairCard *card = memnew(AIRepairCard);
		card->setup(task);
		card->connect(SNAME("apply_patch"), callable_mp(this, &AIChatPanel::_repair_apply_patch).bind(card));
		card->connect(SNAME("run_tests"), callable_mp(this, &AIChatPanel::_repair_run_tests).bind(card));
		card->connect(SNAME("open_files"), callable_mp(this, &AIChatPanel::_repair_open_files).bind(card));
		card->connect(SNAME("retry_with_ai"), callable_mp(this, &AIChatPanel::_repair_retry_ai).bind(card));
		card->connect(SNAME("skip_task"), callable_mp(this, &AIChatPanel::_repair_skip).bind(card));
		card->connect(SNAME("rebuild_requested"), callable_mp(this, &AIChatPanel::_repair_rebuild).bind(card));
		card->connect(SNAME("publish_requested"), callable_mp(this, &AIChatPanel::_repair_publish).bind(card));
		message_list->add_child(card);
		repair_cards.push_back(card);
	}

	message_scroll->set_deferred(SNAME("scroll_vertical"), message_scroll->get_v_scroll_bar()->get_max());
	status_label->set_text(vformat(TTR("AI found %d repair task(s). Review the suggested patch and tests before applying."), p_repairs.size()));
}

void AIChatPanel::_clear_repair_cards() {
	for (int i = repair_cards.size() - 1; i >= 0; i--) {
		if (repair_cards[i]) {
			repair_cards[i]->queue_free();
		}
	}
	repair_cards.clear();
}

AIChatPanel::AIChatPanel() {
	set_name(TTRC("Chat"));
	add_theme_constant_override("margin_left", 0);
	add_theme_constant_override("margin_top", 0);
	add_theme_constant_override("margin_right", 0);
	add_theme_constant_override("margin_bottom", 0);

	VBoxContainer *root = memnew(VBoxContainer);
	root->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	root->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	root->add_theme_constant_override("separation", 0);
	add_child(root);

	// Top-level split layout: conversation sidebar + chat area.
	HSplitContainer *split = memnew(HSplitContainer);
	split->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	split->set_split_offset(218 * EDSCALE);
	split->set_touch_dragger_enabled(true);
	root->add_child(split);

	// Sidebar with conversation list.
	sidebar_panel = memnew(PanelContainer);
	sidebar_panel->set_custom_minimum_size(Size2(190 * EDSCALE, 0));
	split->add_child(sidebar_panel);

	sidebar = memnew(VBoxContainer);
	sidebar->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	sidebar->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	sidebar->add_theme_constant_override("separation", 10 * EDSCALE);
	sidebar_panel->add_child(sidebar);

	Label *sidebar_title = memnew(Label);
	sidebar_title->set_text(TTR("Jundot AI"));
	sidebar_title->add_theme_font_size_override("font_size", 16 * EDSCALE);
	sidebar_title->add_theme_color_override("font_color", Color(0.75f, 0.75f, 0.75f));
	sidebar->add_child(sidebar_title);

	HBoxContainer *sidebar_buttons = memnew(HBoxContainer);
	sidebar_buttons->add_theme_constant_override("separation", 4 * EDSCALE);
	sidebar->add_child(sidebar_buttons);

	new_conversation_button = memnew(Button);
	new_conversation_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	new_conversation_button->set_flat(true);
	new_conversation_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_new_conversation));
	sidebar_buttons->add_child(new_conversation_button);

	delete_conversation_button = memnew(Button);
	delete_conversation_button->set_flat(true);
	delete_conversation_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_delete_current_conversation));
	sidebar_buttons->add_child(delete_conversation_button);

	conversation_list = memnew(ItemList);
	conversation_list->set_auto_height(false);
	conversation_list->set_fixed_icon_size(Size2(0, 0));
	conversation_list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	conversation_list->set_allow_reselect(true);
	conversation_list->connect("item_selected", callable_mp(this, &AIChatPanel::_conversation_selected));
	sidebar->add_child(conversation_list);

	chat_surface_panel = memnew(PanelContainer);
	chat_surface_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	chat_surface_panel->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	chat_surface_panel->set_custom_minimum_size(Size2(420 * EDSCALE, 0));
	split->add_child(chat_surface_panel);

	// Chat area (the right side) reuses most of the original root layout.
	VBoxContainer *chat_vbox = memnew(VBoxContainer);
	chat_vbox->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	chat_vbox->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	chat_vbox->add_theme_constant_override("separation", 0);
	chat_surface_panel->add_child(chat_vbox);

	MarginContainer *top_bar_margin = memnew(MarginContainer);
	top_bar_margin->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	top_bar_margin->add_theme_constant_override("margin_left", 16 * EDSCALE);
	top_bar_margin->add_theme_constant_override("margin_right", 16 * EDSCALE);
	top_bar_margin->add_theme_constant_override("margin_top", 6 * EDSCALE);
	top_bar_margin->add_theme_constant_override("margin_bottom", 4 * EDSCALE);
	chat_vbox->add_child(top_bar_margin);

	HBoxContainer *top_bar = memnew(HBoxContainer);
	top_bar->set_custom_minimum_size(Size2(0, 46) * EDSCALE);
	top_bar->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	top_bar->add_theme_constant_override("separation", 8 * EDSCALE);
	top_bar_margin->add_child(top_bar);

	VBoxContainer *title_box = memnew(VBoxContainer);
	title_box->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	title_box->add_theme_constant_override("separation", 0);
	top_bar->add_child(title_box);

	Label *title_label = memnew(Label);
	title_label->set_text(TTR("AI Assistant"));
	title_label->add_theme_font_size_override("font_size", 16 * EDSCALE);
	title_box->add_child(title_label);

	Label *subtitle_label = memnew(Label);
	subtitle_label->set_text(TTR("Chat, inspect code, run tools"));
	subtitle_label->add_theme_font_size_override("font_size", 11 * EDSCALE);
	subtitle_label->set_modulate(Color(1, 1, 1, 0.62f));
	title_box->add_child(subtitle_label);

	Control *top_spacer = memnew(Control);
	top_spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	top_bar->add_child(top_spacer);

	// Mode switching bar (ENGINE / PROJECT)
	mode_bar = memnew(HBoxContainer);
	mode_bar->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	mode_bar->add_theme_constant_override("separation", 6 * EDSCALE);
	top_bar->add_child(mode_bar);

	engine_mode_btn = memnew(Button);
	engine_mode_btn->set_flat(true);
	engine_mode_btn->set_text(TTR("Engine Mode"));
	engine_mode_btn->set_tooltip_text(TTR("Switch to engine source context mode."));
	engine_mode_btn->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_switch_to_engine));
	mode_bar->add_child(engine_mode_btn);

	project_mode_btn = memnew(Button);
	project_mode_btn->set_flat(true);
	project_mode_btn->set_text(TTR("Project Mode"));
	project_mode_btn->set_tooltip_text(TTR("Switch to game project context mode."));
	project_mode_btn->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_switch_to_project));
	mode_bar->add_child(project_mode_btn);

	message_scroll = memnew(ScrollContainer);
	message_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	message_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	chat_vbox->add_child(message_scroll);

	MarginContainer *message_margin = memnew(MarginContainer);
	message_margin->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	message_margin->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	message_margin->add_theme_constant_override("margin_left", 20 * EDSCALE);
	message_margin->add_theme_constant_override("margin_right", 20 * EDSCALE);
	message_margin->add_theme_constant_override("margin_top", 8 * EDSCALE);
	message_margin->add_theme_constant_override("margin_bottom", 8 * EDSCALE);
	message_scroll->add_child(message_margin);

	message_list = memnew(VBoxContainer);
	message_list->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	message_list->add_theme_constant_override("separation", 12 * EDSCALE);
	message_margin->add_child(message_list);

	// Bulk action bar
	bulk_action_bar = memnew(HBoxContainer);
	bulk_action_bar->add_theme_constant_override("separation", 6 * EDSCALE);
	bulk_action_bar->set_visible(false);
	chat_vbox->add_child(bulk_action_bar);

	add_all_button = memnew(Button);
	add_all_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_add_all_suggestions));
	bulk_action_bar->add_child(add_all_button);

	dismiss_all_button = memnew(Button);
	dismiss_all_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_dismiss_all_suggestions));
	bulk_action_bar->add_child(dismiss_all_button);

	// Tool call label
	tool_call_label = memnew(Label);
	tool_call_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	tool_call_label->set_visible(false);
	tool_call_label->set_custom_minimum_size(Size2(0, 20) * EDSCALE);
	tool_call_label->add_theme_color_override("font_color", Color(0.4f, 0.6f, 1.0f));
	tool_call_label->set_modulate(Color(1, 1, 1, 0.85f));
	chat_vbox->add_child(tool_call_label);

	HBoxContainer *tool_limit_toggle_row = memnew(HBoxContainer);
	tool_limit_toggle_row->set_alignment(BoxContainer::ALIGNMENT_CENTER);
	chat_vbox->add_child(tool_limit_toggle_row);

	tool_limit_toggle_button = memnew(Button);
	tool_limit_toggle_button->set_custom_minimum_size(Size2(42, 36) * EDSCALE);
	tool_limit_toggle_button->set_visible(false);
	tool_limit_toggle_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_set_tool_limit_options_collapsed).bind(false));
	tool_limit_toggle_row->add_child(tool_limit_toggle_button);

	tool_limit_options_panel = memnew(PanelContainer);
	tool_limit_options_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	tool_limit_options_panel->set_visible(false);
	chat_vbox->add_child(tool_limit_options_panel);

	VBoxContainer *tool_limit_options_vb = memnew(VBoxContainer);
	tool_limit_options_vb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	tool_limit_options_vb->add_theme_constant_override("separation", 8 * EDSCALE);
	tool_limit_options_panel->add_child(tool_limit_options_vb);

	tool_limit_options_title = memnew(Label);
	tool_limit_options_title->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	tool_limit_options_title->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	tool_limit_options_vb->add_child(tool_limit_options_title);

	next_question_options_box = memnew(VBoxContainer);
	next_question_options_box->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	next_question_options_box->add_theme_constant_override("separation", 6 * EDSCALE);
	next_question_options_box->set_visible(false);
	tool_limit_options_vb->add_child(next_question_options_box);

	tool_limit_continue_button = memnew(Button);
	tool_limit_continue_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	tool_limit_continue_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_continue_after_tool_limit));
	tool_limit_options_vb->add_child(tool_limit_continue_button);

	tool_limit_custom_button = memnew(Button);
	tool_limit_custom_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	tool_limit_custom_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_focus_custom_tool_limit_message));
	tool_limit_options_vb->add_child(tool_limit_custom_button);

	HBoxContainer *tool_limit_bottom_row = memnew(HBoxContainer);
	tool_limit_bottom_row->add_theme_constant_override("separation", 6 * EDSCALE);
	tool_limit_options_vb->add_child(tool_limit_bottom_row);

	tool_limit_stop_button = memnew(Button);
	tool_limit_stop_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	tool_limit_stop_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_dismiss_tool_limit_options));
	tool_limit_bottom_row->add_child(tool_limit_stop_button);

	tool_limit_collapse_button = memnew(Button);
	tool_limit_collapse_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	tool_limit_collapse_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_set_tool_limit_options_collapsed).bind(true));
	tool_limit_bottom_row->add_child(tool_limit_collapse_button);

	// === Composer panel ===
	MarginContainer *composer_outer = memnew(MarginContainer);
	composer_outer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	composer_outer->add_theme_constant_override("margin_left", 20 * EDSCALE);
	composer_outer->add_theme_constant_override("margin_right", 20 * EDSCALE);
	composer_outer->add_theme_constant_override("margin_top", 6 * EDSCALE);
	composer_outer->add_theme_constant_override("margin_bottom", 10 * EDSCALE);
	chat_vbox->add_child(composer_outer);

	composer_panel = memnew(PanelContainer);
	composer_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	composer_panel->set_custom_minimum_size(Size2(0, 118) * EDSCALE);
	composer_outer->add_child(composer_panel);

	VBoxContainer *composer_vb = memnew(VBoxContainer);
	composer_vb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	composer_vb->add_theme_constant_override("separation", 5 * EDSCALE);
	composer_panel->add_child(composer_vb);

	HBoxContainer *attachment_row = memnew(HBoxContainer);
	composer_vb->add_child(attachment_row);

	attachment_chips = memnew(HBoxContainer);
	attachment_chips->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	attachment_chips->add_theme_constant_override("separation", 4 * EDSCALE);
	attachment_row->add_child(attachment_chips);

	input = memnew(TextEdit);
	input->set_custom_minimum_size(Size2(0, 52) * EDSCALE);
	input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	input->set_drag_forwarding(Callable(), callable_mp(this, &AIChatPanel::_can_drop_files_fw).bind(input), callable_mp(this, &AIChatPanel::_drop_files_fw).bind(input));
	composer_vb->add_child(input);

	HBoxContainer *actions = memnew(HBoxContainer);
	actions->add_theme_constant_override("separation", 6 * EDSCALE);
	composer_vb->add_child(actions);

	add_file_menu = memnew(MenuButton);
	add_file_menu->set_flat(true);
	add_file_menu->get_popup()->add_item(TTR("Reference Project File"), FILE_MENU_REFERENCE_PROJECT);
	add_file_menu->get_popup()->add_item(TTR("Upload Text File"), FILE_MENU_UPLOAD_TEXT);
	add_file_menu->get_popup()->add_item(TTR("Import Skill / MCP / Memory..."), FILE_MENU_IMPORT);
	add_file_menu->get_popup()->connect(SceneStringName(id_pressed), callable_mp(this, &AIChatPanel::_add_file_menu_id_pressed));
	actions->add_child(add_file_menu);

	clear_button = memnew(Button);
	clear_button->set_flat(true);
	clear_button->set_tooltip_text(TTR("Clear input and attachments"));
	clear_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_clear_input));
	actions->add_child(clear_button);

	Control *spacer = memnew(Control);
	spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	actions->add_child(spacer);

	cancel_button = memnew(Button);
	cancel_button->set_flat(true);
	cancel_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_cancel_request));
	actions->add_child(cancel_button);

	send_button = memnew(Button);
	send_button->set_flat(false);
	send_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_send_message));
	actions->add_child(send_button);

	// === Status bar ===
	status_label = memnew(Label);
	status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	status_label->set_custom_minimum_size(Size2(0, 22) * EDSCALE);
	status_label->add_theme_font_size_override("font_size", 12 * EDSCALE);
	chat_vbox->add_child(status_label);

	// Initialize mode indicator �?deferred to NOTIFICATION_READY
	// (buttons must be in the scene tree before add_theme_color_override)

	// Chat service
	chat_service = memnew(AIChatService);
	chat_service->connect(SNAME("chat_completed"), callable_mp(this, &AIChatPanel::_chat_completed));
	chat_service->connect(SNAME("chat_stream_data"), callable_mp(this, &AIChatPanel::_chat_stream_data));
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

	import_file_dialog = memnew(EditorFileDialog);
	import_file_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	import_file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
	import_file_dialog->add_filter("*.md,*.json,*.txt", TTRC("Skill / MCP / Memory Files"));
	import_file_dialog->connect(SNAME("file_selected"), callable_mp(this, &AIChatPanel::_import_file_selected));
	add_child(import_file_dialog);

	usage_agreement_dialog = memnew(AIUsageAgreementDialog);
	usage_agreement_dialog->connect(SNAME("agreement_accepted"), callable_mp(this, &AIChatPanel::_usage_agreement_accepted));
	usage_agreement_dialog->connect(SNAME("agreement_rejected"), callable_mp(this, &AIChatPanel::_usage_agreement_rejected));
	add_child(usage_agreement_dialog);

	// Create tool call confirmation dialog.
	tool_confirmation_dialog = memnew(AIToolConfirmationDialog);
	tool_confirmation_dialog->set_callbacks(
			callable_mp(this, &AIChatPanel::_confirm_tool_execute),
			callable_mp(this, &AIChatPanel::_confirm_tool_skip),
			callable_mp(this, &AIChatPanel::_confirm_tool_cancel_all));
	add_child(tool_confirmation_dialog);

	_update_translations();
	_load_all_conversations();
	if (conversations.is_empty()) {
		_new_conversation();
	} else {
		Conversation first = conversations[0];
		active_conversation_id = first.id;
		_load_conversation_to_ui(first);
		_refresh_conversation_list_ui();
	}
	_set_requesting(false);
	_update_auto_chat_display_scale();
	status_label->set_text(TTR("AI assistant ready."));
}
