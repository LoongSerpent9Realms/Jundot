/*  ai_chat_panel.h                                                        */
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

#include "ai_settings.h"
#include "editor/ai/ai_chat_parser.h"

#include "core/templates/vector.h"
#include "scene/gui/margin_container.h"

class AIChatMessage;
class AIChatService;
class AIRepairCard;
class AIUsageAgreementDialog;
class AISuggestionCard;
class Button;
class EditorFileDialog;
class HBoxContainer;
class HSplitContainer;
class ItemList;
class Label;
class LineEdit;
class MenuButton;
class PanelContainer;
class PopupMenu;
class ScrollContainer;
class TextEdit;
class VBoxContainer;

class AIChatPanel : public MarginContainer {
	GDCLASS(AIChatPanel, MarginContainer)

	enum FileMenuId {
		FILE_MENU_REFERENCE_PROJECT = 0,
		FILE_MENU_UPLOAD_TEXT = 1,
		FILE_MENU_IMPORT = 2,
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
	Label *tool_call_label = nullptr;
	EditorFileDialog *reference_file_dialog = nullptr;
	EditorFileDialog *upload_file_dialog = nullptr;
	EditorFileDialog *import_file_dialog = nullptr;
	AIUsageAgreementDialog *usage_agreement_dialog = nullptr;

	// Mode switching (PROJECT / ENGINE).
	HBoxContainer *mode_bar = nullptr;
	Button *engine_mode_btn = nullptr;
	Button *project_mode_btn = nullptr;
	Label *mode_indicator = nullptr;

	// Conversation selector.
	HBoxContainer *conversation_bar = nullptr;
	LineEdit *conversation_name_edit = nullptr;
	Button *new_conversation_btn = nullptr;
	MenuButton *conversation_menu = nullptr;
	PopupMenu *conversation_popup = nullptr;

	// Conversation history list.
	ItemList *conversation_list = nullptr;
	Vector<Vector<String>> saved_conversations; // Each: [title, timestamp, messages_json].
	String current_conversation_id;

	Vector<AISuggestion> pending_suggestions;
	Vector<AISuggestionCard *> suggestion_cards;
	Vector<AIRepairCard *> repair_cards;
	HBoxContainer *bulk_action_bar = nullptr;
	Button *add_all_button = nullptr;
	Button *dismiss_all_button = nullptr;

	int editing_message_index = -1;

	struct ChatAttachment {
		String path;
		String display_name;
		String content;
		bool external = false;
	};

	bool is_summarizing = false;
	String pending_user_message;
	Vector<ChatAttachment> pending_attachments;

	// Conversation title auto-summary.
	bool is_titling = false;
	bool has_auto_titled = false;
	String pending_title_text;
	Vector<ChatAttachment> pending_title_attachments;

	Vector<ChatAttachment> attachments;

	// Tool call execution.
	struct PendingToolRound {
		Array original_messages;
		Array original_tools;
	};

	PendingToolRound pending_tool_round;
	bool in_tool_loop = false;

	// Active settings used for the current send, cached so the tool loop
	// reuses the same system prompt (with auto_mode, context, etc.) instead
	// of reloading from disk and losing those modifications.
	AISettingsData active_settings;

	void _send_message();
	void _cancel_request();
	void _clear_messages();
	void _chat_completed(int p_result, int p_response_code, const String &p_content, const Dictionary &p_json, const String &p_raw_body, double p_elapsed_seconds, const String &p_think_content, int p_prompt_tokens, int p_completion_tokens);
	void _execute_tool_calls(const Dictionary &p_json);
	void _add_user_message(const String &p_text);
	void _add_ai_message(const String &p_content, const String &p_think_content, double p_think_time, int p_prompt_tokens, int p_completion_tokens);
	void _on_edit_requested(const String &p_content);
	void _add_file_menu_id_pressed(int p_id);
	void _project_file_selected(const String &p_path);
	void _external_file_selected(const String &p_path);
	void _add_attachment(const String &p_path, bool p_external);
	void _remove_attachment(int p_index);
	void _refresh_attachment_chips();
	String _build_attachment_context() const;
	Array _build_message_history() const;
	void _start_summarization(const Array &p_history, int p_budget);
	void _summary_completed(const String &p_summary_text);
	void _add_summary_message(const String &p_content);

	// Conversation title auto-summary.
	void _try_auto_title(const String &p_user_first_message, const Array &p_history);
	void _title_completed(int p_result, int p_response_code, const String &p_title_content);
	String _detect_mode_prompt(const String &p_user_message) const;
	void _update_translations();
	void _set_requesting(bool p_requesting);
	bool _ensure_usage_agreement();
	void _usage_agreement_accepted();
	void _usage_agreement_rejected();

	// Mode switching (PROJECT / ENGINE).
	void _switch_to_engine();
	void _switch_to_project();
	void _update_mode_indicator();

	// Conversation management.
	void _new_conversation();
	void _update_conversation_menu();
	void _select_conversation(int p_index);
	void _save_current_conversation();
	void _load_conversation(const String &p_messages_json);
	void _update_conversation_list();

	// Suggestion card callbacks.
	void _suggestion_accepted(AISuggestionCard *p_card);
	void _suggestion_rejected(AISuggestionCard *p_card);
	void _add_all_suggestions();
	void _dismiss_all_suggestions();
	void _show_suggestions(const Vector<AISuggestion> &p_suggestions);
	void _clear_suggestions();
	void _refresh_bulk_bar();

	// Import callback.
	void _import_file_selected(const String &p_path);

	// Repair card callbacks.
	void _repair_apply_patch(AIRepairCard *p_card);
	void _repair_run_tests(AIRepairCard *p_card);
	void _repair_open_files(AIRepairCard *p_card);
	void _repair_retry_ai(AIRepairCard *p_card);
	void _repair_skip(AIRepairCard *p_card);
	void _repair_rebuild(AIRepairCard *p_card);
	void _repair_publish(AIRepairCard *p_card);
	void _show_repair_tasks(const Vector<AIRepairSuggestion> &p_repairs);
	void _clear_repair_cards();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	AIChatPanel();
};
