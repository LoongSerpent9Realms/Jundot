/*  ai_chat_panel.h                                                       */
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

#include "core/os/thread.h"
#include "core/templates/vector.h"
#include "editor/ai/ai_chat_parser.h"
#include "editor/ai/ai_settings.h"
#include "scene/gui/margin_container.h"

class AIChatMessage;
class AIChatService;
class AIFirstRunGuideDialog;
class AIRepairCard;
class AISourceManager;
class AIToolConfirmationDialog;
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
class ProgressBar;
class ScrollContainer;
class TextEdit;
class Timer;
class VBoxContainer;
class PanelContainer;
class ItemList;

class AIChatPanel : public MarginContainer {
	GDCLASS(AIChatPanel, MarginContainer);

	enum FileMenuId {
		FILE_MENU_REFERENCE_PROJECT = 0,
		FILE_MENU_UPLOAD_TEXT = 1,
		FILE_MENU_UPLOAD_IMAGE = 2,
		FILE_MENU_IMPORT = 3,
	};

	AIChatService *chat_service = nullptr;
	AIChatMessage *streaming_message = nullptr;
	VBoxContainer *message_list = nullptr;
	ScrollContainer *message_scroll = nullptr;
	HBoxContainer *attachment_chips = nullptr;
	VBoxContainer *queued_messages_box = nullptr;
	Label *queued_messages_title = nullptr;
	TextEdit *input = nullptr;
	Button *send_button = nullptr;
	Button *cancel_button = nullptr;
	Button *clear_button = nullptr;
	MenuButton *add_file_menu = nullptr;
	Label *status_label = nullptr;
	PanelContainer *programming_onboarding_panel = nullptr;
	PanelContainer *programming_mode_hint_panel = nullptr;
	Label *programming_mode_hint_label = nullptr;
	Button *programming_mode_switch_button = nullptr;
	PanelContainer *beginner_ai_guide_panel = nullptr;
	Label *beginner_ai_guide_label = nullptr;
	bool beginner_chat_mode = false;
	AISourceManager *beginner_source_manager = nullptr;
	bool beginner_source_auto_config_requested = false;
	PanelContainer *ai_activity_panel = nullptr;
	ProgressBar *ai_activity_progress = nullptr;
	Label *ai_activity_label = nullptr;
	String ai_activity_text;
	Label *tool_call_label = nullptr;
	PanelContainer *tool_limit_options_panel = nullptr;
	Button *tool_limit_toggle_button = nullptr;
	Label *tool_limit_options_title = nullptr;
	Button *tool_limit_continue_button = nullptr;
	Button *tool_limit_custom_button = nullptr;
	Button *tool_limit_stop_button = nullptr;
	Button *tool_limit_collapse_button = nullptr;
	VBoxContainer *next_question_options_box = nullptr;
	bool tool_limit_options_due_to_limit = false;
	Vector<String> next_question_options;
	EditorFileDialog *reference_file_dialog = nullptr;
	EditorFileDialog *upload_file_dialog = nullptr;
	EditorFileDialog *upload_image_dialog = nullptr;
	EditorFileDialog *import_file_dialog = nullptr;
	AIUsageAgreementDialog *usage_agreement_dialog = nullptr;
	AIFirstRunGuideDialog *first_run_guide_dialog = nullptr;

	// Mode switching (PROJECT / ENGINE).
	HBoxContainer *mode_bar = nullptr;
	Label *source_update_status_label = nullptr;
	Label *develop_mode_status_label = nullptr;
	Button *develop_user_pass_button = nullptr;
	Button *develop_user_fail_button = nullptr;
	Button *engine_mode_btn = nullptr;
	Button *project_mode_btn = nullptr;
	Label *mode_indicator = nullptr;
	bool beginner_privilege_escalated = false;

	// Conversation selector.
	HBoxContainer *conversation_bar = nullptr;
	LineEdit *conversation_name_edit = nullptr;
	Button *new_conversation_btn = nullptr;
	MenuButton *conversation_menu = nullptr;
	PopupMenu *conversation_popup = nullptr;

