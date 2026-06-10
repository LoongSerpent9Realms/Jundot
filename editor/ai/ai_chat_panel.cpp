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
#include "ai_chat_service.h"
#include "ai_context_builder.h"
#include "ai_importer.h"
#include "ai_memory_store.h"
#include "ai_settings.h"
#include "ai_repair_card.h"
#include "ai_repair_workflow.h"
#include "ai_suggestion_card.h"
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
		"crash", "leak", "slow", "perf", "性能", "崩溃", "错误", "卡顿", "问题", "BUG", "bug",
		"闪退", "异常", "缺陷", nullptr
	};

	// Feature-related keywords.
	static const char *feature_keywords[] = {
		"feature", "add", "implement", "support", "new", "create", "build",
		"功能", "特性", "新增", "实现", "开发", "添加", "扩展", nullptr
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
	status_label->set_text(TTR("Switched to ENGINE mode — engine source mode."));
}

void AIChatPanel::_switch_to_project() {
	AISettingsData s = AISettings::load();
	s.context_mode = AIContextMode::PROJECT;
	AISettings::save(s);
	_update_mode_indicator();
	status_label->set_text(TTR("Switched to PROJECT mode — game project mode."));
}

void AIChatPanel::_update_mode_indicator() {
	AISettingsData s = AISettings::load();
	if (s.context_mode == AIContextMode::ENGINE) {
		engine_mode_btn->add_theme_color_override("font_color", Color(1.0f, 0.4f, 0.4f));
		project_mode_btn->add_theme_color_override("font_color", Color(0.7f, 0.7f, 0.7f));
	} else {
		project_mode_btn->add_theme_color_override("font_color", Color(0.4f, 1.0f, 0.4f));
		engine_mode_btn->add_theme_color_override("font_color", Color(0.7f, 0.7f, 0.7f));
	}
}

void AIChatPanel::_new_conversation() {
	// Clear current messages
	for (int i = message_list->get_child_count() - 1; i >= 0; i--) {
		message_list->get_child(i)->queue_free();
	}
	conversation_name_edit->set_text("");
	has_auto_titled = false;
	is_titling = false;
	status_label->set_text(TTR("New conversation started."));
}

void AIChatPanel::_update_conversation_menu() {
	if (!conversation_menu) {
		return;
	}
	PopupMenu *popup = conversation_menu->get_popup();
	popup->clear();

	String save_dir = OS::get_singleton()->get_user_data_dir().path_join("ai_conversations");
	Ref<DirAccess> da = DirAccess::open(save_dir);
	if (da.is_null()) {
		return;
	}

	da->list_dir_begin();
	String f = da->get_next();
	while (!f.is_empty()) {
		if (f.ends_with(".json")) {
			String full_path = save_dir.path_join(f);
			Ref<FileAccess> fa = FileAccess::open(full_path, FileAccess::READ);
			if (fa.is_valid()) {
				String content = fa->get_as_text();
				String title = f.trim_suffix(".json");
				JSON json;
				if (json.parse(content) == OK) {
					Array msgs = json.get_data();
					for (int i = 0; i < msgs.size(); i++) {
						Dictionary msg = msgs[i];
						if (msg.get("role", "") == "user") {
							String content_str = msg.get("content", "");
							if (!content_str.is_empty()) {
								title = content_str.substr(0, MIN(40, content_str.length()));
								break;
							}
						}
					}
				}
				popup->add_item(title, popup->get_item_count());
			}
		}
		f = da->get_next();
	}
	da->list_dir_end();
}

void AIChatPanel::_update_conversation_list() {
	conversation_list->clear();
	saved_conversations.clear();

	// Load from user data dir
	String save_dir = OS::get_singleton()->get_user_data_dir().path_join("ai_conversations");
	Ref<DirAccess> da = DirAccess::open(save_dir);
	if (da.is_null()) {
		return;
	}

	da->list_dir_begin();
	String f = da->get_next();
	while (!f.is_empty()) {
		if (f.ends_with(".json")) {
			String full_path = save_dir.path_join(f);
			Ref<FileAccess> fa = FileAccess::open(full_path, FileAccess::READ);
			if (fa.is_valid()) {
				String content = fa->get_as_text();
				// Try to extract a title from the first user message
				String title = f.trim_suffix(".json");
				JSON json;
				if (json.parse(content) == OK) {
					Array msgs = json.get_data();
					for (int i = 0; i < msgs.size(); i++) {
						Dictionary msg = msgs[i];
						if (msg.get("role", "") == "user") {
							String content_str = msg.get("content", "");
							if (!content_str.is_empty()) {
								title = content_str.substr(0, MIN(40, content_str.length()));
								break;
							}
						}
					}
				}
				conversation_list->add_item(title);
				saved_conversations.push_back(Vector<String>()); // placeholder
			}
		}
		f = da->get_next();
	}
	da->list_dir_end();
}

