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
#include "scene/main/http_request.h"

static constexpr int AI_CHAT_ATTACHMENT_MAX_BYTES = 64 * 1024;

void AIChatPanel::_bind_methods() {
}

// ==========================================================================
// Multi-conversation support
// ==========================================================================

Dictionary AIChatPanel::Conversation::to_dict(const Conversation &p_conv) {
	Dictionary d;
	d["id"] = p_conv.id;
	d["title"] = p_conv.title;
	d["created_at"] = (int64_t)p_conv.created_at;
	d["updated_at"] = (int64_t)p_conv.updated_at;

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
	conv.created_at = (uint64_t)p_dict.get("created_at", 0);
	conv.updated_at = (uint64_t)p_dict.get("updated_at", 0);

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
		conversation_list->add_item(title);
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
		// think_content / token stats are not directly exposed on AIChatMessage;
		// they are stored in the message's own private fields and restored via
		// setup_ai / setup_summary. Here we only capture what we can recover:
		conv.messages.push_back(cm);
	}

	conv.updated_at = Time::get_singleton()->get_unix_time_from_system();

	// Auto-update title based on the first user message if still default.
	static const String default_title = TTR("New Chat");
	if (conv.title.is_empty() || conv.title == default_title) {
		conv.title = _auto_generate_title(conv);
	}
}

void AIChatPanel::_load_conversation_to_ui(const Conversation &p_conv) {
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
	}
	if (p_what == NOTIFICATION_TRANSLATION_CHANGED) {
		_update_translations();
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
	_update_mode_indicator();
	status_label->set_text(TTR("Switched to ENGINE mode �?engine source mode."));
}