	// Conversation history list.
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
		String mime_type;
		String data_url;
		bool external = false;
		bool image = false;
	};

	bool is_summarizing = false;
	String pending_user_message;
	Vector<ChatAttachment> pending_attachments;

	// Conversation title auto-summary.
	bool is_titling = false;
	bool has_auto_titled = false;
	String pending_title_text;
	String title_request_conversation_id;
	Vector<ChatAttachment> pending_title_attachments;

	Vector<ChatAttachment> attachments;
	float chat_display_scale = 0.92f;

	// Tool call execution.
	struct PendingToolRound {
		Array original_messages;
		Array original_tools;
		int iteration_count = 0;
		int max_iterations = 10;
		bool missing_tool_retry_used = false;
		bool executed_tool_calls = false;
	};

	PendingToolRound pending_tool_round;
	bool in_tool_loop = false;
	String request_conversation_id;
	uint64_t response_started_usec = 0;
	String accumulated_think_content;
	Timer *build_status_poll_timer = nullptr;
	Timer *response_elapsed_timer = nullptr;
	int build_status_poll_count = 0;

	// Tool call confirmation dialog.
	AIToolConfirmationDialog *tool_confirmation_dialog = nullptr;
	Dictionary pending_tool_calls_json; // Stores the JSON for pending tool calls
	Thread tool_execution_thread;
	bool tool_execution_running = false;
	bool tool_execution_cancelled = false;
	Array tool_execution_messages;
	Array tool_execution_tools;
	Array tool_execution_tool_calls;
	bool tool_execution_build_poll_needed = false;

	// Tool confirmation callbacks.
	void _confirm_tool_execute(int p_tool_index);
	void _confirm_tool_skip(int p_tool_index);
	void _confirm_tool_cancel_all();
	static void _tool_execution_thread_func(void *p_userdata);
	void _finish_tool_execution_thread();

	// Active settings used for the current send, cached so the tool loop
	// reuses the same system prompt (with auto_mode, context, etc.) instead
	// of reloading from disk and losing those modifications.
	AISettingsData active_settings;

	// ============ Multi-conversation support ============
	struct ConversationMessage {
		bool is_user = false;
		bool is_summary = false;
		String content;
		String think_content;
		double think_time_seconds = 0.0;
		int prompt_tokens = 0;
		int completion_tokens = 0;
	};

	struct IssueLedgerEntry {
		String title;
		String status;
		String closed_by;
		uint64_t updated_at = 0;
	};

	struct QueuedChatMessage {
		String text;
		bool guided = false;
		uint64_t created_at = 0;
	};
	struct Conversation {
		String id;
		String title;
		AIContextMode context_mode = AIContextMode::PROJECT;
		String project_brief;
		String task_intent;
		String task_brief;
		Vector<ConversationMessage> messages;
		Vector<IssueLedgerEntry> issue_ledger;
		Array structured_messages;
		uint64_t created_at = 0;
		uint64_t updated_at = 0;
		bool tool_limit_options_available = false;
		bool tool_limit_options_collapsed = false;
		bool tool_limit_options_due_to_limit = false;
		Vector<String> next_question_options;
		Vector<QueuedChatMessage> queued_messages;

		static Dictionary to_dict(const Conversation &p_conv);
		static Conversation from_dict(const Dictionary &p_dict);
	};

	Vector<Conversation> conversations;
	Vector<QueuedChatMessage> queued_messages;
	String active_conversation_id;

	// Sidebar UI.
	HSplitContainer *chat_split = nullptr;
	PanelContainer *sidebar_panel = nullptr;
	PanelContainer *chat_surface_panel = nullptr;
	PanelContainer *composer_panel = nullptr;
	Control *chat_top_bar_container = nullptr;
	VBoxContainer *sidebar = nullptr;
	Button *new_conversation_button = nullptr;
	Button *delete_conversation_button = nullptr;
	ItemList *conversation_list = nullptr;
	Button *collapse_sidebar_button = nullptr;
	Button *expand_sidebar_button = nullptr;
	bool sidebar_collapsed = false;
	bool sidebar_user_collapsed = false;
	bool sidebar_auto_collapsed = false;
	int sidebar_expanded_width = 218;

	void _new_conversation();
	void _delete_current_conversation();
	void _conversation_selected(int p_index);
	void _select_conversation(const String &p_id);
	String _generate_conversation_id() const;
	String _auto_generate_title(const Conversation &p_conv) const;
	void _save_all_conversations() const;
	void _load_all_conversations();
	String _get_conversations_file_path() const;
	String _infer_project_brief() const;
	String _build_conversation_brief_prompt() const;
	String _build_issue_ledger_prompt() const;
	bool _message_confirms_issue_closed(const String &p_message) const;
	String _find_recent_issue_subject(const String &p_current_message) const;
	void _record_issue_closed_from_user(const String &p_user_message);
	void _set_current_conversation_task_intent(const String &p_intent, const String &p_brief);
	void _seed_new_conversation_options();
	void _refresh_conversation_list_ui();
	void _set_sidebar_collapsed(bool p_collapsed);
	void _apply_sidebar_visibility();
	void _update_responsive_sidebar();
	void _toggle_sidebar();
	void _serialize_current_messages();
	void _load_conversation_to_ui(const Conversation &p_conv);
	Array _get_structured_history() const;
	void _store_structured_history(const Array &p_messages, const String &p_assistant_content = String());
	void _clear_structured_history();
	void _enqueue_current_message();
	void _refresh_queued_messages_ui();
	void _guide_queued_message(int p_index);
	void _delete_queued_message(int p_index);
	void _dispatch_next_queued_message();

	// ============ End multi-conversation support ============

	void _send_message();
	void _cancel_request();
	void _clear_input();
	void _clear_messages();
	void _chat_completed(int p_result, int p_response_code, const String &p_content, const Dictionary &p_json, const String &p_raw_body, double p_elapsed_seconds, const String &p_think_content, int p_prompt_tokens, int p_completion_tokens);
	void _chat_stream_data(const String &p_delta, const String &p_full_content, int p_completion_tokens);
	Array _get_available_tools_for_active_settings() const;
	bool _looks_like_tool_preamble(const String &p_content) const;
	bool _extract_text_tool_calls(const String &p_content, Array &r_tool_calls) const;
	String _strip_text_tool_call_blocks(const String &p_content) const;
	void _start_response_tracking();
	void _on_response_elapsed_tick();
	void _append_response_thought(const String &p_content);
	String _get_response_thought(const String &p_current_thought) const;
	double _get_response_elapsed(double p_fallback_elapsed) const;
	void _clear_response_tracking();
	void _save_project_memory_update(const AIProjectMemoryUpdate &p_update);
	bool _retry_after_missing_tool_call(const String &p_content);
	void _execute_tool_calls(const Dictionary &p_json);
	void _add_user_message(const String &p_text);
	void _add_ai_message(const String &p_content, const String &p_think_content, double p_think_time, int p_prompt_tokens, int p_completion_tokens);
	void _show_task_plans(const Vector<AITaskPlan> &p_task_plans);
	void _on_edit_requested(const String &p_content);
	void _add_file_menu_id_pressed(int p_id);
	void _project_file_selected(const String &p_path);
	void _external_file_selected(const String &p_path);
	void _image_file_selected(const String &p_path);
	void _add_attachment(const String &p_path, bool p_external);
	void _add_clipboard_image();
	void _input_gui_input(const Ref<InputEvent> &p_event);
	void _add_dropped_files(const Vector<String> &p_files);
	void _window_files_dropped(const Vector<String> &p_files);
	bool _can_drop_files_fw(const Point2 &p_point, const Variant &p_data, Control *p_from) const;
	void _drop_files_fw(const Point2 &p_point, const Variant &p_data, Control *p_from);
	void _remove_attachment(int p_index);
	void _refresh_attachment_chips();
	String _build_attachment_context() const;
	Array _build_multimodal_user_content(const String &p_text) const;
	void _set_chat_display_scale(float p_scale);
	void _update_auto_chat_display_scale();
	Array _build_message_history() const;
	void _start_summarization(const Array &p_history, int p_budget);
	void _summary_completed(const String &p_summary_text);
	void _add_summary_message(const String &p_content);

	// Conversation title auto-summary.
	void _try_auto_title(const String &p_user_first_message, const Array &p_history);
	void _title_completed(int p_result, int p_response_code, const String &p_title_content);
	bool _is_project_memory_continue_request(const String &p_user_message) const;
	String _detect_mode_prompt(const String &p_user_message) const;
	void _update_translations();
	void _apply_programming_experience_layout();
	void _apply_editor_beginner_workspace(bool p_enabled);
	void _ensure_beginner_engine_source_configured();
	void _set_programming_experience(bool p_has_programming_experience);
	void _toggle_programming_experience_mode();
	void _set_ai_activity(const String &p_text, bool p_visible);
	void _refresh_ai_activity();
	void _set_requesting(bool p_requesting);
	bool _ensure_usage_agreement();
	void _usage_agreement_accepted();
	void _usage_agreement_rejected();
	void _show_first_run_guide_if_needed(bool p_force = false);
	void _open_ai_config_from_first_run_guide();
	void _show_tool_limit_options(bool p_due_to_limit);
	void _hide_tool_limit_options();
	void _set_tool_limit_options_collapsed(bool p_collapsed);
	void _store_tool_limit_options_state(bool p_save = true);
	void _apply_tool_limit_options_state(const Conversation &p_conv);
	void _set_next_question_options(const Vector<String> &p_questions, bool p_save = true);
	void _render_next_question_options();
	void _use_next_question_option(const String &p_question);
	void _send_hidden_followup(const String &p_instruction);
	void _continue_after_tool_limit();
	void _focus_custom_tool_limit_message();
	void _dismiss_tool_limit_options();

	// Mode switching (PROJECT / ENGINE).
	void _switch_to_engine();
	void _switch_to_project();
	void _update_mode_indicator();
	void _update_develop_mode_ui();
	void _develop_user_verification(bool p_passed);

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

	bool _tool_result_needs_build_poll(const String &p_content) const;
	void _start_build_status_poll();
	void _stop_build_status_poll();
	void _on_build_status_poll_timeout();
	void _append_forced_build_status_check();
	void _continue_after_build_poll();

	// --- Send / Stop button dynamic text ---
	bool is_currently_requesting = false;
	void _on_input_text_changed();
	void _update_send_button_text();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void send_post_restart_question();

	AIChatPanel();
};