void AIChatPanel::_select_conversation(int p_index) {
	if (p_index < 0 || p_index >= saved_conversations.size()) {
		return;
	}
	String save_dir = OS::get_singleton()->get_user_data_dir().path_join("ai_conversations");
	Ref<DirAccess> da = DirAccess::open(save_dir);
	if (da.is_null()) {
		return;
	}

	da->list_dir_begin();
	String f = da->get_next();
	int idx = 0;
	while (!f.is_empty()) {
		if (f.ends_with(".json") && idx == p_index) {
			String full_path = save_dir.path_join(f);
			_load_conversation(full_path);
			break;
		}
		f = da->get_next();
		if (f.ends_with(".json")) {
			idx++;
		}
	}
	da->list_dir_end();
}

void AIChatPanel::_save_current_conversation() {
	// Save current message_list content to disk.
	String save_dir = OS::get_singleton()->get_user_data_dir().path_join("ai_conversations");
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_RESOURCES);
	if (da.is_null()) {
		return;
	}
	if (!da->dir_exists(save_dir)) {
		da->make_dir_recursive(save_dir);
	}

	String timestamp = Time::get_singleton()->get_datetime_string_from_system().replace(":", "-").replace("T", "_");
	String file_path = save_dir.path_join(timestamp + ".json");

	Ref<FileAccess> fa = FileAccess::open(file_path, FileAccess::WRITE);
	if (fa.is_valid()) {
		// Build JSON from message history (UI children).
		Array history = _build_message_history();
		String json_content = "[\n";
		for (int i = 0; i < history.size(); i++) {
			Dictionary msg = history[i];
			json_content += "{\"role\": \"" + String(msg.get("role", "")) + "\", \"content\": \"";
			String content = msg.get("content", "");
			content = content.replace("\\", "\\\\").replace("\"", "\\\"");
			json_content += content + "\"}\n";
			if (i < history.size() - 1) {
				json_content += ",\n";
			}
		}
		json_content += "]\n";
		fa->store_string(json_content);
		fa->close();
	}
	_update_conversation_list();
}

void AIChatPanel::_load_conversation(const String &p_file_path) {
	Ref<FileAccess> fa = FileAccess::open(p_file_path, FileAccess::READ);
	if (fa.is_null()) {
		return;
	}
	String content = fa->get_as_text();
	fa->close();

	JSON json;
	if (json.parse(content) != OK) {
		return;
	}

	Array msgs = json.get_data();
	// Clear existing UI children.
	for (int i = message_list->get_child_count() - 1; i >= 0; i--) {
		message_list->get_child(i)->queue_free();
	}
	// Re-add messages from loaded data.
	for (int i = 0; i < msgs.size(); i++) {
		Dictionary msg = msgs[i];
		String role = msg.get("role", "");
		String content = msg.get("content", "");
		if (role == "user") {
			_add_user_message(content);
		} else if (role == "assistant") {
			_add_ai_message(content, "", 0.0, 0, 0);
		}
	}

	// Don't auto-title loaded conversations — they are already saved.
	has_auto_titled = true;
	is_titling = false;
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
	_refresh_attachment_chips();
}