void AIChatPanel::_switch_to_project() {
	AISettingsData s = AISettings::load();
	s.context_mode = AIContextMode::PROJECT;
	AISettings::save(s);
	_update_mode_indicator();
	status_label->set_text(TTR("Switched to PROJECT mode �?game project mode."));
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
	input->set_placeholder(TTR("Input message / drop files here"));
	add_file_menu->set_text(TTR("+"));
	add_file_menu->set_tooltip_text(TTR("Attach or reference a text file"));
	PopupMenu *file_popup = add_file_menu->get_popup();
	file_popup->set_item_text(file_popup->get_item_index(FILE_MENU_REFERENCE_PROJECT), TTR("Reference Project File"));
	file_popup->set_item_text(file_popup->get_item_index(FILE_MENU_UPLOAD_TEXT), TTR("Upload Text File"));
	file_popup->set_item_text(file_popup->get_item_index(FILE_MENU_IMPORT), TTR("Import Skill / MCP / Memory..."));
	clear_button->set_text(TTR("Clear"));
	cancel_button->set_text(TTR("Cancel"));
	send_button->set_text(TTR("Send"));
	reference_file_dialog->set_title(TTR("Reference Project File"));
	upload_file_dialog->set_title(TTR("Upload Text File"));
	import_file_dialog->set_title(TTR("Import Skill / MCP / Memory"));
	add_all_button->set_text(TTR("Add All"));
	dismiss_all_button->set_text(TTR("Dismiss All"));
	if (new_conversation_button) {
		new_conversation_button->set_text(TTR("New Chat"));
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

void AIChatPanel::_add_user_message(const String &p_text) {
	AIChatMessage *message = memnew(AIChatMessage);
	message->setup_user(p_text);
	message_list->add_child(message);
	message_scroll->set_deferred(SNAME("scroll_vertical"), message_scroll->get_v_scroll_bar()->get_max());
}

void AIChatPanel::_add_ai_message(const String &p_content, const String &p_think_content, double p_think_time, int p_prompt_tokens, int p_completion_tokens) {
	AIChatMessage *message = memnew(AIChatMessage);
	message->setup_ai(p_content, p_think_content, p_think_time, p_prompt_tokens, p_completion_tokens);
	message->connect(SNAME("edit_requested"), callable_mp(this, &AIChatPanel::_on_edit_requested));
	message_list->add_child(message);
	message_scroll->set_deferred(SNAME("scroll_vertical"), message_scroll->get_v_scroll_bar()->get_max());
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
	if (chat_service && chat_service->is_requesting()) {
		status_label->set_text(TTR("Wait for the current AI request to finish before sending another message."));
		return;
	}

	const String text = input->get_text().strip_edges();
	if (text.is_empty()) {
		return;
	}

	AISettingsData settings = AISettings::load();
	if (settings.api_key.is_empty()) {
		status_label->set_text(TTR("Configure an API key before sending AI messages."));
		return;
	}

	if (!_ensure_usage_agreement()) {
		return;
	}

	// In edit mode, update the existing user message in-place and remove
	// all messages after it so the upcoming AI reply overwrites the old one.
	if (editing_message_index >= 0) {
		AIChatMessage *user_msg = Object::cast_to<AIChatMessage>(message_list->get_child(editing_message_index));
		if (user_msg) {
			user_msg->set_content(text);
		}
		for (int i = message_list->get_child_count() - 1; i > editing_message_index; i--) {
			message_list->get_child(i)->queue_free();
		}
		editing_message_index = -1;
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

	for (int i = 0; i < history.size(); i++) {
		Dictionary entry = history[i];
		if (i == history.size() - 1 && String(entry["role"]) == "user") {
			entry["content"] = request_text;
		}
		messages.push_back(entry);
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

	const Error err = chat_service->send_messages(messages, tools);
	if (err != OK) {
		status_label->set_text(TTR("AI request could not start."));
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
	// Use the first user message (and up to the first AI reply if available)
	// to ask the AI for a short conversation title. The request is sent as
	// a low-cost chat call; the result overwrites conversation_name_edit.

	// If the user already manually typed a title since we queued the request,
	// skip the auto-title call.
	if (conversation_name_edit && !conversation_name_edit->get_text().strip_edges().is_empty()) {
		has_auto_titled = true;
		return;
	}

	Array title_messages;

	// System prompt: short title summariser.
	{
		Dictionary sys;
		sys["role"] = "system";
		sys["content"] = TTR("You are a conversation title generator. Generate a very short title (under 12 words) that summarises the user's question or goal. Respond with ONLY the title text: no quotes, no prefix, no explanation.");
		title_messages.push_back(sys);
	}

	// Build a compact representation of the first exchange.
	String first_user_text = p_user_first_message;
	String first_ai_text;

	// Prefer the first assistant message from the conversation history if
	// available (p_history contains the last round).
	for (int i = 0; i < p_history.size(); i++) {
		Dictionary entry = p_history[i];
		String role = entry["role"];
		String content = entry["content"];
		if (role == "assistant") {
			first_ai_text = content;
			break;
		}
	}

	// Compose the title query: the user message plus, optionally, a short
	// snippet of the AI reply for better context.
	String title_body = vformat(TTR("User: %s"), first_user_text);
	if (!first_ai_text.is_empty()) {
		// Take the first ~300 chars of the reply to keep it cheap.
		String ai_snippet = first_ai_text;
		if (ai_snippet.length() > 300) {
			ai_snippet = ai_snippet.substr(0, 300) + "...";
		}
		title_body += "\n\n" + vformat(TTR("Assistant: %s"), ai_snippet);
	}

	Dictionary user_msg;
	user_msg["role"] = "user";
	user_msg["content"] = title_body;
	title_messages.push_back(user_msg);

	// Re-load settings with a low token budget and deterministic temperature.
	AISettingsData title_settings = AISettings::load();
	title_settings.max_tokens = 64;
	title_settings.temperature = 0.3;
	chat_service->configure(title_settings);

	is_titling = true;
	title_request_conversation_id = active_conversation_id;
	const Error err = chat_service->send_messages(title_messages);
	if (err != OK) {
		// Silent fallback: leave the title empty.
		is_titling = false;
		title_request_conversation_id = String();
	}
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
	if (streaming_message) {
		streaming_message->queue_free();
		streaming_message = nullptr;
	}
	pending_tool_round = PendingToolRound();
	in_tool_loop = false;
	request_conversation_id = String();
	status_label->set_text(TTR("AI request cancelled."));
	_set_requesting(false);
}

void AIChatPanel::_clear_messages() {
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
		streaming_message->setup_ai(p_full_content, String(), 0.0, 0, p_completion_tokens);
		streaming_message->connect(SNAME("edit_requested"), callable_mp(this, &AIChatPanel::_on_edit_requested));
		message_list->add_child(streaming_message);
	} else {
		streaming_message->set_markdown_content(p_full_content);
	}
	message_scroll->set_deferred(SNAME("scroll_vertical"), message_scroll->get_v_scroll_bar()->get_max());
}

bool AIChatPanel::_looks_like_tool_preamble(const String &p_content) const {
	if (!active_settings.tools_enabled || pending_tool_round.original_tools.is_empty() || pending_tool_round.executed_tool_calls || in_tool_loop) {
		return false;
	}

	const String text = p_content.strip_edges();
	if (text.is_empty()) {
		return false;
	}

	const String lower = text.to_lower();
	if (lower.contains("read_files") || lower.contains("write_file") || lower.contains("search_files") ||
			lower.contains("grep_code") || lower.contains("run_build") || lower.contains("tool call")) {
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

bool AIChatPanel::_retry_after_missing_tool_call(const String &p_content) {
	if (pending_tool_round.missing_tool_retry_used || pending_tool_round.original_messages.is_empty() || pending_tool_round.original_tools.is_empty()) {
		return false;
	}

	pending_tool_round.missing_tool_retry_used = true;

	Array messages = pending_tool_round.original_messages.duplicate(true);

	Dictionary assistant_msg;
	assistant_msg["role"] = "assistant";
	assistant_msg["content"] = p_content;
	messages.push_back(assistant_msg);

	Dictionary correction_msg;
	correction_msg["role"] = "user";
	correction_msg["content"] = TTR("Your previous response described using a tool, but it did not include a structured tool_calls response, so the editor could not execute anything. Do not describe the tool use in prose. Return the appropriate function call now using the available tools. If you need to read a file, call read_files with the exact relative path.");
	messages.push_back(correction_msg);

	pending_tool_round.original_messages = messages.duplicate(true);

	tool_call_label->set_visible(true);
	tool_call_label->set_text(TTR("AI described a tool action without calling it. Retrying once with a stricter tool-call instruction..."));
	status_label->set_text(TTR("Retrying AI request for a real tool call..."));

	chat_service->configure(active_settings);
	_set_requesting(true);
	Error err = chat_service->send_messages(messages, pending_tool_round.original_tools);
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
					if (pending_tool_round.iteration_count >= pending_tool_round.max_iterations) {
						if (streaming_message) {
							streaming_message->queue_free();
							streaming_message = nullptr;
						}
						_add_ai_message(vformat(TTR("Maximum tool call iterations (%d) reached. Stopping here to prevent an infinite loop. You can continue manually by asking the AI to proceed."),
								pending_tool_round.max_iterations),
								String(), 0.0, p_prompt_tokens, p_completion_tokens);
						tool_call_label->set_visible(false);
						tool_call_label->set_text(String());
						in_tool_loop = false;
						status_label->set_text(TTR("AI tool iteration limit reached."));
						_serialize_current_messages();
						_refresh_conversation_list_ui();
						_save_all_conversations();
						request_conversation_id = String();
						return;
					}
					if (streaming_message) {
						streaming_message->queue_free();
						streaming_message = nullptr;
					}
					_execute_tool_calls(p_json);
					return;
				}
			}
		}

		if (_looks_like_tool_preamble(p_content)) {
			if (_retry_after_missing_tool_call(p_content)) {
				return;
			}
		}

		String final_content = p_content;
		if (_looks_like_tool_preamble(p_content) && pending_tool_round.missing_tool_retry_used && !pending_tool_round.executed_tool_calls) {
			final_content += TTR("\n\n[Tool calling did not run: the model returned normal text instead of a structured tool_calls response. Use a model or API endpoint that supports OpenAI-compatible function calling, and make sure Function Calling tools are enabled.]");
		}

		if (streaming_message) {
			streaming_message->setup_ai(final_content, p_think_content, p_elapsed_seconds, p_prompt_tokens, p_completion_tokens);
			streaming_message = nullptr;
		} else {
			_add_ai_message(final_content, p_think_content, p_elapsed_seconds, p_prompt_tokens, p_completion_tokens);
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
		_serialize_current_messages();
		_refresh_conversation_list_ui();
		_save_all_conversations();
		request_conversation_id = String();

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
	_add_ai_message(error_text, String(), 0.0, 0, 0);
	_serialize_current_messages();
	_refresh_conversation_list_ui();
	_save_all_conversations();
	request_conversation_id = String();
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
		String args_preview = fn.get("arguments", "{}");
		// Truncate long arguments for display.
		if (args_preview.length() > 80) {
			args_preview = args_preview.substr(0, 77) + "...";
		}
		if (i > 0) {
			tool_display += "\n";
		}
		tool_display += vformat(TTR("[Round %d/%d] \xe2\x9a\x99 %s(%s)"),
				current_iteration, max_iterations, name, args_preview);
	}
	tool_call_label->set_visible(true);
	tool_call_label->set_text(tool_display);

	// Execute all requested tool calls immediately.
	_confirm_tool_execute(0);
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

	// Execute each tool call and append results.
	for (int i = 0; i < tool_calls.size(); i++) {
		Dictionary tc = tool_calls[i];
		Dictionary result = AIToolExecutor::execute(tc);
		messages.push_back(result);
	}

	// Save state for continuation.
	pending_tool_round.original_messages = messages;
	pending_tool_round.original_tools = tools;
	in_tool_loop = true;

	// Update status.
	int current_iteration = pending_tool_round.iteration_count;
	int max_iterations = pending_tool_round.max_iterations;
	tool_call_label->set_text(vformat(TTR("Executed %d tool(s). Waiting for response..."), tool_calls.size()));

	// Send results back to LLM.
	chat_service->configure(settings);
	_set_requesting(true);
	Error err = chat_service->send_messages(messages, tools);
	if (err != OK) {
		status_label->set_text(TTR("Tool call continuation failed."));
		in_tool_loop = false;
		_set_requesting(false);
		String tool_summary = vformat(TTR("[Tool calls executed: %d]"), tool_calls.size());
		_add_ai_message(tool_summary, String(), 0.0, 0, 0);
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

	tool_call_label->set_text(TTR("Tool skipped. Waiting for response..."));
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
	add_theme_constant_override("margin_left", 8 * EDSCALE);
	add_theme_constant_override("margin_top", 8 * EDSCALE);
	add_theme_constant_override("margin_right", 8 * EDSCALE);
	add_theme_constant_override("margin_bottom", 8 * EDSCALE);

	VBoxContainer *root = memnew(VBoxContainer);
	root->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	root->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	root->add_theme_constant_override("separation", 8 * EDSCALE);
	add_child(root);

	// Top-level horizontal layout: conversation sidebar + chat area.
	HBoxContainer *hbox = memnew(HBoxContainer);
	hbox->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	hbox->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	hbox->add_theme_constant_override("separation", 8 * EDSCALE);
	root->add_child(hbox);

	// Sidebar with conversation list.
	sidebar_panel = memnew(PanelContainer);
	sidebar_panel->set_custom_minimum_size(Size2(220 * EDSCALE, 0));
	hbox->add_child(sidebar_panel);

	sidebar = memnew(VBoxContainer);
	sidebar->add_theme_constant_override("separation", 4 * EDSCALE);
	sidebar->add_theme_constant_override("margin_left", 6 * EDSCALE);
	sidebar->add_theme_constant_override("margin_top", 6 * EDSCALE);
	sidebar->add_theme_constant_override("margin_right", 6 * EDSCALE);
	sidebar->add_theme_constant_override("margin_bottom", 6 * EDSCALE);
	sidebar_panel->add_child(sidebar);

	Label *sidebar_title = memnew(Label);
	sidebar_title->set_text(TTR("Conversations"));
	sidebar_title->add_theme_font_size_override("font_size", 14 * EDSCALE);
	sidebar_title->add_theme_color_override("font_color", Color(0.75f, 0.75f, 0.75f));
	sidebar->add_child(sidebar_title);

	HBoxContainer *sidebar_buttons = memnew(HBoxContainer);
	sidebar_buttons->add_theme_constant_override("separation", 4 * EDSCALE);
	sidebar->add_child(sidebar_buttons);

	new_conversation_button = memnew(Button);
	new_conversation_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	new_conversation_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_new_conversation));
	sidebar_buttons->add_child(new_conversation_button);

	delete_conversation_button = memnew(Button);
	delete_conversation_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_delete_current_conversation));
	sidebar_buttons->add_child(delete_conversation_button);

	conversation_list = memnew(ItemList);
	conversation_list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	conversation_list->connect("item_selected", callable_mp(this, &AIChatPanel::_conversation_selected));
	sidebar->add_child(conversation_list);

	// Chat area (the right side) reuses most of the original root layout.
	VBoxContainer *chat_vbox = memnew(VBoxContainer);
	chat_vbox->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	chat_vbox->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	chat_vbox->add_theme_constant_override("separation", 8 * EDSCALE);
	hbox->add_child(chat_vbox);

	// Mode switching bar (ENGINE / PROJECT)
	mode_bar = memnew(HBoxContainer);
	mode_bar->add_theme_constant_override("separation", 6 * EDSCALE);
	chat_vbox->add_child(mode_bar);

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

	message_list = memnew(VBoxContainer);
	message_list->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	message_list->add_theme_constant_override("separation", 10 * EDSCALE);
	message_scroll->add_child(message_list);

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

	// === Composer panel ===
	PanelContainer *composer = memnew(PanelContainer);
	composer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	composer->set_custom_minimum_size(Size2(0, 180) * EDSCALE);
	chat_vbox->add_child(composer);

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
	add_file_menu->get_popup()->add_item(TTR("Reference Project File"), FILE_MENU_REFERENCE_PROJECT);
	add_file_menu->get_popup()->add_item(TTR("Upload Text File"), FILE_MENU_UPLOAD_TEXT);
	add_file_menu->get_popup()->add_item(TTR("Import Skill / MCP / Memory..."), FILE_MENU_IMPORT);
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

	// === Status bar ===
	status_label = memnew(Label);
	status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	status_label->set_custom_minimum_size(Size2(0, 24) * EDSCALE);
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
	status_label->set_text(TTR("AI assistant ready."));
}