void AIChatPanel::_set_requesting(bool p_requesting) {
	send_button->set_disabled(p_requesting);
	cancel_button->set_disabled(!p_requesting);
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

	// Build the full messages array with conversation history.
	Array history = _build_message_history();

	// Auto-title: on the first user message of a conversation with no
	// manually-set title, ask the AI to summarise a conversation title
	// once the normal response has been received.
	bool needs_auto_title = !has_auto_titled &&
			conversation_name_edit->get_text().strip_edges().is_empty() &&
			!is_summarizing && !is_titling;

	// Check if history exceeds the character budget → trigger summarization.
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

	const Error err = chat_service->send_messages(messages, tools);
	if (err != OK) {
		status_label->set_text(TTR("AI request could not start."));
		return;
	}

	input->clear();
	pending_tool_round = PendingToolRound();
	in_tool_loop = false;
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
	if (!conversation_name_edit->get_text().strip_edges().is_empty()) {
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
	const Error err = chat_service->send_messages(title_messages);
	if (err != OK) {
		// Silent fallback: leave the title empty.
		is_titling = false;
	}
}

void AIChatPanel::_title_completed(int p_result, int p_response_code, const String &p_title_content) {
	is_titling = false;

	if (p_result != HTTPRequest::RESULT_SUCCESS || p_response_code >= HTTPClient::RESPONSE_BAD_REQUEST) {
		// Silent failure: leave the title blank for the user to set.
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
	// Take the first line only — some models emit a short paragraph.
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
		if (conversation_name_edit->get_text().strip_edges().is_empty()) {
			conversation_name_edit->set_text(title);
			has_auto_titled = true;
		}
	}
}

void AIChatPanel::_cancel_request() {
	if (is_summarizing) {
		is_summarizing = false;
		pending_user_message = String();
		pending_attachments.clear();
	}
	if (is_titling) {
		is_titling = false;
	}
	chat_service->cancel_request();
	pending_tool_round = PendingToolRound();
	in_tool_loop = false;
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
	conversation_name_edit->set_text("");
	has_auto_titled = false;
	status_label->set_text(TTR("AI assistant ready."));
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
			// Summarization failed — fall back to truncation.
			_summary_completed(String());
		}
		return;
	}

	_set_requesting(false);
	if (p_result == HTTPRequest::RESULT_SUCCESS && p_response_code < HTTPClient::RESPONSE_BAD_REQUEST) {
		// Check if the response contains tool_calls (Function Calling).
		if (p_json.has("choices") && p_json["choices"].get_type() == Variant::ARRAY) {
			Array choices = p_json["choices"];
			if (choices.size() > 0 && choices[0].get_type() == Variant::DICTIONARY) {
				Dictionary first = choices[0];
				// Prefer finish_reason check for robustness; fall back to
				// inspecting message.tool_calls for providers that omit it.
				String finish_reason = first.get("finish_reason", String());
				Dictionary msg = first.get("message", Dictionary());
				bool has_tool_calls = (finish_reason == "tool_calls") ||
					(msg.has("tool_calls") && msg["tool_calls"].get_type() == Variant::ARRAY &&
						!((Array)msg["tool_calls"]).is_empty());
				if (has_tool_calls) {
					_execute_tool_calls(p_json);
					return;
				}
			}
		}

		_add_ai_message(p_content, p_think_content, p_elapsed_seconds, p_prompt_tokens, p_completion_tokens);

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

		// Parse suggestions from the AI response.
		const AISettingsData settings = AISettings::load();
		if (settings.auto_suggest_entries) {
			Vector<AISuggestion> suggestions;
			AIChatParser::parse(p_content, suggestions);
			if (!suggestions.is_empty()) {
				_show_suggestions(suggestions);
			}

			Vector<AIFeatureGateResult> feature_gates;
			AIChatParser::parse_feature_gates(p_content, feature_gates);
			for (int i = 0; i < feature_gates.size(); i++) {
				AIFeatureGateResult gate = feature_gates[i];
				AIFeatureGate::evaluate(gate, settings);
				_add_ai_message(AIFeatureGate::build_report(gate), String(), 0.0, 0, 0);
			}

			Vector<AIRepairSuggestion> repair_tasks;
			AIChatParser::parse_repair_tasks(p_content, repair_tasks);
			if (!repair_tasks.is_empty()) {
				_show_repair_tasks(repair_tasks);
			}
		}
		status_label->set_text(TTR("AI response received."));
		return;
	}

	String error_text = p_content;
	if (error_text.is_empty()) {
		error_text = vformat(TTR("AI request failed. HTTP %d."), p_response_code);
	}
	_add_ai_message(error_text, String(), 0.0, 0, 0);
	status_label->set_text(error_text);
}

void AIChatPanel::_execute_tool_calls(const Dictionary &p_json) {
	Array choices = p_json["choices"];
	Dictionary first_choice = choices[0];
	Dictionary msg = first_choice["message"];
	Array tool_calls = msg["tool_calls"];

	// Rebuild messages array. If continuing from a previous tool round,
	// reuse the accumulated messages to preserve tool results history.
	// Use the cached active settings so auto_mode_prompt, context, and
	// user_extra_instructions from _send_message() are preserved.
	AISettingsData settings = active_settings;
	Array messages = pending_tool_round.original_messages;

	if (messages.is_empty()) {
		// First round: build from scratch using the cached active_settings.
		// active_settings.system_prompt already contains the auto_mode_prompt,
		// context, and user_extra_instructions from _send_message(), so we
		// do NOT rebuild context here to avoid duplication.
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

	// Add the assistant message with tool_calls (may have null content).
	Dictionary clean_msg;
	clean_msg["role"] = "assistant";
	if (msg.has("content") && msg["content"].get_type() != Variant::NIL) {
		clean_msg["content"] = msg["content"];
	}
	clean_msg["tool_calls"] = tool_calls;
	messages.push_back(clean_msg);

	// Show status.
	status_label->set_text(vformat(TTR("AI is using %d tool(s)..."), tool_calls.size()));

	// Build tool call display.
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
		tool_display += vformat("\xe2\x9a\x99 %s(%s)", name, args_preview);
	}
	tool_call_label->set_visible(true);
	tool_call_label->set_text(tool_display);

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

	// Save state for continuation on the next round.
	pending_tool_round.original_messages = messages;
	pending_tool_round.original_tools = tools;
	in_tool_loop = true;

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
		// Start polling for build completion — will show restart dialog when ready.
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

	// === Top bar (mode switch + conversation selector) ===
	HBoxContainer *top_bar = memnew(HBoxContainer);
	top_bar->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	top_bar->add_theme_constant_override("separation", 8 * EDSCALE);
	root->add_child(top_bar);

	// Left: Engine/Project mode buttons
	mode_bar = memnew(HBoxContainer);
	mode_bar->add_theme_constant_override("separation", 4 * EDSCALE);
	top_bar->add_child(mode_bar);

	engine_mode_btn = memnew(Button);
	engine_mode_btn->set_text(TTR("Engine"));
	engine_mode_btn->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_switch_to_engine));
	mode_bar->add_child(engine_mode_btn);

	project_mode_btn = memnew(Button);
	project_mode_btn->set_text(TTR("Project"));
	project_mode_btn->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_switch_to_project));
	mode_bar->add_child(project_mode_btn);

	// Spacer
	Control *top_spacer = memnew(Control);
	top_spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	top_bar->add_child(top_spacer);

	// Right: Conversation selector
	conversation_bar = memnew(HBoxContainer);
	conversation_bar->add_theme_constant_override("separation", 4 * EDSCALE);
	top_bar->add_child(conversation_bar);

	conversation_name_edit = memnew(LineEdit);
	conversation_name_edit->set_placeholder(TTR("Conversation Name"));
	conversation_name_edit->set_custom_minimum_size(Size2(150 * EDSCALE, 0));
	conversation_bar->add_child(conversation_name_edit);

	new_conversation_btn = memnew(Button);
	new_conversation_btn->set_text("+");
	new_conversation_btn->set_custom_minimum_size(Size2(32 * EDSCALE, 0));
	new_conversation_btn->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_new_conversation));
	conversation_bar->add_child(new_conversation_btn);

	conversation_menu = memnew(MenuButton);
	conversation_menu->set_text(TTR("Conversations"));
	conversation_bar->add_child(conversation_menu);

	// === Main content area ===
	VBoxContainer *main_area = memnew(VBoxContainer);
	main_area->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_area->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	main_area->add_theme_constant_override("separation", 8 * EDSCALE);
	root->add_child(main_area);

	// Message scroll area
	message_scroll = memnew(ScrollContainer);
	message_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	message_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	main_area->add_child(message_scroll);

	message_list = memnew(VBoxContainer);
	message_list->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	message_list->add_theme_constant_override("separation", 10 * EDSCALE);
	message_scroll->add_child(message_list);

	// Bulk action bar
	bulk_action_bar = memnew(HBoxContainer);
	bulk_action_bar->add_theme_constant_override("separation", 6 * EDSCALE);
	bulk_action_bar->set_visible(false);
	main_area->add_child(bulk_action_bar);

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
	main_area->add_child(tool_call_label);

	// === Composer panel ===
	PanelContainer *composer = memnew(PanelContainer);
	composer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	composer->set_custom_minimum_size(Size2(0, 180) * EDSCALE);
	main_area->add_child(composer);

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
	root->add_child(status_label);

	// Initialize mode indicator
	_update_mode_indicator();

	// Chat service
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

	_update_translations();
	_set_requesting(false);
	status_label->set_text(TTR("AI assistant ready."));
}
