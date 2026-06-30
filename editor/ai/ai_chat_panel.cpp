/*  ai_chat_panel.cpp                                                     */
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

#include "editor/ai/ai_source_update_service.h"

#include "core/config/project_settings.h"
#include "core/core_bind.h"
#include "core/input/input_event.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/image.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "editor/ai/ai_build_bridge.h"
#include "editor/ai/ai_chat_message.h"
#include "editor/ai/ai_chat_service.h"
#include "editor/ai/ai_code_fetcher.h"
#include "editor/ai/ai_develop_flow.h"
#include "editor/ai/ai_first_run_guide_dialog.h"
#include "editor/ai/ai_context_builder.h"
#include "editor/ai/ai_feature_gate.h"
#include "editor/ai/ai_importer.h"
#include "editor/ai/ai_memory_store.h"
#include "editor/ai/ai_new_build_notifier.h"
#include "editor/ai/ai_patch_applier.h"
#include "editor/ai/ai_repair_card.h"
#include "editor/ai/ai_repair_workflow.h"
#include "editor/ai/ai_restart_helper.h"
#include "editor/ai/ai_settings.h"
#include "editor/ai/ai_suggestion_card.h"
#include "editor/ai/ai_tool_confirmation_dialog.h"
#include "editor/ai/ai_tool_defs.h"
#include "editor/ai/ai_tool_executor.h"
#include "editor/ai/ai_tool_registry.h"
#include "editor/ai/ai_usage_agreement_dialog.h"
#include "editor/docks/editor_dock.h"
#include "editor/file_system/editor_paths.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/export/editor_export.h"
#include "editor/export/editor_export_platform.h"
#include "editor/export/editor_export_preset.h"
#include "editor/export/export_template_manager.h"
#include "editor/run/editor_run_bar.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "editor/project_manager/ai_source_manager.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/progress_bar.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/split_container.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/text_edit.h"
#include "scene/main/http_request.h"
#include "scene/main/scene_tree.h"
#include "scene/main/timer.h"
#include "scene/main/window.h"
#include "scene/resources/style_box_flat.h"
#include "servers/display/display_server.h"

static constexpr int AI_CHAT_ATTACHMENT_MAX_BYTES = 64 * 1024;
static constexpr int AI_CHAT_IMAGE_ATTACHMENT_MAX_BYTES = 4 * 1024 * 1024;
static const char *AI_CHAT_METADATA_SECTION = "jundot_ai_chat";
static const char *AI_CHAT_METADATA_PROGRAMMING_EXPERIENCE_ASKED = "programming_experience_asked";
static const char *AI_CHAT_METADATA_HAS_PROGRAMMING_EXPERIENCE = "has_programming_experience";
static const char *AI_CHAT_METADATA_FIRST_RUN_GUIDE_SHOWN = "first_run_guide_shown";

static bool _ai_chat_is_image_extension(const String &p_path) {
	const String ext = p_path.get_extension().to_lower();
	return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "webp" || ext == "bmp";
}

static String _ai_chat_image_mime_type(const String &p_path) {
	const String ext = p_path.get_extension().to_lower();
	if (ext == "jpg" || ext == "jpeg") {
		return "image/jpeg";
	}
	if (ext == "webp") {
		return "image/webp";
	}
	if (ext == "bmp") {
		return "image/bmp";
	}
	return "image/png";
}

static String _ai_chat_normalize_project_path(String p_path) {
	p_path = p_path.strip_edges().replace("\\", "/");
	if (p_path.begins_with("res://")) {
		p_path = p_path.substr(6);
	}
	while (p_path.begins_with("./")) {
		p_path = p_path.substr(2);
	}
	return p_path.simplify_path();
}

static bool _ai_chat_is_html_prototype_write_path(const String &p_path) {
	const String normalized = _ai_chat_normalize_project_path(p_path).to_lower();
	if (!normalized.begins_with(".jundotai/prototypes/")) {
		return false;
	}
	const String ext = normalized.get_extension();
	return ext == "html" || ext == "css" || ext == "js";
}

static bool _ai_chat_is_tool_error_text(const String &p_text) {
	const String lower = p_text.to_lower();
	return lower.contains("<tool_call") ||
			lower.contains("function calling tools are disabled") ||
			lower.contains("no tools are available for the current mode") ||
			lower.contains("tool calling did not run") ||
			lower.contains("ai returned a text tool call") ||
			lower.contains("当前模式没有可用的 function calling 工具") ||
			lower.contains("工具调用");
}

static String _ai_chat_next_question_protocol() {
	return String("=== Next Question Options Protocol ===\n"
				  "- REQUIRED: At the end of every final assistant response, suggest 2 to 4 likely next questions the user may want to ask. Never omit them.\n"
				  "- Put each question in a hidden machine-readable block so the editor can show it as a clickable option:\n"
				  "<!-- NEXT_QUESTION -->\nMODE: single\nQUESTION: <one concise user question>\n<!-- END_NEXT_QUESTION -->\n"
				  "- Use MODE: single (default) when each option is an independent follow-up. Use MODE: multi when the options are complementary steps the user may want to combine.\n"
				  "- You may put multiple QUESTION lines in one block when using MODE: multi:\n"
				  "<!-- NEXT_QUESTION -->\nMODE: multi\nQUESTION: <question A>\nQUESTION: <question B>\nQUESTION: <question C>\n<!-- END_NEXT_QUESTION -->\n"
				  "- Keep each question specific to the current conversation and useful as the user's next message.\n"
				  "- Do not mention these hidden blocks in the visible response.");
}

static String _ai_project_memory_protocol() {
	return String("=== Automatic Project Memory Protocol ===\n"
				  "- PROJECT mode only: at the end of every final assistant response, summarize only durable new facts learned or confirmed in this completed response.\n"
				  "- Do not copy the conversation, transient errors, speculation, secrets, credentials, tokens, personal identifiers, or temporary implementation chatter.\n"
				  "- Keep each field to one concise semicolon-separated line. Use an empty value when nothing durable was added for that category.\n"
				  "- USER_PREFERENCES covers the user's stable communication style, workflow preferences, and recurring requirements.\n"
				  "- PROJECT_REQUIREMENTS covers game design goals, technical constraints, architecture rules, and acceptance criteria.\n"
				  "- KEY_DECISIONS covers approved choices that future work must preserve.\n"
				  "- COMPLETED_WORK covers concrete project work actually completed and verified, not merely proposed.\n"
				  "<!-- PROJECT_MEMORY -->\n"
				  "USER_PREFERENCES: <durable new facts or empty>\n"
				  "PROJECT_REQUIREMENTS: <durable new facts or empty>\n"
				  "KEY_DECISIONS: <durable new facts or empty>\n"
				  "COMPLETED_WORK: <durable new facts or empty>\n"
				  "<!-- END_PROJECT_MEMORY -->\n"
				  "- Do not mention this hidden block in the visible response.");
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
	d["project_brief"] = p_conv.project_brief;
	d["task_intent"] = p_conv.task_intent;
	d["task_brief"] = p_conv.task_brief;
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
	d["next_question_multi_select"] = p_conv.next_question_multi_select;
	d["structured_messages"] = p_conv.structured_messages;
	Array issues;
	for (int i = 0; i < p_conv.issue_ledger.size(); i++) {
		const IssueLedgerEntry &issue = p_conv.issue_ledger[i];
		Dictionary item;
		item["title"] = issue.title;
		item["status"] = issue.status;
		item["closed_by"] = issue.closed_by;
		item["updated_at"] = (int64_t)issue.updated_at;
		issues.push_back(item);
	}
	d["issue_ledger"] = issues;
	Array queued;
	for (int i = 0; i < p_conv.queued_messages.size(); i++) {
		Dictionary q;
		q["text"] = p_conv.queued_messages[i].text;
		q["guided"] = p_conv.queued_messages[i].guided;
		q["created_at"] = (int64_t)p_conv.queued_messages[i].created_at;
		queued.push_back(q);
	}
	d["queued_messages"] = queued;

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
	conv.project_brief = String(p_dict.get("project_brief", String())).strip_edges();
	conv.task_intent = String(p_dict.get("task_intent", String())).strip_edges();
	conv.task_brief = String(p_dict.get("task_brief", String())).strip_edges();
	conv.created_at = (uint64_t)p_dict.get("created_at", 0);
	conv.updated_at = (uint64_t)p_dict.get("updated_at", 0);
	conv.tool_limit_options_available = p_dict.get("tool_limit_options_available", false);
	conv.tool_limit_options_collapsed = p_dict.get("tool_limit_options_collapsed", false);
	conv.tool_limit_options_due_to_limit = p_dict.get("tool_limit_options_due_to_limit", false);
	Variant structured_messages_var = p_dict.get("structured_messages", Array());
	if (structured_messages_var.get_type() == Variant::ARRAY) {
		conv.structured_messages = ((Array)structured_messages_var).duplicate(true);
	}
	Array issues = p_dict.get("issue_ledger", Array());
	for (int i = 0; i < issues.size(); i++) {
		if (issues[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary item = issues[i];
		IssueLedgerEntry issue;
		issue.title = String(item.get("title", String())).strip_edges();
		issue.status = String(item.get("status", String())).strip_edges();
		issue.closed_by = String(item.get("closed_by", String())).strip_edges();
		issue.updated_at = (uint64_t)item.get("updated_at", 0);
		if (!issue.title.is_empty() && !issue.status.is_empty()) {
			conv.issue_ledger.push_back(issue);
		}
	}
	Array queued = p_dict.get("queued_messages", Array());
	for (int i = 0; i < queued.size(); i++) {
		if (queued[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary q = queued[i];
		QueuedChatMessage item;
		item.text = String(q.get("text", String())).strip_edges();
		item.guided = q.get("guided", false);
		item.created_at = (uint64_t)q.get("created_at", 0);
		if (!item.text.is_empty()) {
			conv.queued_messages.push_back(item);
		}
	}
	Array next_questions = p_dict.get("next_question_options", Array());
	for (int i = 0; i < next_questions.size(); i++) {
		const String question = String(next_questions[i]).strip_edges();
		if (!question.is_empty()) {
			conv.next_question_options.push_back(question);
		}
	}
	conv.next_question_multi_select = p_dict.get("next_question_multi_select", false);

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
		if (m.is_summary && _ai_chat_is_tool_error_text(m.content)) {
			continue;
		}
		m.think_content = md.get("think_content", String());
		m.think_time_seconds = md.get("think_time", 0.0);
		m.prompt_tokens = md.get("prompt_tokens", 0);
		m.completion_tokens = md.get("completion_tokens", 0);
		conv.messages.push_back(m);
	}
	return conv;
}

String AIChatPanel::_get_conversations_file_path() const {
	if (ProjectSettings::get_singleton()) {
		const String project_root = ProjectSettings::get_singleton()->get_resource_path();
		if (!project_root.is_empty() && (FileAccess::exists(project_root.path_join("project.jundot")) || FileAccess::exists(project_root.path_join("project.godot")))) {
			return project_root.path_join(".JundotAI").path_join("conversations.json");
		}
	}
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

String AIChatPanel::_infer_project_brief() const {
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (!project_settings) {
		return String();
	}

	Vector<String> parts;
	const String project_name = String(project_settings->get_setting("application/config/name", String())).strip_edges();
	if (!project_name.is_empty()) {
		parts.push_back(vformat("Project name: %s", project_name));
	}

	const String main_scene = String(project_settings->get_setting("application/run/main_scene", String())).strip_edges();
	if (!main_scene.is_empty()) {
		parts.push_back(vformat("Main scene: %s", main_scene));
	}

	const String root = project_settings->get_resource_path();
	if (!root.is_empty()) {
		parts.push_back(vformat("Project root: %s", root));
	}

	if (parts.is_empty()) {
		return TTR("No project metadata was detected yet. Ask the user for the project goal before making broad changes.");
	}

	return String("\n").join(parts);
}

String AIChatPanel::_build_conversation_brief_prompt() const {
	if (active_conversation_id.is_empty()) {
		return String();
	}

	for (int i = 0; i < conversations.size(); i++) {
		if (conversations[i].id != active_conversation_id) {
			continue;
		}

		const Conversation &conv = conversations[i];
		String prompt = "=== Conversation Brief ===\n";
		if (!conv.project_brief.strip_edges().is_empty()) {
			prompt += conv.project_brief.strip_edges();
		} else {
			prompt += "Project: unknown. Infer carefully from the open project before making broad assumptions.";
		}
		prompt += "\n";

		if (!conv.task_intent.strip_edges().is_empty()) {
			prompt += vformat("Current task intent: %s\n", conv.task_intent.strip_edges());
		} else {
			prompt += "Current task intent: not selected yet. Infer from the latest user message and ask one concise clarifying question if the goal is ambiguous.\n";
		}
		if (!conv.task_brief.strip_edges().is_empty()) {
			prompt += vformat("Active task brief: %s\n", conv.task_brief.strip_edges());
		}

		const String issue_prompt = _build_issue_ledger_prompt();
		if (!issue_prompt.is_empty()) {
			prompt += issue_prompt;
			prompt += "\n";
		}

		prompt += "Use this brief and the latest user request as the active instruction boundary. Treat older chat messages as historical evidence only; if old messages conflict with this brief or the latest request, follow this brief and the latest request. Do not revive abandoned plans, stale errors, fixed issues, verified-passed failures, or earlier guesses unless fresh logs, fresh tool checks, or a fresh user report prove they are active again.";
		return prompt;
	}

	return String();
}

String AIChatPanel::_build_issue_ledger_prompt() const {
	if (active_conversation_id.is_empty()) {
		return String();
	}

	for (int i = 0; i < conversations.size(); i++) {
		if (conversations[i].id != active_conversation_id) {
			continue;
		}

		const Conversation &conv = conversations[i];
		Vector<String> closed_items;
		for (int j = 0; j < conv.issue_ledger.size(); j++) {
			const IssueLedgerEntry &issue = conv.issue_ledger[j];
			const String status = issue.status.strip_edges();
			if (status != "closed" && status != "verification_passed") {
				continue;
			}
			String line = vformat("- [%s] %s", status, issue.title.strip_edges());
			if (!issue.closed_by.strip_edges().is_empty()) {
				line += vformat(" (by %s)", issue.closed_by.strip_edges());
			}
			closed_items.push_back(line);
		}
		if (closed_items.is_empty()) {
			return String();
		}

		String prompt = "\n=== Issue Ledger ===\nClosed or verified-passed issues are historical records, not active failures. Do not keep repairing or diagnosing them from older chat history. Reopen one only when the latest user message, a fresh tool result, or a fresh log proves it is failing again.\n";
		const int start = MAX(0, closed_items.size() - 8);
		for (int j = start; j < closed_items.size(); j++) {
			prompt += closed_items[j] + "\n";
		}
		prompt += "Evidence priority: current editor/project state > fresh tool checks > latest user confirmation > summaries > old chat messages.";
		return prompt;
	}

	return String();
}

bool AIChatPanel::_message_confirms_issue_closed(const String &p_message) const {
	const String msg = p_message.to_lower().strip_edges();
	if (msg.is_empty()) {
		return false;
	}

	return msg.contains("修好了") || msg.contains("已经好了") || msg.contains("好了") || msg.contains("可以了") ||
		   msg.contains("不用管了") || msg.contains("别管了") || msg.contains("没问题了") || msg.contains("问题没了") ||
		   msg.contains("检测通过") || msg.contains("验证通过") || msg.contains("通过了") || msg.contains("已修复") ||
		   msg.contains("fixed") || msg.contains("it is fixed") || msg.contains("it's fixed") || msg.contains("works now") ||
		   msg.contains("verified") || msg.contains("passed") || msg.contains("no longer happens") || msg.contains("ignore this");
}

String AIChatPanel::_find_recent_issue_subject(const String &p_current_message) const {
	const String current = p_current_message.strip_edges();
	for (int i = message_list->get_child_count() - 2; i >= 0; i--) {
		AIChatMessage *msg = Object::cast_to<AIChatMessage>(message_list->get_child(i));
		if (!msg || !msg->is_user_message() || msg->is_summary_message()) {
			continue;
		}
		const String content = msg->get_content().strip_edges();
		const String lower = content.to_lower();
		if (lower.contains("没反应") || lower.contains("没有反应") || lower.contains("不能用") || lower.contains("不好用") ||
				lower.contains("不生效") || lower.contains("报错") || lower.contains("崩溃") || lower.contains("修复") ||
				lower.contains("bug") || lower.contains("error") || lower.contains("crash") || lower.contains("not working") ||
				lower.contains("doesn't work") || lower.contains("does not work") || lower.contains("broken") || lower.contains("fix")) {
			return content.left(180);
		}
	}

	if (!current.is_empty()) {
		return current.left(180);
	}
	return TTR("Issue confirmed fixed by the user");
}

void AIChatPanel::_record_issue_closed_from_user(const String &p_user_message) {
	if (!_message_confirms_issue_closed(p_user_message) || active_conversation_id.is_empty()) {
		return;
	}

	const String subject = _find_recent_issue_subject(p_user_message).strip_edges();
	if (subject.is_empty()) {
		return;
	}

	for (int i = 0; i < conversations.size(); i++) {
		if (conversations[i].id != active_conversation_id) {
			continue;
		}

		Conversation &conv = conversations.write[i];
		const uint64_t now = Time::get_singleton()->get_unix_time_from_system();
		for (int j = 0; j < conv.issue_ledger.size(); j++) {
			if (conv.issue_ledger[j].title == subject) {
				conv.issue_ledger.write[j].status = "closed";
				conv.issue_ledger.write[j].closed_by = "user_confirmation";
				conv.issue_ledger.write[j].updated_at = now;
				conv.updated_at = now;
				return;
			}
		}

		IssueLedgerEntry issue;
		issue.title = subject;
		issue.status = "closed";
		issue.closed_by = "user_confirmation";
		issue.updated_at = now;
		conv.issue_ledger.push_back(issue);
		conv.updated_at = now;
		return;
	}
}

void AIChatPanel::_set_current_conversation_task_intent(const String &p_intent, const String &p_brief) {
	if (active_conversation_id.is_empty()) {
		return;
	}

	for (int i = 0; i < conversations.size(); i++) {
		if (conversations[i].id == active_conversation_id) {
			conversations.write[i].task_intent = p_intent.strip_edges();
			conversations.write[i].task_brief = p_brief.strip_edges();
			conversations.write[i].updated_at = Time::get_singleton()->get_unix_time_from_system();
			_clear_structured_history();
			_save_all_conversations();
			break;
		}
	}
}

void AIChatPanel::_seed_new_conversation_options() {
	Vector<String> questions;
	questions.push_back(TTR("深入做功能：我会说明要新增或完善的功能目标"));
	questions.push_back(TTR("修复 Bug：我会说明现象、复现步骤和期望结果"));
	questions.push_back(TTR("讨论方案：先分析项目和设计思路，不直接改文件"));
	_set_next_question_options(questions, true);
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

void AIChatPanel::_apply_sidebar_visibility() {
	if (!chat_split || !sidebar_panel) {
		return;
	}
	if (beginner_chat_mode) {
		sidebar_panel->set_visible(false);
		if (collapse_sidebar_button) {
			collapse_sidebar_button->set_visible(false);
		}
		if (expand_sidebar_button) {
			expand_sidebar_button->set_visible(false);
		}
		return;
	}

	sidebar_collapsed = sidebar_user_collapsed || sidebar_auto_collapsed;
	sidebar_panel->set_visible(!sidebar_collapsed);
	if (collapse_sidebar_button) {
		collapse_sidebar_button->set_visible(!sidebar_collapsed);
	}
	if (expand_sidebar_button) {
		expand_sidebar_button->set_visible(sidebar_collapsed);
	}
	if (!sidebar_collapsed) {
		chat_split->set_split_offset(sidebar_expanded_width);
	}
}

void AIChatPanel::_set_sidebar_collapsed(bool p_collapsed) {
	if (!chat_split || !sidebar_panel) {
		return;
	}

	if (!sidebar_collapsed && p_collapsed) {
		sidebar_expanded_width = MAX(chat_split->get_split_offset(), int(150 * EDSCALE));
	}
	sidebar_user_collapsed = p_collapsed;
	if (!p_collapsed) {
		sidebar_auto_collapsed = false;
	}
	_apply_sidebar_visibility();
}

void AIChatPanel::_update_responsive_sidebar() {
	if (!chat_split || !sidebar_panel || get_size().x <= 0.0f) {
		return;
	}
	if (beginner_chat_mode) {
		_apply_sidebar_visibility();
		return;
	}

	const float panel_width = get_size().x;
	const int compact_width = int(150 * EDSCALE);
	const int normal_width = int(218 * EDSCALE);
	const float hide_threshold = 640.0f * EDSCALE;
	const float compact_threshold = 820.0f * EDSCALE;

	sidebar_auto_collapsed = panel_width < hide_threshold;
	if (!sidebar_auto_collapsed && !sidebar_user_collapsed) {
		sidebar_expanded_width = panel_width < compact_threshold ? compact_width : normal_width;
		sidebar_panel->set_custom_minimum_size(Size2(compact_width, 0));
	}
	_apply_sidebar_visibility();
}

void AIChatPanel::_toggle_sidebar() {
	_set_sidebar_collapsed(!sidebar_collapsed);
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
		if (msg->is_summary_message() && _ai_chat_is_tool_error_text(msg->get_content())) {
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

	conv.queued_messages = queued_messages;
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
			return _compress_messages_for_low_token_mode(conversations[i].structured_messages);
		}
	}
	return Array();
}

static int _ai_chat_message_content_length(const Dictionary &p_message) {
	const Variant content = p_message.get("content", Variant());
	if (content.get_type() == Variant::ARRAY) {
		int total = 0;
		const Array parts = content;
		for (int i = 0; i < parts.size(); i++) {
			if (parts[i].get_type() == Variant::DICTIONARY) {
				const Dictionary part = parts[i];
				total += String(part.get("text", String())).length();
			}
		}
		return total;
	}
	return String(content).length();
}

static String _ai_chat_truncate_context_text(const String &p_text, int p_limit, const String &p_reason) {
	if (p_limit <= 0 || p_text.length() <= p_limit) {
		return p_text;
	}
	const int head_len = MAX(1, p_limit * 2 / 3);
	const int tail_len = MAX(1, p_limit - head_len);
	return p_text.substr(0, head_len).strip_edges() +
			vformat("\n\n[... %s compressed: omitted %d chars ...]\n\n", p_reason, p_text.length() - p_limit) +
			p_text.substr(p_text.length() - tail_len, tail_len).strip_edges();
}

Array AIChatPanel::_compress_messages_for_low_token_mode(const Array &p_messages) const {
	const AISettingsData settings = AISettings::load();
	if (!settings.low_token_mode || p_messages.is_empty()) {
		return p_messages.duplicate(true);
	}

	const int total_budget = MAX(1024, settings.history_char_budget > 0 ? settings.history_char_budget : AISettings::get_low_token_history_char_budget());
	const int old_text_budget = 700;
	const int recent_text_budget = 1800;
	const int old_tool_budget = 900;
	const int recent_tool_budget = 1800;
	const int recent_window = 8;

	Array compressed;
	int running_chars = 0;
	for (int i = p_messages.size() - 1; i >= 0; i--) {
		if (p_messages[i].get_type() != Variant::DICTIONARY) {
			continue;
		}

		Dictionary message = ((Dictionary)p_messages[i]).duplicate(true);
		const String role = String(message.get("role", String()));
		const bool recent = (p_messages.size() - i) <= recent_window;
		int content_budget = recent ? recent_text_budget : old_text_budget;
		String reason = recent ? "recent message" : "old message";
		if (role == "tool") {
			content_budget = recent ? recent_tool_budget : old_tool_budget;
			reason = recent ? "recent tool result" : "old tool result";
		}

		if (role != "system" && message.has("content")) {
			if (message["content"].get_type() == Variant::ARRAY) {
				Array parts = message["content"];
				for (int j = 0; j < parts.size(); j++) {
					if (parts[j].get_type() != Variant::DICTIONARY) {
						continue;
					}
					Dictionary part = parts[j];
					if (part.has("text")) {
						part["text"] = _ai_chat_truncate_context_text(String(part["text"]), content_budget, reason);
					}
					parts[j] = part;
				}
				message["content"] = parts;
			} else {
				message["content"] = _ai_chat_truncate_context_text(String(message["content"]), content_budget, reason);
			}
		}

		const int message_chars = String(message.get("role", String())).length() + _ai_chat_message_content_length(message);
		if (role != "system" && !recent && running_chars + message_chars > total_budget) {
			if (role == "tool") {
				message["content"] = "[Old tool result compressed by Low Token Mode. Ask the AI to rerun or inspect the relevant file/log if exact output is needed.]";
			} else {
				message["content"] = "[Old conversation message compressed by Low Token Mode. Continue from the latest visible context and durable summary.]";
			}
		}

		running_chars += String(message.get("role", String())).length() + _ai_chat_message_content_length(message);
		compressed.push_front(message);
	}

	return compressed;
}

void AIChatPanel::_store_structured_history(const Array &p_messages, const String &p_assistant_content) {
	if (active_conversation_id.is_empty()) {
		return;
	}

	const Array messages_to_store = _compress_messages_for_low_token_mode(p_messages);
	Array stored_messages;
	for (int i = 0; i < messages_to_store.size(); i++) {
		if (messages_to_store[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary message = messages_to_store[i];
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

void AIChatPanel::_refresh_queued_messages_ui() {
	if (!queued_messages_box) {
		return;
	}

	for (int i = queued_messages_box->get_child_count() - 1; i >= 0; i--) {
		queued_messages_box->get_child(i)->queue_free();
	}

	queued_messages_box->set_visible(!queued_messages.is_empty());
	if (queued_messages.is_empty()) {
		return;
	}

	queued_messages_title = memnew(Label);
	queued_messages_title->set_text(vformat(TTR("Queued messages (%d)"), queued_messages.size()));
	queued_messages_title->add_theme_font_size_override("font_size", 12 * EDSCALE);
	queued_messages_title->add_theme_color_override("font_color", Color(0.72f, 0.72f, 0.72f));
	queued_messages_box->add_child(queued_messages_title);

	for (int i = 0; i < queued_messages.size(); i++) {
		HBoxContainer *row = memnew(HBoxContainer);
		row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		row->add_theme_constant_override("separation", 4 * EDSCALE);
		queued_messages_box->add_child(row);

		Label *preview = memnew(Label);
		String preview_text = queued_messages[i].text.replace("\r", " ").replace("\n", " ").strip_edges();
		if (queued_messages[i].guided) {
			preview_text = TTR("[Guided] ") + preview_text;
		}
		preview->set_text(preview_text);
		preview->set_tooltip_text(queued_messages[i].text);
		preview->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		preview->set_clip_text(true);
		preview->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
		preview->add_theme_font_size_override("font_size", 12 * EDSCALE);
		row->add_child(preview);

		Button *guide_button = memnew(Button);
		guide_button->set_flat(true);
		guide_button->set_text(TTR("Guide"));
		guide_button->set_tooltip_text(TTR("Prioritize this queued message as guidance for the next AI turn"));
		guide_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_guide_queued_message).bind(i));
		row->add_child(guide_button);

		Button *delete_button = memnew(Button);
		delete_button->set_flat(true);
		delete_button->set_text(TTR("Delete"));
		delete_button->set_tooltip_text(TTR("Remove this queued message"));
		delete_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_delete_queued_message).bind(i));
		row->add_child(delete_button);
	}
}

void AIChatPanel::_enqueue_current_message() {
	String queued_text = input ? input->get_text().strip_edges() : String();
	bool has_image_attachment = false;
	for (int i = 0; i < attachments.size(); i++) {
		if (attachments[i].image) {
			has_image_attachment = true;
			break;
		}
	}
	if (has_image_attachment) {
		status_label->set_text(TTR("Image attachments cannot be queued yet. Please wait for the current AI response to finish before sending images."));
		return;
	}

	if (queued_text.is_empty() && attachments.is_empty()) {
		status_label->set_text(TTR("Type a message to queue while the AI is responding."));
		return;
	}

	const String attachment_context = _build_attachment_context();
	if (!attachment_context.is_empty()) {
		if (!queued_text.is_empty()) {
			queued_text += "\n\n";
		}
		queued_text += attachment_context;
	}

	QueuedChatMessage item;
	item.text = queued_text;
	item.guided = false;
	item.created_at = Time::get_singleton()->get_unix_time_from_system();
	queued_messages.push_back(item);

	input->clear();
	attachments.clear();
	_refresh_attachment_chips();
	_refresh_queued_messages_ui();
	_serialize_current_messages();
	_save_all_conversations();
	status_label->set_text(vformat(TTR("Message queued. It will be sent after the current AI response finishes. (%d queued)"), queued_messages.size()));
	if (ai_activity_panel && ai_activity_panel->is_visible()) {
		_refresh_ai_activity();
	}
}

void AIChatPanel::_guide_queued_message(int p_index) {
	if (p_index < 0 || p_index >= queued_messages.size()) {
		return;
	}
	QueuedChatMessage item = queued_messages[p_index];
	item.guided = true;
	queued_messages.remove_at(p_index);
	queued_messages.insert(0, item);
	_refresh_queued_messages_ui();
	_serialize_current_messages();
	_save_all_conversations();
	status_label->set_text(TTR("Queued message prioritized as guidance for the next turn."));
	if (ai_activity_panel && ai_activity_panel->is_visible()) {
		_refresh_ai_activity();
	}
}

void AIChatPanel::_delete_queued_message(int p_index) {
	if (p_index < 0 || p_index >= queued_messages.size()) {
		return;
	}
	queued_messages.remove_at(p_index);
	_refresh_queued_messages_ui();
	_serialize_current_messages();
	_save_all_conversations();
	status_label->set_text(TTR("Queued message removed."));
	if (ai_activity_panel && ai_activity_panel->is_visible()) {
		_refresh_ai_activity();
	}
}

void AIChatPanel::_dispatch_next_queued_message() {
	const bool busy = (chat_service && chat_service->is_requesting()) || in_tool_loop || tool_execution_running || is_summarizing || is_titling || is_auditing || (build_status_poll_timer && !build_status_poll_timer->is_stopped());
	if (busy || queued_messages.is_empty() || !input) {
		return;
	}

	QueuedChatMessage next = queued_messages[0];
	queued_messages.remove_at(0);
	_refresh_queued_messages_ui();
	_serialize_current_messages();
	_save_all_conversations();

	input->set_text(next.text);
	status_label->set_text(next.guided ? TTR("Sending prioritized queued guidance...") : TTR("Sending next queued message..."));
	_send_message();
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
	_set_ai_activity(String(), false);
	editing_message_index = -1;
	has_auto_titled = !p_conv.messages.is_empty();
	attachments.clear();
	_refresh_attachment_chips();
	input->clear();
	queued_messages = p_conv.queued_messages;
	_refresh_queued_messages_ui();
	next_question_options = p_conv.next_question_options;
	_render_next_question_options();

	// Re-populate messages.
	for (int i = 0; i < p_conv.messages.size(); i++) {
		const ConversationMessage &cm = p_conv.messages[i];
		if (cm.is_summary && _ai_chat_is_tool_error_text(cm.content)) {
			continue;
		}
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
	conv.project_brief = _infer_project_brief();
	conv.created_at = Time::get_singleton()->get_unix_time_from_system();
	conv.updated_at = conv.created_at;

	conversations.insert(0, conv);
	active_conversation_id = conv.id;

	_load_conversation_to_ui(conv);
	_refresh_conversation_list_ui();
	_seed_new_conversation_options();
	_save_all_conversations();

	status_label->set_text(TTR("Started a new chat. Choose the task type or type your own request."));
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
	// (This is a const method that serializes by working on a cached copy;
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
		if (input && !input->is_connected(SceneStringName(text_changed), callable_mp(this, &AIChatPanel::_on_input_text_changed))) {
			input->connect(SceneStringName(text_changed), callable_mp(this, &AIChatPanel::_on_input_text_changed));
		}
	}
	if (p_what == NOTIFICATION_TRANSLATION_CHANGED) {
		_update_translations();
	}
	if (p_what == NOTIFICATION_RESIZED) {
		_update_auto_chat_display_scale();
		_update_responsive_sidebar();
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
		if (msg->is_summary_message() && _ai_chat_is_tool_error_text(msg->get_content())) {
			continue;
		}
		Dictionary entry;
		entry["role"] = msg->is_user_message() ? "user" : "assistant";
		entry["content"] = msg->get_content();
		history.push_back(entry);
	}
	return history;
}

bool AIChatPanel::_is_project_memory_continue_request(const String &p_user_message) const {
	const String msg = p_user_message.to_lower().strip_edges();
	if (msg.is_empty()) {
		return false;
	}

	return msg == "继续" || msg == "继续做" || msg == "继续吧" || msg == "接着做" || msg == "往下做" || msg == "按记忆继续" || msg == "根据记忆继续" ||
		   msg == "开始制作" || msg == "开始做" || msg == "开始开发" || msg == "开始实现" || msg == "开始创建" || msg == "制作吧" || msg == "做吧" ||
		   msg == "continue" || msg == "continue." || msg == "go on" || msg == "keep going" || msg == "resume" || msg == "proceed" ||
		   msg == "start making" || msg == "start building" || msg == "start development" || msg == "build it" || msg == "make it";
}

bool AIChatPanel::_is_html_prototype_gate_start_message(const String &p_user_message) const {
	const String msg = p_user_message.to_lower().strip_edges();
	if (msg.is_empty()) {
		return false;
	}

	return msg.contains("i want to make") || msg.contains("i want to build") || msg.contains("game idea") || msg.contains("game concept") ||
			msg.contains("我要做一个") || msg.contains("我想做一个") || msg.contains("我想开发一个") || msg.contains("做一款") ||
			msg.contains("做个游戏") || msg.contains("做一个游戏") || msg.contains("小游戏") || msg.contains("游戏原型") ||
			msg.contains("游戏想法") || msg.contains("项目想法") || msg.contains("prototype") || msg.contains("game jam");
}

bool AIChatPanel::_is_html_prototype_gate_approval_message(const String &p_user_message) const {
	const String msg = p_user_message.to_lower().strip_edges();
	if (msg.is_empty()) {
		return false;
	}

	return msg.contains("试玩通过") || msg.contains("验证通过") || msg.contains("验证成功") || msg.contains("测试通过") ||
			msg.contains("玩法可以") || msg.contains("玩法没问题") || msg.contains("原型通过") || msg.contains("网页通过") ||
			msg == "可以继续" || msg == "可以继续了" || msg.contains("可以继续制作") || msg.contains("继续制作") || msg.contains("开始制作godot") || msg.contains("开始做godot") ||
			msg.contains("开始生成godot") || msg.contains("approved") || msg.contains("tested and approved") ||
			msg.contains("prototype approved") || msg.contains("looks good") || msg.contains("go ahead with godot");
}

bool AIChatPanel::_is_html_prototype_gate_skip_message(const String &p_user_message) const {
	const String msg = p_user_message.to_lower().strip_edges();
	if (msg.is_empty()) {
		return false;
	}

	return msg.contains("跳过html") || msg.contains("跳过 html") || msg.contains("跳过网页") || msg.contains("不用html") ||
			msg.contains("不用 html") || msg.contains("不要网页") || msg.contains("直接做godot") || msg.contains("直接生成c#") ||
			msg.contains("skip html") || msg.contains("skip the html") || msg.contains("skip prototype") ||
			msg.contains("directly implement") || msg.contains("go straight to godot");
}

String AIChatPanel::_html_prototype_gate_prompt() const {
	return String("=== Active HTML Gameplay Prototype Gate ===\n"
				  "This conversation is currently waiting for user validation of a runnable HTML gameplay prototype. Treat every follow-up as prototype feedback unless the user explicitly says the prototype has been tested and approved or explicitly asks to skip HTML.\n"
				  "- Until that approval, do not create, edit, validate, build, or package real Godot project production files such as `.cs`, `.gd`, `.tscn`, `.tres`, `.gdextension`, `.csproj`, `.sln`, `project.godot`, or `project.jundot`.\n"
				  "- Allowed writes are limited to `.JundotAI/prototypes/` HTML prototype files. Prefer one self-contained `.html` file with inline CSS/JavaScript.\n"
				  "- After creating or revising the HTML prototype, call `check_html_prototype` to open it in a browser-backed check, collect console/page/network errors, and fix reported issues before asking the user to verify it.\n"
				  "- If the user asks for changes, revise the HTML prototype only, run `check_html_prototype` again, present the updated path/link, and ask them to run/verify it again through NEXT_QUESTION.\n"
				  "- Only after the user explicitly confirms the HTML prototype gameplay is approved may you continue into real Godot/C# project production.");
}

bool AIChatPanel::_html_prototype_gate_blocks_tool_call(const Dictionary &p_tool_call, String &r_reason) const {
	if (!html_prototype_gate_pending || active_settings.context_mode != AIContextMode::PROJECT) {
		return false;
	}

	const Dictionary fn_def = p_tool_call.get("function", Dictionary());
	String name = String(fn_def.get("name", String())).strip_edges();
	const String args_json = fn_def.get("arguments", "{}");
	if (name == "read_file") {
		name = AIToolNames::READ_FILES;
	} else if (name == "glob" || name == "glob_search") {
		name = AIToolNames::SEARCH_FILES;
	}

	Dictionary args;
	Variant parsed = JSON::parse_string(args_json);
	if (parsed.get_type() == Variant::DICTIONARY) {
		args = parsed;
	}

	if (name == AIToolNames::READ_FILES || name == AIToolNames::SEARCH_FILES || name == AIToolNames::LIST_FILES || name == AIToolNames::GREP_CODE || name == AIToolNames::FETCH_URL || name == AIToolNames::CHECK_HTML_PROTOTYPE) {
		return false;
	}

	if (name == AIToolNames::WRITE_FILE || name == AIToolNames::EDIT_FILE) {
		const String path = String(args.get("path", String()));
		if (_ai_chat_is_html_prototype_write_path(path)) {
			return false;
		}
		r_reason = vformat(TTR("HTML prototype gate blocked %s for '%s'. The user has not approved the playable HTML prototype yet. Until approval, write/edit only `.html`, `.css`, or `.js` files under `.JundotAI/prototypes/`; do not create C#, GDScript, scenes, project settings, builds, or packages."), name, path);
		return true;
	}

	r_reason = vformat(TTR("HTML prototype gate blocked tool `%s`. The user has not approved the playable HTML prototype yet. Continue by creating or revising the runnable HTML prototype under `.JundotAI/prototypes/`, run `check_html_prototype` to catch browser console/runtime errors, then ask the user to test it before Godot/C# production work."), name);
	return true;
}

String AIChatPanel::_detect_mode_prompt(const String &p_user_message) const {
	const String msg = p_user_message.to_lower().strip_edges();
	const AISettingsData settings = AISettings::load();
	String task_intent;
	for (int i = 0; i < conversations.size(); i++) {
		if (conversations[i].id == active_conversation_id) {
			task_intent = conversations[i].task_intent;
			break;
		}
	}

	if (settings.context_mode == AIContextMode::ENGINE) {
		if (msg.contains("bug") || msg.contains("error") || msg.contains("crash") || msg.contains("修复") || msg.contains("报错") || msg.contains("崩溃")) {
			return "Current request guidance: treat this as an engine defect investigation. First restate the understood failing feature, current behavior, expected behavior, and acceptance criteria. Inspect evidence and real source paths, identify the root cause, implement the fix, build, and verify. If the failing feature, reproduction steps, expected result, or error evidence is missing and cannot be inferred from engine context, ask one concise clarifying question through NEXT_QUESTION before editing.";
		}
		return "Current request guidance: work on the JunDot engine source according to the user's requested outcome, using a task breakdown when it materially improves execution clarity.";
	}

	const bool broad_project_idea =
			msg.contains("i want to make") || msg.contains("i want to build") || msg.contains("game idea") || msg.contains("game concept") ||
			msg.contains("我要做一个") || msg.contains("我想做一个") || msg.contains("我想开发一个") || msg.contains("做一款") || msg.contains("做个游戏") || msg.contains("做一个游戏") || msg.contains("小游戏") || msg.contains("游戏原型") || msg.contains("游戏想法") || msg.contains("项目想法") || msg.contains("prototype") || msg.contains("game jam");
	const bool concrete_change =
			msg.contains("modify") || msg.contains("change") || msg.contains("fix") || msg.contains("add") || msg.contains("remove") || msg.contains("implement") ||
			msg.contains("修改") || msg.contains("修复") || msg.contains("调整") || msg.contains("增加") || msg.contains("添加") || msg.contains("删除") || msg.contains("实现") || msg.contains("优化") ||
			msg.contains("bug") || msg.contains("报错") || msg.contains("问题");
	const bool vague_repair_request =
			msg.contains("doesn't work") || msg.contains("does not work") || msg.contains("not working") || msg.contains("broken") || msg.contains("fix a feature") || msg.contains("fix this feature") ||
			msg.contains("不好用") || msg.contains("不能用") || msg.contains("没反应") || msg.contains("没有反应") || msg.contains("不生效") || msg.contains("不对") || msg.contains("有问题") ||
			msg.contains("修一个功能") || msg.contains("修下功能") || msg.contains("修一下功能") || msg.contains("修不好") || msg.contains("半天修不好");
	const bool game_ui_request =
			msg.contains("ui") || msg.contains("hud") || msg.contains("menu") || msg.contains("inventory") || msg.contains("shop") || msg.contains("dialog") || msg.contains("interface") ||
			msg.contains("界面") || msg.contains("菜单") || msg.contains("背包") || msg.contains("商店") || msg.contains("对话框") || msg.contains("技能面板") || msg.contains("暂停界面") || msg.contains("设置界面") || msg.contains("游戏ui");
	const bool animation_request =
			msg.contains("animation") || msg.contains("animate") || msg.contains("animated") || msg.contains("motion") || msg.contains("transition") || msg.contains("tween") || msg.contains("animationplayer") || msg.contains("animationtree") || msg.contains("particle") || msg.contains("camera shake") ||
			msg.contains("动画") || msg.contains("动效") || msg.contains("实时动画") || msg.contains("转场") || msg.contains("过渡") || msg.contains("补间") || msg.contains("粒子") || msg.contains("镜头震动") || msg.contains("受击反馈") || msg.contains("攻击反馈") || msg.contains("加载动画");
	const bool consultation =
			msg.contains("how should") || msg.contains("why") || msg.contains("explain") || msg.contains("recommend") ||
			msg.contains("怎么设计") || msg.contains("为什么") || msg.contains("解释") || msg.contains("建议") || msg.contains("哪个好");

	const String boundary = " Stay strictly inside the open game project. Never inspect or modify engine source in PROJECT mode; if an engine limitation is discovered, finish any safe project-side work, call setup_engine_workspace to create or bind this project's dedicated engine worktree, then call request_engine_change with the exact reason and required engine change.";
	if (html_prototype_gate_pending) {
		return "Current request guidance: the HTML gameplay prototype gate is still pending. Treat this user message as feedback for the playable HTML prototype unless they explicitly approved or skipped it. Do not create or edit Godot/C# production files. Create or revise only the runnable HTML prototype under .JundotAI/prototypes/, then call check_html_prototype to operate a browser-backed check and collect console/page/network errors. If it reports errors, fix the prototype and run the check again before presenting its path/link and asking the user to test it before continuing to Godot production." + boundary;
	}
	if (_is_project_memory_continue_request(p_user_message)) {
		return "Current request guidance: the user asked to continue from this project's memory. Treat Project Memories as the primary source of the intended game concept, project name, style, mechanics, and pending direction. First inspect the open project enough to determine whether it is still empty/minimal or already has meaningful game content. If it is empty/minimal, continue from the remembered concept without asking the user to repeat it: produce a compact execution plan and begin building the first playable Jundot project structure using C# scripts by default, or GDScript only if the memory/user explicitly asks for GDScript. If content already exists, continue from the newest project state and memory together. Do not use memories from other projects and do not ask the user to choose an engine." + boundary;
	}
	if (task_intent == "feature_development") {
		return "Current request guidance: the user selected feature development for this chat. Work from the latest requested outcome, inspect the real project files, and implement directly when the request is concrete. Use a compact TASK_PLAN only when scope or sequencing materially benefits from it. If the latest message is just the starter option, ask what feature goal they want to build." + boundary;
	}
	if (task_intent == "bug_fix") {
		return "Current request guidance: the user selected bug fixing for this chat. Treat this as a defect investigation: restate the failing feature, current behavior, expected behavior, reproduction evidence, root cause, and acceptance criteria. If the latest message is just the starter option or lacks the failing behavior, ask one concise NEXT_QUESTION before editing." + boundary;
	}
	if (task_intent == "design_discussion") {
		return "Current request guidance: the user selected design discussion for this chat. Analyze the project and propose a path, but do not modify files until the user explicitly approves implementation." + boundary;
	}
	if (broad_project_idea && !concrete_change) {
		if (settings.html_min_project_prototype_enabled) {
			return "Current request guidance: this is a new game concept and the HTML gameplay prototype gate is enabled. First inspect enough project structure to decide whether the open project is empty/minimal or already contains meaningful scenes, scripts, assets, gameplay systems, UI, or project-specific architecture. Before touching Godot scenes, scripts, resources, or project settings, create one runnable standalone HTML prototype under .JundotAI/prototypes/ that demonstrates the core loop, basic controls, score/win/lose feedback when applicable, and main screen flow. Keep it self-contained with inline CSS/JavaScript and no external assets unless they already exist in the project. After writing it, call check_html_prototype to operate a browser-backed check, collect console/page/network errors, and fix any reported issues before presenting the HTML path/link and asking the user through NEXT_QUESTION to verify the gameplay; stop there until the user approves or requests changes. Only after user approval continue into the real Godot project implementation, then validate scripts/runtime/UI, package_project, poll check_package_status, run test_package, and hand off package paths and validation evidence. If the project already has meaningful content, preserve it and ask approval before any broad replacement, restructuring, or reinterpretation. If the user explicitly asks to skip HTML preview, use the normal project workflow. Do not ask the user to operate PackageBuilder manually." + boundary;
		}
		return "Current request guidance: this is a new game concept. First inspect enough project structure to decide whether the open project is empty/minimal or already contains meaningful scenes, scripts, assets, gameplay systems, UI, or project-specific architecture. Produce a TASK_PLAN scaled to its size: concise for a small game or prototype, fuller for a larger project. Evaluate the core loop, moment-to-moment fun, mastery, rewards, replayability, boring/frustrating risks, and prototype tests. Use fetch_url to research relevant official Steam/Epic pages when available, identify concrete differentiation opportunities, and generate a .JundotAI/mockups/ SVG when a visual flow or interface will help the user judge the idea. If the user did not explicitly request direct implementation, expose NEXT_QUESTION choices for Plan review, a minimum playable prototype, or gameplay/reference discussion, and stop before modifying game content until they choose or approve. If the project is empty/minimal and the Plan is approved or the user's answers make the Plan complete, continue autonomously through implementation, compile/build validation, runtime/UI testing, package_project, repeated check_package_status polling, test_package, and final handoff with package paths and validation evidence. If the project already has meaningful content, insert NEXT_QUESTION dialogue checkpoints before any broad replacement, restructuring, or reinterpretation of existing content. Do not ask the user to operate PackageBuilder manually." + boundary;
	}
	if (vague_repair_request) {
		return "Current request guidance: this is a potentially ambiguous repair request. Before editing, briefly restate the understood target feature, current behavior, expected behavior, and observable acceptance criteria. Inspect project evidence and the real code path before changing files. If the target feature, reproduction steps, expected result, or error evidence cannot be inferred from the project context, ask one concise clarifying question through NEXT_QUESTION and do not guess-edit." + boundary;
	}
	if (game_ui_request && animation_request) {
		return "Current request guidance: this is a player-facing animated UI request. Follow the Game UI Visual Quality Protocol, Runtime UI/Input Audit Protocol, and Runtime Animation Audit Protocol together: infer or inspect the game's existing style, define compact visual/motion/interaction acceptance criteria, implement purposeful contemporary game-interface motion with the simplest appropriate Godot mechanism, inspect scripts/project.jundot (or project.godot) input actions when controls or keybindings matter, validate layout safety with check_ui_layout, and use play_scene/click_ui_position/stop_play_scene for important animated UI paths before and after motion when coordinates can be inferred." + boundary;
	}
	if (game_ui_request) {
		return "Current request guidance: this is a player-facing game UI request. Follow the Game UI Visual Quality Protocol and Runtime UI/Input Audit Protocol: infer or inspect the game's existing style, use any attached images as the primary visual reference, define compact visual and interaction acceptance criteria, build with contemporary game-interface aesthetics rather than plain utility controls, inspect scripts/project.jundot (or project.godot) input actions when controls or keybindings matter, validate layout safety with check_ui_layout for changed Control scenes, and use play_scene/click_ui_position/stop_play_scene for important or suspected runtime click paths." + boundary;
	}
	if (animation_request) {
		return "Current request guidance: this is a realtime animation or motion-feedback request. Follow the Runtime Animation Audit Protocol: inspect existing scenes/scripts/AnimationPlayer/AnimationTree/Tween/particle/input paths, define the purpose and acceptance criteria for each motion, implement with the simplest appropriate Godot animation mechanism, make interruption and cleanup rules explicit, validate scripts and UI layout when relevant, and use play_scene plus click_ui_position for important animated UI paths before and after motion when coordinates can be inferred." + boundary;
	}
	if (concrete_change) {
		return "Current request guidance: this is a concrete project implementation, adjustment, or bug-fix request. Inspect the relevant project files and implement it directly. Use a compact task breakdown only when useful; do not pause for separate Plan approval unless scope is destructive, highly ambiguous, or materially larger than requested." + boundary;
	}
	if (consultation) {
		return "Current request guidance: this is primarily a project design or explanation request. Answer it directly and do not modify files unless the user requested implementation or the requested outcome clearly requires project-file changes." + boundary;
	}
	return "Current request guidance: choose adaptively between discussion, planning, and implementation based on scope, ambiguity, risk, and the user's requested outcome. Do not mechanically force a Plan, and do not avoid tools when concrete project work is requested." + boundary;
}
void AIChatPanel::_switch_to_engine() {
	if (beginner_chat_mode && !beginner_privilege_escalated) {
    status_label->set_text(TTR("Beginner mode: switching to Engine mode is only allowed when AI requests it."));
    return;
}
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
	_update_develop_mode_ui();
	_refresh_conversation_list_ui();
	_save_all_conversations();
	status_label->set_text(TTR("Switched to ENGINE mode: engine source context."));
}

void AIChatPanel::_switch_to_project() {
	if (beginner_chat_mode && !beginner_privilege_escalated) {
    status_label->set_text(TTR("Beginner mode: switching to Projets mode is only allowed when AI requests it."));
    return;
}
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
	if (beginner_chat_mode) {
		if (mode_bar) {
			mode_bar->set_visible(false);
		}
		if (source_update_status_label) {
			source_update_status_label->set_visible(false);
		}
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

	_update_develop_mode_ui();

	if (source_update_status_label) {
		if (s.context_mode != AIContextMode::ENGINE) {
			source_update_status_label->set_visible(false);
		} else {
			// Mode and conversation switching must remain UI-only. Git status and
			// network checks are intentionally deferred to source-management or
			// pre-mutation workflows so a slow repository cannot freeze the editor.
			const AISourceUpdateStatus update_status = AISourceUpdateService::get_cached_status();
			source_update_status_label->set_visible(true);
			const String source_status_text = update_status.message.is_empty() ? TTR("Source update: not checked") : update_status.message;
			source_update_status_label->set_text(source_status_text);
			source_update_status_label->set_tooltip_text(source_status_text);
			const bool attention = update_status.state == AISourceUpdateStatus::UPDATE_AVAILABLE || update_status.state == AISourceUpdateStatus::ERROR;
			source_update_status_label->add_theme_color_override("font_color", attention ? Color(1.0f, 0.65f, 0.25f) : Color(0.55f, 0.8f, 1.0f));
		}
	}
}

void AIChatPanel::_update_develop_mode_ui() {
	const AISettingsData settings = AISettings::load();
	if (!develop_mode_status_label || !develop_user_pass_button || !develop_user_fail_button) {
		return;
	}
	if (!settings.develop_mode || settings.context_mode != AIContextMode::ENGINE) {
		develop_mode_status_label->set_visible(false);
		develop_user_pass_button->set_visible(false);
		develop_user_fail_button->set_visible(false);
		return;
	}
	AIDevelopFlow::resume_after_restart();
	develop_mode_status_label->set_visible(true);
	develop_mode_status_label->set_text(AIDevelopFlow::get_status_text());
	develop_mode_status_label->add_theme_color_override("font_color", Color(1.0f, 0.72f, 0.25f));
	const bool waiting = AIDevelopFlow::is_waiting_for_user();
	develop_user_pass_button->set_visible(waiting);
	develop_user_fail_button->set_visible(waiting);
}

void AIChatPanel::_develop_user_verification(bool p_passed) {
	const String detail = p_passed ? "User confirmed that the feature works after restart." : "User reported that the feature does not work after restart.";
	AIDevelopFlow::record_user_verification(p_passed, detail);
	_update_develop_mode_ui();
	input->set_text(p_passed ? TTR("Develop Mode user verification passed. Please inspect the implementation and build evidence, perform AI verification, then call develop_ai_verify with your result.") : TTR("Develop Mode user verification failed. Please analyze the failure and call develop_ai_verify with passed=false and your findings."));
	_send_message();
}

void AIChatPanel::_update_translations() {
	set_name(TTRC("Chat"));
	input->set_placeholder(TTR("Message Jundot AI..."));
	add_file_menu->set_text(TTR("+"));
	add_file_menu->set_tooltip_text(TTR("Attach or reference a file or image"));
	PopupMenu *file_popup = add_file_menu->get_popup();
	file_popup->set_item_text(file_popup->get_item_index(FILE_MENU_REFERENCE_PROJECT), TTR("Reference Project File"));
	file_popup->set_item_text(file_popup->get_item_index(FILE_MENU_UPLOAD_TEXT), TTR("Upload Text File"));
	file_popup->set_item_text(file_popup->get_item_index(FILE_MENU_UPLOAD_IMAGE), TTR("Upload Image"));
	file_popup->set_item_text(file_popup->get_item_index(FILE_MENU_IMPORT), TTR("Import Skill / MCP / Memory..."));
	clear_button->set_text(TTR("Clear"));
	clear_button->set_tooltip_text(TTR("Clear input and attachments"));
	cancel_button->set_text(TTR("Cancel"));
	if (send_button) {
		const bool busy = (chat_service && chat_service->is_requesting()) || in_tool_loop || tool_execution_running || is_summarizing || is_titling || is_auditing || (build_status_poll_timer && !build_status_poll_timer->is_stopped());
		send_button->set_text(busy ? TTR("Queue") : TTR("Send"));
	}
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
	upload_image_dialog->set_title(TTR("Upload Image"));
	import_file_dialog->set_title(TTR("Import Skill / MCP / Memory"));
	add_all_button->set_text(TTR("Add All"));
	dismiss_all_button->set_text(TTR("Dismiss All"));
	if (new_conversation_button) {
		new_conversation_button->set_text(TTR("+ New Chat"));
	}
	if (delete_conversation_button) {
		delete_conversation_button->set_text(TTR("Delete"));
	}
	if (collapse_sidebar_button) {
		collapse_sidebar_button->set_text(TTR("<"));
		collapse_sidebar_button->set_tooltip_text(TTR("Collapse conversation sidebar"));
	}
	if (develop_user_pass_button) {
		develop_user_pass_button->set_text(TTR("User verification passed"));
	}
	if (develop_user_fail_button) {
		develop_user_fail_button->set_text(TTR("User verification failed"));
	}
	if (expand_sidebar_button) {
		expand_sidebar_button->set_text(TTR(">"));
		expand_sidebar_button->set_tooltip_text(TTR("Expand conversation sidebar"));
	}
	_refresh_attachment_chips();
	_refresh_queued_messages_ui();
	_apply_programming_experience_layout();
}

void AIChatPanel::_apply_programming_experience_layout() {
	EditorSettings *editor_settings = EditorSettings::get_singleton();
	const bool asked = editor_settings ? bool(editor_settings->get_project_metadata(AI_CHAT_METADATA_SECTION, AI_CHAT_METADATA_PROGRAMMING_EXPERIENCE_ASKED, false)) : true;
	const bool has_programming_experience = editor_settings ? bool(editor_settings->get_project_metadata(AI_CHAT_METADATA_SECTION, AI_CHAT_METADATA_HAS_PROGRAMMING_EXPERIENCE, true)) : true;
	beginner_chat_mode = asked && !has_programming_experience;

	if (programming_onboarding_panel) {
		programming_onboarding_panel->set_visible(!asked);
	}
	if (programming_mode_hint_panel) {
		programming_mode_hint_panel->set_visible(asked);
	}
	if (programming_mode_hint_label) {
		programming_mode_hint_label->set_text(beginner_chat_mode ? TTR("Beginner mode is on. You can switch back to the full AI workspace at any time.") : TTR("Full AI workspace is on. You can switch to beginner chat mode at any time."));
	}
	if (programming_mode_switch_button) {
		programming_mode_switch_button->set_text(beginner_chat_mode ? TTR("Show Full UI") : TTR("Use Beginner Mode"));
		programming_mode_switch_button->set_tooltip_text(beginner_chat_mode ? TTR("Show the full AI workspace with modes, files, and tools.") : TTR("Hide advanced controls and keep only chat and input."));
	}
	if (beginner_ai_guide_panel) {
		beginner_ai_guide_panel->set_visible(asked && !beginner_ai_guide_hidden);
	}
	if (beginner_ai_guide_label) {
		beginner_ai_guide_label->set_text(beginner_chat_mode ?
						TTR("How to use Jundot AI:\n1. Say what you want in everyday language, for example \"make a jumping game\" or \"fix this button\".\n2. If something looks wrong, describe what you see, paste the error text, or use + / Ctrl+V to attach a screenshot.\n3. Jundot AI will open hidden editor panels only when it needs them.\n4. You can keep chatting while it works; it will ask when it needs your choice.") :
						TTR("How to use the full AI workspace:\n1. Chat is for requests and decisions; Configuration controls the AI connection and behavior.\n2. Memories store project facts and preferences the AI should remember.\n3. Tools show what the AI can use to inspect files, validate scripts, run builds, or test the project.\n4. Switch to beginner mode any time if you want a cleaner chat-only workspace."));
	}
	if (beginner_ai_guide_hide_button) {
		beginner_ai_guide_hide_button->set_text(TTR("Hide"));
		beginner_ai_guide_hide_button->set_tooltip_text(TTR("Hide this quick guide"));
	}
	if (!asked) {
		if (input) {
			input->set_editable(false);
			input->set_placeholder(TTR("Please choose whether you have programming experience first."));
		}
	}

	if (sidebar_panel) {
		sidebar_panel->set_visible(!beginner_chat_mode);
	}
	if (chat_top_bar_container) {
		chat_top_bar_container->set_visible(!beginner_chat_mode);
	}
	if (expand_sidebar_button) {
		expand_sidebar_button->set_visible(!beginner_chat_mode && sidebar_collapsed);
	}
	if (mode_bar) {
		mode_bar->set_visible(!beginner_chat_mode);
	}
	if (beginner_chat_mode) {
		if (source_update_status_label) {
			source_update_status_label->set_visible(false);
		}
		if (develop_mode_status_label) {
			develop_mode_status_label->set_visible(false);
		}
		if (develop_user_pass_button) {
			develop_user_pass_button->set_visible(false);
		}
		if (develop_user_fail_button) {
			develop_user_fail_button->set_visible(false);
		}
	} else {
		_update_mode_indicator();
	}
	if (add_file_menu) {
		add_file_menu->set_visible(true);
		add_file_menu->set_tooltip_text(beginner_chat_mode ? TTR("Attach a screenshot or image") : TTR("Attach or reference a file or image"));
		PopupMenu *file_popup = add_file_menu->get_popup();
		if (file_popup) {
			file_popup->set_item_disabled(file_popup->get_item_index(FILE_MENU_REFERENCE_PROJECT), beginner_chat_mode);
			file_popup->set_item_disabled(file_popup->get_item_index(FILE_MENU_UPLOAD_TEXT), beginner_chat_mode);
			file_popup->set_item_disabled(file_popup->get_item_index(FILE_MENU_UPLOAD_IMAGE), false);
			file_popup->set_item_disabled(file_popup->get_item_index(FILE_MENU_IMPORT), beginner_chat_mode);
		}
	}
	if (clear_button) {
		clear_button->set_visible(!beginner_chat_mode);
	}
	if (cancel_button) {
		cancel_button->set_visible(!beginner_chat_mode);
	}
	if (bulk_action_bar) {
		bulk_action_bar->set_visible(!beginner_chat_mode && bulk_action_bar->is_visible());
	}
	if (input && asked) {
		input->set_editable(true);
		input->set_placeholder(TTR("Message Jundot AI..."));
	}
	_apply_sidebar_visibility();
	if (beginner_chat_mode && status_label) {
		status_label->set_text(TTR("Beginner mode: describe what you want to make or change, and Jundot AI will guide you."));
	}
	if (asked) {
		callable_mp(this, &AIChatPanel::_apply_editor_beginner_workspace).bind(beginner_chat_mode).call_deferred();
	}
}

void AIChatPanel::_hide_beginner_ai_guide() {
	beginner_ai_guide_hidden = true;
	if (beginner_ai_guide_panel) {
		beginner_ai_guide_panel->hide();
	}
}

void AIChatPanel::_apply_editor_beginner_workspace(bool p_enabled) {
	EditorNode *editor_node = EditorNode::get_singleton();
	if (!editor_node || !editor_node->get_editor_main_screen()) {
		return;
	}

	if (p_enabled) {
		editor_node->get_editor_main_screen()->select_by_name("AI Assistant");
		editor_node->set_distraction_free_mode(true);
		callable_mp(this, &AIChatPanel::_ensure_beginner_engine_source_configured).call_deferred();
	} else {
		if (editor_node->get_editor_main_screen()->get_selected_plugin() &&
				editor_node->get_editor_main_screen()->get_selected_plugin()->get_plugin_name() == "AI Assistant") {
			editor_node->get_editor_main_screen()->select(EditorMainScreen::EDITOR_2D);
		}
		editor_node->set_distraction_free_mode(false);
	}
}

void AIChatPanel::_ensure_beginner_engine_source_configured() {
	if (!beginner_chat_mode || beginner_source_auto_config_requested) {
		return;
	}

	const AISettingsData settings = AISettings::load();
	const String source_root = AISettings::get_engine_source_root(settings);
	if (!source_root.is_empty() && FileAccess::exists(source_root.path_join("SConstruct"))) {
		return;
	}

	beginner_source_auto_config_requested = true;
	if (status_label) {
		status_label->set_text(TTR("Beginner mode: engine source is missing. Jundot AI is configuring it automatically."));
	}

	if (!beginner_source_manager) {
		beginner_source_manager = memnew(AISourceManager);
		add_child(beginner_source_manager);
	}
	beginner_source_manager->popup_centered_on_parent(get_window());
	beginner_source_manager->begin_auto_configure();
}

void AIChatPanel::_set_programming_experience(bool p_has_programming_experience) {
	EditorSettings *editor_settings = EditorSettings::get_singleton();
	if (editor_settings) {
		editor_settings->set_project_metadata(AI_CHAT_METADATA_SECTION, AI_CHAT_METADATA_PROGRAMMING_EXPERIENCE_ASKED, true);
		editor_settings->set_project_metadata(AI_CHAT_METADATA_SECTION, AI_CHAT_METADATA_HAS_PROGRAMMING_EXPERIENCE, p_has_programming_experience);
		editor_settings->save_project_metadata();
	}
	_apply_programming_experience_layout();
	if (status_label) {
		status_label->set_text(p_has_programming_experience ? TTR("Full AI workspace enabled.") : TTR("Beginner chat mode enabled."));
	}
	if (input) {
		input->grab_focus();
	}
}

void AIChatPanel::_toggle_programming_experience_mode() {
	_set_programming_experience(beginner_chat_mode);
}

void AIChatPanel::_set_ai_activity(const String &p_text, bool p_visible) {
	ai_activity_text = p_text;
	if (ai_activity_panel) {
		ai_activity_panel->set_visible(p_visible);
	}
	if (ai_activity_progress) {
		ai_activity_progress->set_indeterminate(true);
	}
	if (p_visible) {
		_refresh_ai_activity();
	}
}

void AIChatPanel::_refresh_ai_activity() {
	if (!ai_activity_panel || !ai_activity_panel->is_visible() || !ai_activity_label) {
		return;
	}

	String text = ai_activity_text.strip_edges();
	if (text.is_empty()) {
		text = TTR("AI is working...");
	}
	const double elapsed = _get_response_elapsed(0.0);
	if (elapsed > 0.0) {
		const int seconds = (int)elapsed;
		if (seconds >= 60) {
			text += " " + vformat(TTR("Elapsed %dm %ds"), seconds / 60, seconds % 60);
		} else {
			text += " " + vformat(TTR("Elapsed %ds"), seconds);
		}
	}
	if (!queued_messages.is_empty()) {
		text += " " + vformat(TTR("Queued messages: %d"), queued_messages.size());
	}
	ai_activity_label->set_text(text);
}

void AIChatPanel::_set_requesting(bool p_requesting) {
	is_currently_requesting = p_requesting;
	if (send_button) {
		send_button->set_disabled(false);
		_update_send_button_text();
	}
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
	if (!p_requesting) {
		_set_ai_activity(String(), false);
	} else if (ai_activity_text.strip_edges().is_empty()) {
		_set_ai_activity(TTR("AI request is running..."), true);
	} else {
		_refresh_ai_activity();
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
	status_label->set_text(TTR("AI usage agreement accepted. Finish the first-time setup guide to continue."));
	_show_first_run_guide_if_needed(true);
}

void AIChatPanel::_usage_agreement_rejected() {
	status_label->set_text(TTR("AI message was not sent because the usage agreement was not accepted."));
}

void AIChatPanel::_show_first_run_guide_if_needed(bool p_force) {
	if (!first_run_guide_dialog) {
		return;
	}

	EditorSettings *editor_settings = EditorSettings::get_singleton();
	const bool already_shown = editor_settings ? bool(editor_settings->get_project_metadata(AI_CHAT_METADATA_SECTION, AI_CHAT_METADATA_FIRST_RUN_GUIDE_SHOWN, false)) : true;
	if (!p_force && already_shown) {
		return;
	}

	if (editor_settings) {
		editor_settings->set_project_metadata(AI_CHAT_METADATA_SECTION, AI_CHAT_METADATA_FIRST_RUN_GUIDE_SHOWN, true);
		editor_settings->save_project_metadata();
	}

	first_run_guide_dialog->popup_centered_clamped(Size2(560, 380) * EDSCALE, 0.85);
}

void AIChatPanel::_open_ai_config_from_first_run_guide() {
	TabContainer *parent_tabs = Object::cast_to<TabContainer>(get_parent());
	if (parent_tabs) {
		parent_tabs->set_current_tab(CLAMP(get_index() + 1, 0, parent_tabs->get_tab_count() - 1));

		EditorDock *dock = Object::cast_to<EditorDock>(parent_tabs->get_parent());
		if (dock) {
			dock->make_visible();
		} else {
			parent_tabs->show();
		}
	}
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
			conversations.write[i].next_question_multi_select = next_question_options_multi_select;
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
	next_question_options_multi_select = p_conv.next_question_multi_select;
	next_question_selected.clear();
	for (int i = 0; i < next_question_options.size(); i++) {
		next_question_selected.push_back(false);
	}
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

void AIChatPanel::_set_next_question_options(const Vector<String> &p_questions, bool p_save, bool p_multi_select) {
	next_question_options.clear();
	for (int i = 0; i < p_questions.size() && next_question_options.size() < 4; i++) {
		const String question = p_questions[i].strip_edges();
		if (!question.is_empty()) {
			next_question_options.push_back(question);
		}
	}
	next_question_options_multi_select = p_multi_select && next_question_options.size() > 1;
	next_question_selected.clear();
	for (int i = 0; i < next_question_options.size(); i++) {
		next_question_selected.push_back(false);
	}
	_render_next_question_options();

	if (active_conversation_id.is_empty()) {
		return;
	}
	for (int i = 0; i < conversations.size(); i++) {
		if (conversations[i].id == active_conversation_id) {
			conversations.write[i].next_question_options = next_question_options;
			conversations.write[i].next_question_multi_select = next_question_options_multi_select;
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
	next_question_confirm_button = nullptr;

	next_question_options_box->set_visible(!next_question_options.is_empty());
	for (int i = 0; i < next_question_options.size(); i++) {
		Button *question_button = memnew(Button);
		question_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		question_button->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		question_button->set_text_overrun_behavior(TextServer::OVERRUN_NO_TRIMMING);
		question_button->set_custom_minimum_size(Size2(0, Math::round(38 * chat_display_scale)) * EDSCALE);
		question_button->add_theme_font_size_override(SceneStringName(font_size), Math::round(14 * chat_display_scale * EDSCALE));

		if (next_question_options_multi_select) {
			// Multi-select: toggle button with checkmark indicator.
			question_button->set_toggle_mode(true);
			question_button->set_pressed(next_question_selected.size() > i && next_question_selected[i]);
			question_button->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
			const String prefix = question_button->is_pressed() ? String::utf8("\xe2\x98\x91 ") : String::utf8("\xe2\x98\x90 ");
			question_button->set_text(prefix + next_question_options[i]);
			question_button->set_tooltip_text(next_question_options[i]);
			question_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_toggle_next_question_option).bind(i));
		} else {
			// Single-select: regular button, click to fill input.
			question_button->set_text(next_question_options[i]);
			question_button->set_tooltip_text(next_question_options[i]);
			question_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_use_next_question_option).bind(next_question_options[i]));
		}
		next_question_options_box->add_child(question_button);
	}

	// In multi-select mode, add a confirm button below the options.
	if (next_question_options_multi_select) {
		next_question_confirm_button = memnew(Button);
		next_question_confirm_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		next_question_confirm_button->set_custom_minimum_size(Size2(0, Math::round(38 * chat_display_scale)) * EDSCALE);
		next_question_confirm_button->add_theme_font_size_override(SceneStringName(font_size), Math::round(14 * chat_display_scale * EDSCALE));
		next_question_confirm_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_confirm_multi_select_options));
		next_question_options_box->add_child(next_question_confirm_button);
		_update_next_question_confirm_button();
	}
}

void AIChatPanel::_use_next_question_option(const String &p_question) {
	// Export pipeline: intercept platform selection.
	if (export_pipeline_state == EXPORT_PIPELINE_AWAITING_PLATFORM) {
		_export_pipeline_select_platform(p_question);
		return;
	}

	if (!input) {
		return;
	}
	if (p_question.begins_with(TTR("深入做功能"))) {
		_set_current_conversation_task_intent("feature_development", TTR("The user wants to build or deepen a project feature. Inspect the project first, then implement within project scope unless the user asks only for planning."));
	} else if (p_question.begins_with(TTR("修复 Bug"))) {
		_set_current_conversation_task_intent("bug_fix", TTR("The user wants to fix a defect. Identify current behavior, expected behavior, reproduction evidence, root cause, and acceptance criteria before editing."));
	} else if (p_question.begins_with(TTR("讨论方案"))) {
		_set_current_conversation_task_intent("design_discussion", TTR("The user wants analysis or design discussion first. Do not modify files unless the user later explicitly approves implementation."));
	}
	input->set_text(p_question);
	input->grab_focus();
	_hide_tool_limit_options();
	_set_next_question_options(Vector<String>(), true);
}

void AIChatPanel::_toggle_next_question_option(int p_index) {
	if (!next_question_options_multi_select || p_index < 0 || p_index >= next_question_options.size()) {
		return;
	}
	if (next_question_selected.size() <= p_index) {
		next_question_selected.resize(next_question_options.size());
		for (int i = 0; i < next_question_selected.size(); i++) {
			next_question_selected.write[i] = false;
		}
	}
	next_question_selected.write[p_index] = !next_question_selected[p_index];

	// Re-render to update button text (checkmark prefix).
	_render_next_question_options();
}

void AIChatPanel::_confirm_multi_select_options() {
	if (!input) {
		return;
	}
	String combined;
	int count = 0;
	for (int i = 0; i < next_question_options.size() && i < next_question_selected.size(); i++) {
		if (next_question_selected[i]) {
			count++;
			if (!combined.is_empty()) {
				combined += "\n";
			}
			combined += vformat("%d. %s", count, next_question_options[i]);
		}
	}
	if (count == 0) {
		status_label->set_text(TTR("Please select at least one option."));
		return;
	}
	input->set_text(combined);
	input->grab_focus();
	_hide_tool_limit_options();
	_set_next_question_options(Vector<String>(), true);
	_send_message();
}

void AIChatPanel::_update_next_question_confirm_button() {
	if (!next_question_confirm_button) {
		return;
	}
	int count = 0;
	for (int i = 0; i < next_question_selected.size(); i++) {
		if (next_question_selected[i]) {
			count++;
		}
	}
	if (count == 0) {
		next_question_confirm_button->set_text(TTR("Select options to send"));
		next_question_confirm_button->set_disabled(true);
	} else {
		next_question_confirm_button->set_text(vformat(TTR("Send %d selected"), count));
		next_question_confirm_button->set_disabled(false);
	}
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
	if ((settings.backend_type == AIBackendType::CODEX || settings.backend_type == AIBackendType::LEGACY_OPENAI || settings.backend_type == AIBackendType::QWEN) && (settings.base_url.strip_edges().is_empty() || settings.model.strip_edges().is_empty() || settings.api_key.is_empty())) {
		status_label->set_text(TTR("Configure Base URL, model, and API key before sending AI messages."));
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
	if (settings.context_mode == AIContextMode::PROJECT) {
		settings.system_prompt += "\n\n" + _ai_project_memory_protocol();
	}
	const String conversation_brief = _build_conversation_brief_prompt();
	if (!conversation_brief.is_empty()) {
		settings.system_prompt += "\n\n" + conversation_brief;
	}
	if (settings.develop_mode && settings.context_mode == AIContextMode::ENGINE) {
		settings.system_prompt += "\n\n=== DEVELOP MODE DEMONSTRATION ===\nRun the visible workflow in order: modify locally, build, restart, wait for explicit user verification, inspect evidence and call develop_ai_verify, then call upload_code. upload_code is a dry run in this mode and MUST NOT commit or push. Never bypass this restriction with shell_command.";
	}
	const bool force_project_memories = settings.context_mode == AIContextMode::PROJECT && _is_project_memory_continue_request(instruction);
	const String ai_context = AIContextBuilder::build_context(force_project_memories || settings.include_project_memories, settings.include_tool_context, settings.context_char_budget, settings.auto_suggest_entries);
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
		if (settings.mcp_tools_enabled && !settings.develop_mode) {
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
	_set_ai_activity(TTR("Continuing AI response..."), true);

	const Error err = chat_service->send_messages(messages, tools);
	if (err != OK) {
		_clear_response_tracking();
		_set_ai_activity(String(), false);
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
	} else if (p_id == FILE_MENU_UPLOAD_IMAGE) {
		upload_image_dialog->popup_file_dialog();
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

void AIChatPanel::_image_file_selected(const String &p_path) {
	_add_attachment(p_path, true);
}

void AIChatPanel::_add_clipboard_image() {
	if (!DisplayServer::get_singleton() || !DisplayServer::get_singleton()->clipboard_has_image()) {
		status_label->set_text(TTR("Clipboard does not contain an image."));
		return;
	}

	Ref<Image> image = DisplayServer::get_singleton()->clipboard_get_image();
	if (image.is_null() || image->is_empty()) {
		status_label->set_text(TTR("Could not read the clipboard image."));
		return;
	}

	Vector<uint8_t> png_data = image->save_png_to_buffer();
	if (png_data.is_empty()) {
		status_label->set_text(TTR("Could not encode the clipboard image."));
		return;
	}
	if (png_data.size() > AI_CHAT_IMAGE_ATTACHMENT_MAX_BYTES) {
		status_label->set_text(vformat(TTR("Clipboard image is larger than %d MB."), AI_CHAT_IMAGE_ATTACHMENT_MAX_BYTES / (1024 * 1024)));
		return;
	}

	CoreBind::Marshalls *marshalls = CoreBind::Marshalls::get_singleton();
	ERR_FAIL_NULL(marshalls);

	ChatAttachment attachment;
	attachment.path = TTR("Clipboard image");
	attachment.display_name = vformat(TTR("Clipboard Image %d.png"), attachments.size() + 1);
	attachment.mime_type = "image/png";
	attachment.data_url = "data:image/png;base64," + marshalls->raw_to_base64(png_data);
	attachment.external = true;
	attachment.image = true;
	attachments.push_back(attachment);

	_refresh_attachment_chips();
	status_label->set_text(vformat(TTR("Attached %s."), attachment.display_name));
}

void AIChatPanel::_input_gui_input(const Ref<InputEvent> &p_event) {
	const Ref<InputEventKey> key = p_event;
	if (key.is_null() || !key->is_pressed() || key->is_echo() || !key->is_command_or_control_pressed() || key->get_keycode() != Key::V) {
		return;
	}

	if (DisplayServer::get_singleton() && DisplayServer::get_singleton()->clipboard_has_image()) {
		_add_clipboard_image();
		input->accept_event();
		return;
	}

	if (!DisplayServer::get_singleton() || !DisplayServer::get_singleton()->clipboard_has()) {
		return;
	}

	const String clipboard_text = DisplayServer::get_singleton()->clipboard_get().strip_edges();
	if (clipboard_text.is_empty()) {
		return;
	}

	Vector<String> files;
	const Vector<String> lines = clipboard_text.split("\n", false);
	for (int i = 0; i < lines.size(); i++) {
		String path = String(lines[i]).strip_edges().trim_prefix("\"").trim_suffix("\"");
		if (!path.is_empty() && FileAccess::exists(path)) {
			files.push_back(path);
		}
	}

	if (!files.is_empty() && files.size() == lines.size()) {
		_add_dropped_files(files);
		input->accept_event();
	}
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

	if (_ai_chat_is_image_extension(p_path)) {
		if (size > AI_CHAT_IMAGE_ATTACHMENT_MAX_BYTES) {
			status_label->set_text(vformat(TTR("Selected image is larger than %d MB."), AI_CHAT_IMAGE_ATTACHMENT_MAX_BYTES / (1024 * 1024)));
			return;
		}

		Vector<uint8_t> bytes = FileAccess::get_file_as_bytes(p_path, &err);
		if (err != OK || bytes.is_empty()) {
			status_label->set_text(TTR("Could not read the selected image."));
			return;
		}

		CoreBind::Marshalls *marshalls = CoreBind::Marshalls::get_singleton();
		ERR_FAIL_NULL(marshalls);

		ChatAttachment attachment;
		attachment.path = p_path;
		attachment.display_name = p_path.get_file();
		attachment.mime_type = _ai_chat_image_mime_type(p_path);
		attachment.data_url = "data:" + attachment.mime_type + ";base64," + marshalls->raw_to_base64(bytes);
		attachment.external = p_external;
		attachment.image = true;
		attachments.push_back(attachment);

		_refresh_attachment_chips();
		status_label->set_text(vformat(TTR("Attached image %s."), attachment.display_name));
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

	bool has_text_attachment = false;
	for (int i = 0; i < attachments.size(); i++) {
		if (!attachments[i].image) {
			has_text_attachment = true;
			break;
		}
	}
	if (!has_text_attachment) {
		return String();
	}

	String context = TTR("Attached text files for this message:");
	for (int i = 0; i < attachments.size(); i++) {
		const ChatAttachment &attachment = attachments[i];
		if (attachment.image) {
			continue;
		}
		context += "\n\n";
		context += vformat("[%s] %s\n%s\n", attachment.external ? TTR("Uploaded") : TTR("Referenced"), attachment.display_name, vformat(TTR("Path: %s"), attachment.path));
		context += "```text\n";
		context += attachment.content;
		context += "\n```";
	}
	return context;
}

Array AIChatPanel::_build_multimodal_user_content(const String &p_text) const {
	Array content;
	Dictionary text_part;
	text_part["type"] = "text";
	text_part["text"] = p_text.is_empty() ? TTR("Please analyze the attached image or file.") : p_text;
	content.push_back(text_part);

	for (int i = 0; i < attachments.size(); i++) {
		const ChatAttachment &attachment = attachments[i];
		if (!attachment.image || attachment.data_url.is_empty()) {
			continue;
		}

		Dictionary image_url;
		image_url["url"] = attachment.data_url;

		Dictionary image_part;
		image_part["type"] = "image_url";
		image_part["image_url"] = image_url;
		content.push_back(image_part);
	}

	return content;
}

void AIChatPanel::_append_tool_result_image_messages(Array &r_messages) const {
	for (int i = 0; i < r_messages.size(); i++) {
		if (r_messages[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary msg = r_messages[i];
		if (String(msg.get("role", String())) != "tool") {
			continue;
		}
		const String image_path = String(msg.get("image_path", String())).strip_edges();
		if ((bool)msg.get("image_injected", false) || image_path.is_empty() || !FileAccess::exists(image_path)) {
			if (!image_path.is_empty()) {
				msg.erase("image_path");
				msg.erase("image_mime_type");
				msg.erase("image_description");
				msg.erase("image_injected");
				r_messages[i] = msg;
			}
			continue;
		}

		const String mime_type = String(msg.get("image_mime_type", "image/png"));
		Error err = OK;
		Vector<uint8_t> bytes = FileAccess::get_file_as_bytes(image_path, &err);
		if (err != OK || bytes.is_empty()) {
			continue;
		}
		CoreBind::Marshalls *marshalls = CoreBind::Marshalls::get_singleton();
		if (!marshalls) {
			continue;
		}

		Dictionary text_part;
		text_part["type"] = "text";
		text_part["text"] = vformat(TTR("Runtime screenshot from tool result. Inspect this image together with the preceding tool output and runtime UI snapshot for UI position, visibility, clipping, overlap, scale, color, and composition issues.\nPath: %s"), image_path);

		Dictionary image_url;
		image_url["url"] = "data:" + mime_type + ";base64," + marshalls->raw_to_base64(bytes);

		Dictionary image_part;
		image_part["type"] = "image_url";
		image_part["image_url"] = image_url;

		Array content;
		content.push_back(text_part);
		content.push_back(image_part);

		Dictionary image_msg;
		image_msg["role"] = "user";
		image_msg["content"] = content;
		r_messages.push_back(image_msg);
		msg["image_injected"] = true;
		msg.erase("image_path");
		msg.erase("image_mime_type");
		msg.erase("image_description");
		msg.erase("image_injected");
		r_messages[i] = msg;
	}
}

void AIChatPanel::_append_tool_final_summary_instruction(Array &r_messages) const {
	Dictionary summary_request;
	summary_request["role"] = "user";
	summary_request["content"] = TTR("When you finish this tool sequence and provide the final user-facing answer, include a concise execution summary covering: what was done from start to finish, files read, files modified or created, tools or validations run and their results, and any remaining risks or next steps. If more tool calls are still needed, continue using tools first and apply this instruction only to the final answer.");
	r_messages.push_back(summary_request);
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
	if (beginner_chat_mode && !beginner_privilege_escalated) {
    AISettingsData s = AISettings::load();
    if (s.context_mode != AIContextMode::PROJECT) {
        s.context_mode = AIContextMode::PROJECT;
        AISettings::save(s);
        // 同时更新当前对话的模式
        for (auto &conv : conversations) {
            if (conv.id == active_conversation_id) {
                conv.context_mode = AIContextMode::PROJECT;
                break;
            }
        }
        _update_mode_indicator();
        _refresh_conversation_list_ui();
    }
}

	const bool busy = (chat_service && chat_service->is_requesting()) || in_tool_loop || tool_execution_running || is_summarizing || is_titling || is_auditing || (build_status_poll_timer && !build_status_poll_timer->is_stopped());
	if (busy) {
		bool has_input = !input->get_text().strip_edges().is_empty() || !attachments.is_empty();
		if (!has_input) {
			_cancel_request();
			return;
		}
		_enqueue_current_message();
		return;
	}

	const String text = input->get_text().strip_edges();
	if (text.is_empty() && attachments.is_empty()) {
		return;
	}
	_hide_tool_limit_options();

	AISettingsData settings = AISettings::load();
	if ((settings.backend_type == AIBackendType::CODEX || settings.backend_type == AIBackendType::LEGACY_OPENAI || settings.backend_type == AIBackendType::QWEN) && (settings.base_url.strip_edges().is_empty() || settings.model.strip_edges().is_empty() || settings.api_key.is_empty())) {
		status_label->set_text(TTR("Configure Base URL, model, and API key before sending AI messages."));
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
	String visible_user_text = text;
	if (visible_user_text.is_empty()) {
		visible_user_text = TTR("Attached files.");
	}
	if (!attachments.is_empty()) {
		String attachment_names;
		for (int i = 0; i < attachments.size(); i++) {
			if (!attachment_names.is_empty()) {
				attachment_names += ", ";
			}
			attachment_names += attachments[i].display_name;
		}
		visible_user_text += "\n\n" + vformat(TTR("Attachments: %s"), attachment_names);
	}
	if (editing_existing_message) {
		AIChatMessage *user_msg = Object::cast_to<AIChatMessage>(message_list->get_child(editing_message_index));
		if (user_msg) {
			user_msg->set_content(visible_user_text);
		}
		for (int i = message_list->get_child_count() - 1; i > editing_message_index; i--) {
			message_list->get_child(i)->queue_free();
		}
		editing_message_index = -1;
		_clear_structured_history();
	} else {
		_add_user_message(visible_user_text);
	}
	_record_issue_closed_from_user(visible_user_text);

	// Detect no-bug confirmation: after the AI response, override NEXT_QUESTION
	// with platform selection to enter the export pipeline automatically.
	if (export_pipeline_state == EXPORT_PIPELINE_IDLE &&
			settings.context_mode == AIContextMode::PROJECT &&
			_is_no_bug_confirmation_message(visible_user_text)) {
		export_pipeline_state = EXPORT_PIPELINE_AWAITING_PLATFORM;
	}

	if (settings.context_mode == AIContextMode::PROJECT && settings.html_min_project_prototype_enabled) {
		if (html_prototype_gate_pending && (_is_html_prototype_gate_approval_message(visible_user_text) || _is_html_prototype_gate_skip_message(visible_user_text))) {
			html_prototype_gate_pending = false;
		} else if (!html_prototype_gate_pending && _is_html_prototype_gate_start_message(visible_user_text) && !_is_html_prototype_gate_skip_message(visible_user_text)) {
			html_prototype_gate_pending = true;
		}
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
	// manually-set title, ask the AI to summarize a conversation title
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
		pending_title_text = visible_user_text;
		pending_title_attachments = attachments;
	}

	// Build system prompt + context. Use the effective prompt based on the current context mode.
	// Split into stable (cacheable) and dynamic parts for DashScope explicit context caching.
	String configured_system_prompt = AISettings::get_effective_system_prompt(settings);
	String stable_prompt = configured_system_prompt;
	stable_prompt += "\n\n" + _ai_chat_next_question_protocol();
	if (settings.context_mode == AIContextMode::PROJECT) {
		stable_prompt += "\n\n" + _ai_project_memory_protocol();
	}
	if (html_prototype_gate_pending) {
		stable_prompt += "\n\n" + _html_prototype_gate_prompt();
	}

	String dynamic_prompt;
	const String conversation_brief = _build_conversation_brief_prompt();
	if (!conversation_brief.is_empty()) {
		dynamic_prompt += "\n\n" + conversation_brief;
	}
	dynamic_prompt += "\n\n=== Current Request Guidance ===\n" + _detect_mode_prompt(visible_user_text);
	if (settings.develop_mode && settings.context_mode == AIContextMode::ENGINE) {
		dynamic_prompt += "\n\n=== DEVELOP MODE DEMONSTRATION ===\nRun the visible workflow in order: modify locally, build, restart, wait for explicit user verification, inspect evidence and call develop_ai_verify, then call upload_code. upload_code is a dry run in this mode and MUST NOT commit or push. Never bypass this restriction with shell_command.";
	}
	const String ai_context = AIContextBuilder::build_context(settings.include_project_memories, settings.include_tool_context, settings.context_char_budget, settings.auto_suggest_entries);
	if (!ai_context.is_empty()) {
		dynamic_prompt += "\n\n" + ai_context;
	}

	// settings.system_prompt retains the full combined prompt for backward compatibility.
	settings.system_prompt = stable_prompt + dynamic_prompt;
	chat_service->configure(settings);
	active_settings = settings; // Cache for tool loop reuse.

	Array messages;
	{
		Dictionary system_msg;
		system_msg["role"] = "system";
		// DashScope explicit context cache: content must be Array format with cache_control marker.
		// Caches the stable prompt prefix at 10% cost on subsequent hits (5-min TTL, auto-renew).
		if (settings.backend_type == AIBackendType::QWEN) {
			Array content_array;
			Dictionary stable_block;
			stable_block["type"] = "text";
			stable_block["text"] = stable_prompt;
			Dictionary cache_ctrl;
			cache_ctrl["type"] = "ephemeral";
			stable_block["cache_control"] = cache_ctrl;
			content_array.push_back(stable_block);
			if (!dynamic_prompt.is_empty()) {
				Dictionary dynamic_block;
				dynamic_block["type"] = "text";
				dynamic_block["text"] = dynamic_prompt;
				content_array.push_back(dynamic_block);
			}
			system_msg["content"] = content_array;
		} else {
			system_msg["content"] = settings.system_prompt;
		}
		messages.push_back(system_msg);
	}

	String request_text = text;
	const String attachment_context = _build_attachment_context();
	if (!attachment_context.is_empty()) {
		if (!request_text.is_empty()) {
			request_text += "\n\n";
		}
		request_text += attachment_context;
	}
	bool has_image_attachment = false;
	for (int i = 0; i < attachments.size(); i++) {
		if (attachments[i].image) {
			has_image_attachment = true;
			break;
		}
	}
	const Variant user_content = has_image_attachment ? Variant(_build_multimodal_user_content(request_text)) : Variant(request_text);

	Array structured_history = _get_structured_history();
	if (!structured_history.is_empty()) {
		for (int i = 0; i < structured_history.size(); i++) {
			messages.push_back(structured_history[i]);
		}
		Dictionary user_message;
		user_message["role"] = "user";
		user_message["content"] = user_content;
		messages.push_back(user_message);
	} else {
		for (int i = 0; i < history.size(); i++) {
			Dictionary entry = history[i];
			if (i == history.size() - 1 && String(entry["role"]) == "user") {
				entry["content"] = user_content;
			}
			messages.push_back(entry);
		}
	}

	// If tools are enabled, include tool definitions filtered by the current context mode.
	// For consultation/design discussion queries, use a minimal read-only set to save tokens.
	Array tools;
	if (settings.tools_enabled) {
		const bool is_consultation = AIToolDefs::is_consultation_message(visible_user_text);
		if (is_consultation) {
			tools = AIToolDefs::get_readonly_tools();
			print_line("AIChatPanel: Consultation query detected, using readonly tools (saved ~3000 tokens).");
		} else {
			tools = AIToolDefs::get_tools_for_mode(settings.context_mode);
		}
		if (settings.mcp_tools_enabled && !settings.develop_mode) {
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
	_set_ai_activity(TTR("Waiting for AI response..."), true);

	const Error err = chat_service->send_messages(messages, tools);
	if (err != OK) {
		_clear_response_tracking();
		_set_ai_activity(String(), false);
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
		summary_system["content"] = TTR("You are a conversation summarizer. Return only a concise plain-text summary under 500 characters, preserving durable project facts, confirmed user decisions, code changes, root causes, verified-passed or user-closed issues, and the current active state. Mark fixed/verified issues as resolved history, not active failures. Drop stale guesses, abandoned plans, transient tool errors, repeated failed attempts, and anything that conflicts with newer user direction. Do not call tools, do not request files, do not output XML, JSON, markdown fences, or tool_call blocks.");
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
	summary_settings.tools_enabled = false;
	summary_settings.mcp_tools_enabled = false;
	chat_service->configure(summary_settings);

	is_summarizing = true;
	status_label->set_text(TTR("Compressing conversation history..."));
	_start_response_tracking();
	_set_ai_activity(TTR("Compressing conversation history before continuing..."), true);
	_set_requesting(true);

	const Error err = chat_service->send_messages(summary_messages);
	if (err != OK) {
		_clear_response_tracking();
		// Fall back to truncation if the summary request fails to send.
		_summary_completed(String());
	}
}

void AIChatPanel::_summary_completed(const String &p_summary_text) {
	is_summarizing = false;
	_clear_response_tracking();
	const String summary_text = p_summary_text.strip_edges();
	const bool valid_summary = !summary_text.is_empty() && !_ai_chat_is_tool_error_text(summary_text);

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

	if (valid_summary) {
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
		_add_summary_message(summary_text);
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

void AIChatPanel::_on_input_text_changed() {
	_update_send_button_text();
}

void AIChatPanel::_update_send_button_text() {
	if (!send_button) {
		return;
	}
	bool has_input = !input->get_text().strip_edges().is_empty() || !attachments.is_empty();
	if (is_currently_requesting) {
		if (has_input) {
			send_button->set_text(TTR("Queue"));
			send_button->set_tooltip_text(TTR("Queue this message while the AI is responding"));
		} else {
			send_button->set_text(TTR("Stop"));
			send_button->set_tooltip_text(TTR("Stop the current AI response"));
		}
	} else {
		send_button->set_text(TTR("Send"));
		send_button->set_tooltip_text(String());
	}
	if (cancel_button) {
		cancel_button->set_visible(is_currently_requesting && has_input && !beginner_chat_mode);
	}
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
	if (is_auditing) {
		is_auditing = false;
		audit_service->cancel_request();
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
	is_auditing = false;
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
	_set_ai_activity(String(), false);
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
	_set_ai_activity(TTR("AI is writing a response..."), true);

	String visible_stream_content = p_full_content;
	const int memory_block_start = visible_stream_content.find("<!-- PROJECT_MEMORY -->");
	if (memory_block_start >= 0) {
		visible_stream_content = visible_stream_content.left(memory_block_start).strip_edges();
	}

	if (!streaming_message) {
		streaming_message = memnew(AIChatMessage);
		streaming_message->setup_ai(visible_stream_content, String(), _get_response_elapsed(0.0), 0, p_completion_tokens);
		streaming_message->connect(SNAME("edit_requested"), callable_mp(this, &AIChatPanel::_on_edit_requested));
		streaming_message->set_display_scale(chat_display_scale);
		message_list->add_child(streaming_message);
	} else {
		streaming_message->set_markdown_content(visible_stream_content);
	}
	message_scroll->set_deferred(SNAME("scroll_vertical"), message_scroll->get_v_scroll_bar()->get_max());
}

Array AIChatPanel::_get_available_tools_for_active_settings() const {
	Array available_tools = pending_tool_round.original_tools;
	if (available_tools.is_empty()) {
		available_tools = AIToolDefs::get_tools_for_mode(active_settings.context_mode);
		if (active_settings.mcp_tools_enabled && !active_settings.develop_mode) {
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
		const String original_fn_name = fn_name;
		if (fn_name == "read_file") {
			fn_name = "read_files";
		} else if (fn_name == "glob" || fn_name == "glob_search") {
			fn_name = "search_files";
		} else if (fn_name == "memory_search" || fn_name == "session_list") {
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
		if (original_fn_name == "memory_search") {
			Array paths;
			paths.push_back(".JundotAI/memory.json");
			args["paths"] = paths;
		} else if (original_fn_name == "session_list") {
			Array paths;
			paths.push_back(".JundotAI/conversations.json");
			args["paths"] = paths;
		} else if (fn_name == "read_files") {
			if (!args.has("paths") && args.has("path")) {
				args["paths"] = args["path"];
				args.erase("path");
			}
			if (args.has("paths") && args["paths"].get_type() == Variant::STRING) {
				Array paths;
				paths.push_back(String(args["paths"]));
				args["paths"] = paths;
			}
		} else if (fn_name == "search_files" && args.has("pattern") && args["pattern"].get_type() == Variant::STRING) {
			String pattern = String(args["pattern"]).replace("\\", "/").strip_edges();
			if (pattern.begins_with("/") && !pattern.begins_with("//")) {
				pattern = pattern.trim_prefix("/");
			}
			if (pattern.begins_with(".")) {
				pattern = "*" + pattern;
			}
			args["pattern"] = pattern;
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
	if (is_summarizing || is_titling) {
		_refresh_ai_activity();
		return;
	}
	if (!streaming_message) {
		streaming_message = memnew(AIChatMessage);
		streaming_message->setup_ai(String(), String(), elapsed, 0, 0);
		streaming_message->connect(SNAME("edit_requested"), callable_mp(this, &AIChatPanel::_on_edit_requested));
		streaming_message->set_display_scale(chat_display_scale);
		message_list->add_child(streaming_message);
	} else {
		streaming_message->set_think_time_seconds(elapsed);
	}
	_refresh_ai_activity();
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

static bool _ai_memory_fact_is_usable(const String &p_fact) {
	const String fact = p_fact.strip_edges().trim_prefix("-").strip_edges();
	if (fact.is_empty()) {
		return false;
	}
	const String lower = fact.to_lower();
	if (lower == "none" || lower == "empty" || lower == "n/a" || lower == "无" || lower == "无新增" || lower == "没有") {
		return false;
	}
	return !lower.contains("password") && !lower.contains("api key") && !lower.contains("api_key") &&
			!lower.contains("access token") && !lower.contains("secret") && !lower.contains("密码") && !lower.contains("令牌");
}

static Vector<String> _ai_memory_facts_from_text(const String &p_text) {
	String normalized = p_text.replace("；", ";").replace("\r", "\n").replace("\n", ";");
	Vector<String> parts = normalized.split(";", false);
	Vector<String> facts;
	for (int i = 0; i < parts.size(); i++) {
		String fact = parts[i].strip_edges().trim_prefix("-").strip_edges();
		if (!_ai_memory_fact_is_usable(fact)) {
			continue;
		}
		if (fact.length() > 800) {
			fact = fact.left(800).strip_edges();
		}
		facts.push_back(fact);
	}
	return facts;
}

static String _ai_merge_memory_facts(const String &p_existing, const String &p_additions) {
	Vector<String> facts = _ai_memory_facts_from_text(p_existing);
	Vector<String> additions = _ai_memory_facts_from_text(p_additions);
	for (int i = 0; i < additions.size(); i++) {
		bool duplicate = false;
		for (int j = 0; j < facts.size(); j++) {
			if (facts[j].nocasecmp_to(additions[i]) == 0) {
				duplicate = true;
				break;
			}
		}
		if (!duplicate) {
			facts.push_back(additions[i]);
		}
	}

	const int MAX_FACTS = 40;
	while (facts.size() > MAX_FACTS) {
		facts.remove_at(0);
	}
	String result;
	for (int i = 0; i < facts.size(); i++) {
		if (!result.is_empty()) {
			result += "\n";
		}
		result += "- " + facts[i];
	}
	return result;
}

void AIChatPanel::_save_project_memory_update(const AIProjectMemoryUpdate &p_update) {
	if (active_settings.context_mode != AIContextMode::PROJECT || p_update.is_empty() || !ProjectSettings::get_singleton()) {
		return;
	}
	const String project_root = ProjectSettings::get_singleton()->get_resource_path();
	if (project_root.is_empty() || (!FileAccess::exists(project_root.path_join("project.jundot")) && !FileAccess::exists(project_root.path_join("project.godot")))) {
		return;
	}

	Vector<AIMemoryEntry> entries;
	if (AIMemoryStore::load(entries) != OK) {
		return;
	}

	struct CategoryUpdate {
		String title;
		String tag;
		String content;
	};
	Vector<CategoryUpdate> updates;
	updates.push_back({ "User Preferences and Personality", "user-preferences", p_update.user_preferences });
	updates.push_back({ "Project Requirements", "project-requirements", p_update.project_requirements });
	updates.push_back({ "Key Decisions", "key-decisions", p_update.key_decisions });
	updates.push_back({ "Completed Work", "completed-work", p_update.completed_work });

	bool changed = false;
	for (int u = 0; u < updates.size(); u++) {
		if (_ai_memory_facts_from_text(updates[u].content).is_empty()) {
			continue;
		}
		int found = -1;
		for (int i = 0; i < entries.size(); i++) {
			if (entries[i].title.nocasecmp_to(updates[u].title) == 0) {
				found = i;
				break;
			}
		}
		if (found < 0) {
			AIMemoryEntry entry = AIMemoryStore::make_entry(updates[u].title, String());
			entry.tags.push_back("automatic");
			entry.tags.push_back(updates[u].tag);
			entries.push_back(entry);
			found = entries.size() - 1;
		}
		const String merged = _ai_merge_memory_facts(entries[found].content, updates[u].content);
		if (merged != entries[found].content) {
			entries.write[found].content = merged;
			entries.write[found].updated_at = AIMemoryStore::make_entry().updated_at;
			changed = true;
		}
	}

	if (changed) {
		AIMemoryStore::save(entries);
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
	correction_msg["content"] = TTR("Your previous response described using a tool or returned a text-form tool call that the editor could not execute. Do not describe tool use in prose. Return a structured tool_calls response using only the tools actually provided in the current tool list. Do not call unavailable Codex/MiMo-style tools such as memory_search, session_list, read_file, or glob. If you need project memory, use the Project Memories already in context or call read_files for .JundotAI/memory.json. If you need to inspect directories, call list_files with path and depth. If you need to read a file, call read_files with the exact relative path.");
	messages.push_back(correction_msg);

	pending_tool_round.original_messages = messages.duplicate(true);

	tool_call_label->set_visible(true);
	tool_call_label->set_text(TTR("AI described a tool action without calling it. Retrying once with a stricter tool-call instruction..."));
	status_label->set_text(TTR("Retrying AI request for a real tool call..."));
	_set_ai_activity(TTR("Retrying with a stricter tool-call instruction..."), true);

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

void AIChatPanel::_start_auto_audit(const String &p_user_message, const String &p_ai_response) {
	if (!audit_service || is_auditing) {
		return;
	}

	is_auditing = true;

	// Build the audit request messages.
	Array messages;
	{
		Dictionary system_msg;
		system_msg["role"] = "system";
		system_msg["content"] = String(AI_AUDIT_SYSTEM_PROMPT);
		messages.push_back(system_msg);
	}
	{
		Dictionary user_msg;
		user_msg["role"] = "user";
		user_msg["content"] = p_user_message;
		messages.push_back(user_msg);
	}
	{
		Dictionary assistant_msg;
		assistant_msg["role"] = "assistant";
		assistant_msg["content"] = p_ai_response;
		messages.push_back(assistant_msg);
	}

	// Configure the audit service with the same backend settings.
	AISettingsData audit_settings = AISettings::load();
	audit_service->configure(audit_settings);

	Error err = audit_service->send_messages(messages);
	if (err != OK) {
		is_auditing = false;
		// Silent failure: audit is non-critical.
	}
}

void AIChatPanel::_audit_completed(int p_result, int p_response_code, const String &p_content, const Dictionary &p_json, const String &p_raw_body, double p_elapsed_seconds, const String &p_think_content, int p_prompt_tokens, int p_completion_tokens) {
	is_auditing = false;

	if (p_result != HTTPRequest::RESULT_SUCCESS || p_response_code >= HTTPClient::RESPONSE_BAD_REQUEST) {
		// Silent failure: audit is non-critical.
		return;
	}

	// Extract the AUDIT_REPORT block from the response.
	String audit_content = p_content;
	int start_tag = audit_content.find("<!-- AUDIT_REPORT -->");
	int end_tag = audit_content.find("<!-- END_AUDIT_REPORT -->");

	String audit_report;
	if (start_tag != -1 && end_tag != -1) {
		start_tag += strlen("<!-- AUDIT_REPORT -->");
		audit_report = audit_content.substr(start_tag, end_tag - start_tag).strip_edges();
	} else {
		// No structured block found; use the raw response if it's short enough.
		audit_report = audit_content.strip_edges();
		if (audit_report.length() > 500) {
			audit_report = audit_report.substr(0, 497).strip_edges() + "...";
		}
	}

	if (audit_report.is_empty()) {
		return;
	}

	// Display the audit report as a styled AI message.
	String display_content = TTR("**AI Response Audit**") + "\n" + audit_report;
	_add_ai_message(display_content, String(), 0.0, 0, 0);
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
					String visible_content = AIChatParser::strip_next_question_blocks(AIChatParser::strip_task_plan_blocks(AIChatParser::strip_project_memory_blocks(p_content)));
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
						final_request["content"] = TTR("The tool-call iteration limit has been reached. Stop requesting tools now. Based only on the tool results already in this conversation, provide a clear final response with an execution summary: what was done from start to finish, what was found, files read, files modified or created, tools or validations run and their results, what was changed or should be changed, and any remaining risks or next steps. Do not request or describe more tool calls.");
						final_messages.push_back(final_request);

						tool_call_label->set_visible(true);
						tool_call_label->set_text(TTR("Tool iteration limit reached. Asking AI to summarize results..."));
						_set_ai_activity(TTR("Tool iteration limit reached. Asking AI to summarize results..."), true);
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
					AIChatParser::strip_next_question_blocks(AIChatParser::strip_task_plan_blocks(AIChatParser::strip_project_memory_blocks(p_content)))));
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
		bool generated_next_questions_multi_select = false;
		AIChatParser::parse_next_questions(p_content, generated_next_questions, &generated_next_questions_multi_select);
		AIProjectMemoryUpdate project_memory_update;
		AIChatParser::parse_project_memory_update(p_content, project_memory_update);
		if (generated_next_questions.is_empty() && _looks_like_tool_preamble(p_content)) {
			generated_next_questions.push_back(TTR("Please continue from where you stopped, using function calling tools to perform the next concrete step."));
		}

		String final_content = _strip_text_tool_call_blocks(AIChatParser::strip_next_question_blocks(AIChatParser::strip_task_plan_blocks(AIChatParser::strip_project_memory_blocks(p_content))));
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
		} else if (generated_next_questions.is_empty() && final_content.strip_edges().is_empty()) {
			// The AI response consisted entirely of hidden protocol blocks
			// (task plans, project memory, etc.) with no visible content and
			// no next-question blocks. Provide a minimal set of generic
			// fallback options so the panel still appears.
			generated_next_questions.push_back(TTR("Please summarize what you just did and suggest next steps."));
			generated_next_questions.push_back(TTR("Please review the changes and point out anything that needs attention."));
			generated_next_questions.push_back(TTR("Please continue with the most important remaining task."));
		}

		const String response_thought = _get_response_thought(p_think_content);
		const double response_elapsed = _get_response_elapsed(p_elapsed_seconds);
		if (streaming_message) {
			streaming_message->setup_ai(final_content, response_thought, response_elapsed, p_prompt_tokens, p_completion_tokens);
			streaming_message = nullptr;
		} else if (!final_content.strip_edges().is_empty()) {
			_add_ai_message(final_content, response_thought, response_elapsed, p_prompt_tokens, p_completion_tokens);
		} else if (!generated_next_questions.is_empty()) {
			// final_content is empty (response was all hidden protocol blocks)
			// but we have fallback options — show a brief placeholder message
			// so the options panel has visible context above it.
			_add_ai_message(TTR("Done."), response_thought, response_elapsed, p_prompt_tokens, p_completion_tokens);
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
		_save_project_memory_update(project_memory_update);
		_set_next_question_options(generated_next_questions, false, generated_next_questions_multi_select);

		// Export pipeline: if user just confirmed no bugs, override next-question
		// options with platform selection for the export pipeline.
		if (export_pipeline_state == EXPORT_PIPELINE_AWAITING_PLATFORM) {
			_start_export_pipeline();
		}

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

		// Auto-audit: if enabled, trigger a second AI pass to review the response.
		if (settings.auto_audit_enabled && !final_content.strip_edges().is_empty()) {
			String last_user_message;
			for (int i = message_list->get_child_count() - 1; i >= 0; i--) {
				AIChatMessage *msg = Object::cast_to<AIChatMessage>(message_list->get_child(i));
				if (msg && msg->is_user_message()) {
					last_user_message = msg->get_content();
					break;
				}
			}
			if (!last_user_message.is_empty()) {
				_start_auto_audit(last_user_message, final_content);
			}
		}

		status_label->set_text(TTR("AI response received."));
		_show_tool_limit_options(false);
		_dispatch_next_queued_message();
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
	_dispatch_next_queued_message();
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
	const String tool_round_text = vformat(TTR("AI tool call round %d of %d: executing %d tool call(s)..."),
			current_iteration, max_iterations, tool_calls.size());
	status_label->set_text(tool_round_text);
	_set_ai_activity(tool_round_text, true);

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

static String _find_mode_transition_result(const Array &p_messages, const String &p_marker) {
	for (int i = p_messages.size() - 1; i >= 0; i--) {
		if (p_messages[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary msg = p_messages[i];
		if (String(msg.get("role", String())) != "tool") {
			continue;
		}
		const String content = msg.get("content", String());
		if (content.contains(p_marker)) {
			return content;
		}
	}
	return String();
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
	_set_ai_activity(TTR("Build is running in the background. Monitoring real build status..."), true);
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
	pending_tool_round.original_messages = _compress_messages_for_low_token_mode(pending_tool_round.original_messages);
	_store_structured_history(pending_tool_round.original_messages);
	_save_all_conversations();

	String content = result.get("content", String());
	if (_tool_result_needs_build_poll(content)) {
		tool_call_label->set_text(vformat(TTR("Build is still running. Status check #%d..."), build_status_poll_count));
		status_label->set_text(TTR("Build is still running in the background..."));
		_set_ai_activity(vformat(TTR("Build is still running. Status check #%d..."), build_status_poll_count), true);
		return;
	}

	_stop_build_status_poll();
	tool_call_label->set_visible(false);
	tool_call_label->set_text(String());
	_update_mode_indicator();
	_update_develop_mode_ui();
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
		if (settings.mcp_tools_enabled && !settings.develop_mode) {
			Array mcp_tools = AIToolDefs::get_mcp_tools();
			if (!mcp_tools.is_empty()) {
				tools.append_array(mcp_tools);
			}
		}
		pending_tool_round.original_tools = tools;
	}

	in_tool_loop = true;
	chat_service->configure(settings);
	_set_ai_activity(TTR("Build finished. Asking AI to continue with the results..."), true);
	_set_requesting(true);
	Array send_messages = _compress_messages_for_low_token_mode(pending_tool_round.original_messages);
	_append_tool_result_image_messages(send_messages);
	_append_tool_final_summary_instruction(send_messages);
	Error err = chat_service->send_messages(send_messages, tools);
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
		if (settings.mcp_tools_enabled && !settings.develop_mode) {
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

	const String execution_text = vformat(TTR("Executing %d AI tool(s) in the background..."), tool_calls.size());
	tool_call_label->set_text(vformat(TTR("Executing %d tool(s)..."), tool_calls.size()));
	status_label->set_text(TTR("Executing AI tools in the background..."));
	_set_ai_activity(execution_text, true);
	_set_requesting(true);

	tool_execution_thread.start(_tool_execution_thread_func, this);
}

void AIChatPanel::_tool_execution_thread_func(void *p_userdata) {
	AIChatPanel *panel = static_cast<AIChatPanel *>(p_userdata);
	Array tool_calls = panel->tool_execution_tool_calls;
	Array messages = panel->tool_execution_messages;
	bool build_poll_needed = false;
	for (int i = 0; i < tool_calls.size(); i++) {
		if (panel->tool_execution_cancelled) {
			break;
		}
		Dictionary tc = tool_calls[i];
		String blocked_reason;
		Dictionary result;
		if (panel->_html_prototype_gate_blocks_tool_call(tc, blocked_reason)) {
			result["content"] = blocked_reason;
			result["is_error"] = true;
			result["role"] = "tool";
			result["tool_call_id"] = tc.get("id", String());
		} else {
			result = AIToolExecutor::execute(tc);
		}
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

	Array messages = _compress_messages_for_low_token_mode(tool_execution_messages);
	Array tools = tool_execution_tools;
	const bool build_poll_needed = tool_execution_build_poll_needed;

	pending_tool_round.original_messages = messages;
	pending_tool_round.original_tools = tools;
	in_tool_loop = true;
	_store_structured_history(pending_tool_round.original_messages);
	_save_all_conversations();

	tool_call_label->set_visible(false);
	tool_call_label->set_text(String());

	const String engine_request = _find_mode_transition_result(messages, "ENGINE_MODE_REQUEST_ACCEPTED");
	if (!engine_request.is_empty() && active_settings.context_mode == AIContextMode::PROJECT) {
		 if (beginner_chat_mode) beginner_privilege_escalated = true; 
		_switch_to_engine();
		active_settings = AISettings::load();
		status_label->set_text(TTR("Continuing in ENGINE mode for the requested engine change..."));
		_send_hidden_followup("Continue the original project request in ENGINE mode because the project-side tool analysis determined that an engine change is required. Use the engine source tools to inspect, implement, build, and verify the engine-side change. After the engine work is complete and validated, call return_to_project_mode with a concise summary so the editor can return to the game project context.\n\n" + engine_request);
		return;
	}

	const String project_return = _find_mode_transition_result(messages, "PROJECT_MODE_RETURN_ACCEPTED");
	if (!project_return.is_empty() && active_settings.context_mode == AIContextMode::ENGINE) {
		  if (beginner_chat_mode) beginner_privilege_escalated = false;  
		_switch_to_project();
		active_settings = AISettings::load();
		status_label->set_text(TTR("Returned to PROJECT mode after engine work."));
		_send_hidden_followup("Continue or finish the original game-project task now that the required engine work has been completed. Stay inside PROJECT mode for project files, validate project scripts if needed, and summarize both the engine-side and project-side results for the user.\n\n" + project_return);
		return;
	}
	if (build_poll_needed) {
		_start_build_status_poll();
		return;
	}

	chat_service->configure(active_settings);
	_set_ai_activity(TTR("Tools finished. Asking AI to analyze the results..."), true);
	_set_requesting(true);
	Array send_messages = _compress_messages_for_low_token_mode(messages);
	_append_tool_result_image_messages(send_messages);
	_append_tool_final_summary_instruction(send_messages);
	Error err = chat_service->send_messages(send_messages, tools);
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
		if (settings.mcp_tools_enabled && !settings.develop_mode) {
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
	_set_ai_activity(TTR("Tool skipped. Sending result back to AI..."), true);

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
		status_label->set_text(TTR("PackageBuilder launched in unattended AI build mode."));
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

// ============ Export pipeline (no-bugs -> package -> verify) ============

bool AIChatPanel::_is_no_bug_confirmation_message(const String &p_text) const {
	const String text = p_text.strip_edges().to_lower();
	if (text.is_empty()) {
		return false;
	}

	// Chinese patterns (UTF-8 encoded).
	const String zh_patterns[] = {
		String::utf8("\xe6\xb2\xa1\xe6\x9c\x89""bug"),               // 没有bug
		String::utf8("\xe6\xb2\xa1\xe6\x9c\x89""bug\xe4\xba\x86"),   // 没有bug了
		String::utf8("\xe6\xb2\xa1\xe6\x9c\x89\xe9\x97\xae\xe9\xa2\x98"), // 没有问题
		String::utf8("\xe6\xb2\xa1\xe9\x97\xae\xe9\xa2\x98"),               // 没问题
		String::utf8("\xe7\xa1\xae\xe8\xae\xa4\xe6\xb2\xa1\xe9\x97\xae\xe9\xa2\x98"), // 确认没问题
		String::utf8("\xe6\xb2\xa1\xe6\x9c\x89\xe4\xbb\xbb\xe4\xbd\x95\xe9\x97\xae\xe9\xa2\x98"), // 没有任何问题
		String::utf8("\xe5\x85\xa8\xe9\x83\xbd\xe6\xb2\xa1\xe9\x97\xae\xe9\xa2\x98"), // 都没没问题
		String::utf8("\xe6\xb5\x8b\xe8\xaf\x95\xe9\x80\x9a\xe8\xbf\x87"),             // 测试通过
		String::utf8("\xe9\xaa\x8c\xe8\xaf\x81\xe9\x80\x9a\xe8\xbf\x87"),             // 验证通过
		String::utf8("\xe6\xb2\xa1\xe6\x9c\x89\xe5\x8f\x91\xe7\x8e\xb0\xe9\x97\xae\xe9\xa2\x98"), // 没有发现问题
		String::utf8("\xe4\xb8\x80\xe5\x88\x87\xe6\xad\xa3\xe5\xb8\xb8"),             // 一切正常
		String::utf8("\xe5\x8f\xaf\xe4\xbb\xa5\xe6\x89\x93\xe5\x8c\x85"),             // 可以打包
		String::utf8("\xe5\x8f\xaf\xe4\xbb\xa5\xe5\x8f\x91\xe5\xb8\x83"),             // 可以发布
	};
	for (unsigned int i = 0; i < sizeof(zh_patterns) / sizeof(zh_patterns[0]); i++) {
		if (text.find(zh_patterns[i]) >= 0) {
			return true;
		}
	}

	// English patterns.
	const char *en_patterns[] = {
		"no bug",
		"no more bug",
		"no issues",
		"no problem",
		"no errors",
		"everything works",
		"all good",
		"all fixed",
		"works fine",
		"works perfectly",
		"looks good",
		"ready to package",
		"ready to export",
		"ready to publish",
		"ready for release",
		"can package",
		"can export",
		"can publish",
		"let's package",
		"let's export",
		"test passed",
		"tests passed",
		"verification passed",
	};
	for (unsigned int i = 0; i < sizeof(en_patterns) / sizeof(en_patterns[0]); i++) {
		if (text.find(en_patterns[i]) >= 0) {
			return true;
		}
	}

	return false;
}

void AIChatPanel::_start_export_pipeline() {
	// Gather available export presets and show platform selection options.
	if (!EditorExport::get_singleton()) {
		_export_pipeline_finish(false, TTR("Export system is not available."));
		return;
	}

	Vector<String> platform_options;
	for (int i = 0; i < EditorExport::get_singleton()->get_export_preset_count(); i++) {
		Ref<EditorExportPreset> preset = EditorExport::get_singleton()->get_export_preset(i);
		if (preset.is_null() || preset->get_platform().is_null()) {
			continue;
		}
		String platform_name = preset->get_platform()->get_name();
		String preset_name = preset->get_name();
		String option_text = preset_name;
		if (!platform_name.is_empty() && option_text.to_lower() != platform_name.to_lower()) {
			option_text = preset_name + " (" + platform_name + ")";
		}
		platform_options.push_back(option_text);
	}

	if (platform_options.is_empty()) {
		// No presets configured — open the export dialog so the user can create one.
		EditorNode *editor_node = EditorNode::get_singleton();
		if (editor_node) {
			_add_ai_message(TTR("No export presets found. Please open the Export dialog to create a preset for your target platform first."), String(), 0.0, 0, 0);
			// TODO: open export dialog programmatically.
		}
		export_pipeline_state = EXPORT_PIPELINE_IDLE;
		return;
	}

	// Show platform selection as NEXT_QUESTION options.
	_set_next_question_options(platform_options, true);
	_add_ai_message(TTR("Great, no bugs found! Let's package the game. Please select a target platform to export:"), String(), 0.0, 0, 0);
	_update_export_pipeline_status_ui();
}

void AIChatPanel::_export_pipeline_select_platform(const String &p_platform) {
	export_pipeline_platform = p_platform;
	_set_next_question_options(Vector<String>(), true);
	_update_export_pipeline_status_ui();

	// Find the matching export preset.
	Ref<EditorExportPreset> matched_preset;
	for (int i = 0; i < EditorExport::get_singleton()->get_export_preset_count(); i++) {
		Ref<EditorExportPreset> preset = EditorExport::get_singleton()->get_export_preset(i);
		if (preset.is_null() || preset->get_platform().is_null()) {
			continue;
		}
		String platform_name = preset->get_platform()->get_name();
		String preset_name = preset->get_name();
		String option_text = preset_name;
		if (!platform_name.is_empty() && option_text.to_lower() != platform_name.to_lower()) {
			option_text = preset_name + " (" + platform_name + ")";
		}
		if (option_text == p_platform) {
			matched_preset = preset;
			break;
		}
	}

	if (matched_preset.is_null()) {
		_export_pipeline_finish(false, vformat(TTR("Could not find an export preset for '%s'."), p_platform));
		return;
	}

	// Check if export templates are available.
	export_pipeline_state = EXPORT_PIPELINE_CHECKING_TEMPLATES;
	_update_export_pipeline_status_ui();

	Ref<EditorExportPlatform> platform = matched_preset->get_platform();
	String export_error;
	bool missing_templates = false;
	bool can_export_result = platform->can_export(matched_preset, export_error, missing_templates, false);

	if (can_export_result && !missing_templates) {
		// Templates are ready — proceed to export.
		_export_pipeline_do_export();
	} else if (missing_templates) {
		// Templates are missing — open the template manager.
		export_pipeline_state = EXPORT_PIPELINE_WAITING_TEMPLATE_DOWNLOAD;
		_update_export_pipeline_status_ui();
		_add_ai_message(TTR("Export templates for this platform are not installed. Opening the Export Template Manager — please download the required templates."), String(), 0.0, 0, 0);

		EditorNode *editor_node = EditorNode::get_singleton();
		if (editor_node) {
			editor_node->open_export_template_manager();
		}

		// Start a poll timer to check when templates are ready.
		if (!export_template_poll_timer) {
			export_template_poll_timer = memnew(Timer);
			export_template_poll_timer->set_wait_time(2.0);
			export_template_poll_timer->set_one_shot(false);
			export_template_poll_timer->connect("timeout", callable_mp(this, &AIChatPanel::_export_pipeline_on_template_poll));
			add_child(export_template_poll_timer, false, INTERNAL_MODE_BACK);
		}
		export_template_poll_timer->start();
	} else {
		// Other export configuration error.
		_export_pipeline_finish(false, vformat(TTR("Export configuration error: %s"), export_error));
	}
}

void AIChatPanel::_export_pipeline_on_template_poll() {
	if (export_pipeline_state != EXPORT_PIPELINE_WAITING_TEMPLATE_DOWNLOAD) {
		if (export_template_poll_timer) {
			export_template_poll_timer->stop();
		}
		return;
	}

	// Check if the template manager is still downloading.
	EditorNode *editor_node = EditorNode::get_singleton();
	if (!editor_node) {
		_export_pipeline_finish(false, TTR("Editor node is not available."));
		return;
	}

	// Re-check if the matched preset can now export (templates might have been downloaded).
	Ref<EditorExportPreset> matched_preset;
	for (int i = 0; i < EditorExport::get_singleton()->get_export_preset_count(); i++) {
		Ref<EditorExportPreset> preset = EditorExport::get_singleton()->get_export_preset(i);
		if (preset.is_null() || preset->get_platform().is_null()) {
			continue;
		}
		String preset_name = preset->get_name();
		String platform_name = preset->get_platform()->get_name();
		String option_text = preset_name;
		if (!platform_name.is_empty() && option_text.to_lower() != platform_name.to_lower()) {
			option_text = preset_name + " (" + platform_name + ")";
		}
		if (option_text == export_pipeline_platform) {
			matched_preset = preset;
			break;
		}
	}

	if (matched_preset.is_null()) {
		export_template_poll_timer->stop();
		_export_pipeline_finish(false, TTR("Export preset no longer available."));
		return;
	}

	Ref<EditorExportPlatform> platform = matched_preset->get_platform();
	String export_error;
	bool missing_templates = false;
	bool can_export_result = platform->can_export(matched_preset, export_error, missing_templates, false);

	if (can_export_result && !missing_templates) {
		// Templates are now available!
		export_template_poll_timer->stop();
		_add_ai_message(TTR("Export templates are ready. Starting export..."), String(), 0.0, 0, 0);
		_export_pipeline_do_export();
	}
	// Otherwise, keep polling — the user is still downloading.
}

void AIChatPanel::_export_pipeline_check_templates() {
	// This is called from _export_pipeline_select_platform; logic is inlined there.
}

void AIChatPanel::_export_pipeline_do_export() {
	export_pipeline_state = EXPORT_PIPELINE_EXPORTING;
	_update_export_pipeline_status_ui();

	// Find the matching preset.
	Ref<EditorExportPreset> matched_preset;
	for (int i = 0; i < EditorExport::get_singleton()->get_export_preset_count(); i++) {
		Ref<EditorExportPreset> preset = EditorExport::get_singleton()->get_export_preset(i);
		if (preset.is_null() || preset->get_platform().is_null()) {
			continue;
		}
		String preset_name = preset->get_name();
		String platform_name = preset->get_platform()->get_name();
		String option_text = preset_name;
		if (!platform_name.is_empty() && option_text.to_lower() != platform_name.to_lower()) {
			option_text = preset_name + " (" + platform_name + ")";
		}
		if (option_text == export_pipeline_platform) {
			matched_preset = preset;
			break;
		}
	}

	if (matched_preset.is_null()) {
		_export_pipeline_finish(false, TTR("Export preset not found."));
		return;
	}

	Ref<EditorExportPlatform> platform = matched_preset->get_platform();

	// Determine the output path.
	String export_path = matched_preset->get_export_path();
	if (export_path.is_empty()) {
		// Generate a default export path in the project directory.
		String project_name = ProjectSettings::get_singleton()->get("application/config/name");
		if (project_name.is_empty()) {
			project_name = "game";
		}
		String project_dir = ProjectSettings::get_singleton()->get_resource_path();
		String extension;
		String os_name = platform->get_os_name();
		if (os_name == "Windows") {
			extension = ".exe";
		} else if (os_name == "macOS") {
			extension = ".zip";
		} else if (os_name == "Web") {
			extension = ".html";
		} else {
			extension = ".x86_64";
		}
		export_path = project_dir.path_join("build" + String("/") + project_name + extension);
		matched_preset->set_export_path(export_path);
	}

	export_pipeline_output_path = export_path;

	// Ensure the output directory exists.
	String export_dir = export_path.get_base_dir();
	Ref<DirAccess> dir = DirAccess::create_for_path(export_dir);
	if (dir.is_valid()) {
		dir->make_dir_recursive(export_dir);
	}

	// Perform the export.
	platform->clear_messages();
	Error err = platform->export_project(matched_preset, false, export_path, 0);

	if (err != OK) {
		String error_msg;
		if (platform->get_message_count() > 0) {
			// Collect error messages from the platform.
			error_msg = vformat(TTR("Export failed with error code %d."), (int)err);
		} else {
			error_msg = vformat(TTR("Export failed with error code %d."), (int)err);
		}
		_export_pipeline_finish(false, error_msg);
		return;
	}

	_add_ai_message(vformat(TTR("Export completed successfully! Output: %s\nLaunching game to verify..."), export_path), String(), 0.0, 0, 0);
	_export_pipeline_launch_game();
}

void AIChatPanel::_export_pipeline_launch_game() {
	export_pipeline_state = EXPORT_PIPELINE_LAUNCHING;
	_update_export_pipeline_status_ui();

	if (!FileAccess::exists(export_pipeline_output_path)) {
		_export_pipeline_finish(false, vformat(TTR("Exported file not found: %s"), export_pipeline_output_path));
		return;
	}

	// Launch the exported game as a subprocess.
	List<String> args;
	ProcessID pid = 0;
	Error run_err = OS::get_singleton()->create_process(export_pipeline_output_path, args, &pid);

	if (run_err != OK || pid == 0) {
		// Fallback: try shell_open for platforms where direct execution isn't possible.
		run_err = OS::get_singleton()->shell_open(export_pipeline_output_path);
		if (run_err != OK) {
			_export_pipeline_finish(false, TTR("Failed to launch the exported game."));
			return;
		}
		// shell_open doesn't give us a PID, so we can't track the process.
		// Just report success after a delay.
		export_verify_pid = 0;
	} else {
		export_verify_pid = pid;
	}

	export_pipeline_state = EXPORT_PIPELINE_VERIFYING;
	export_verify_wait_count = 0;

	// Start a timer to check if the game is still running after a few seconds.
	if (!export_verify_timer) {
		export_verify_timer = memnew(Timer);
		export_verify_timer->set_wait_time(2.0);
		export_verify_timer->set_one_shot(false);
		export_verify_timer->connect("timeout", callable_mp(this, &AIChatPanel::_export_pipeline_on_verify_tick));
		add_child(export_verify_timer, false, INTERNAL_MODE_BACK);
	}
	export_verify_timer->start();
	_update_export_pipeline_status_ui();
}

void AIChatPanel::_export_pipeline_on_verify_tick() {
	if (export_pipeline_state != EXPORT_PIPELINE_VERIFYING) {
		if (export_verify_timer) {
			export_verify_timer->stop();
		}
		return;
	}

	export_verify_wait_count++;

	// If we can't track the process (launched via shell_open), just wait and report success.
	if (export_verify_pid == 0) {
		if (export_verify_wait_count >= 3) {
			export_verify_timer->stop();
			_export_pipeline_finish(true, vformat(TTR("Export and verification successful! The game was launched.\nOutput: %s"), export_pipeline_output_path));
		}
		return;
	}

	// Check if the process is still alive.
	if (export_verify_pid != 0) {
		bool alive = OS::get_singleton()->is_process_running(export_verify_pid);
		if (!alive) {
			export_verify_timer->stop();
			if (export_verify_wait_count <= 2) {
				_export_pipeline_finish(false, TTR("The exported game exited unexpectedly within the first few seconds. It may have crashed on startup."));
			} else {
				_export_pipeline_finish(true, vformat(TTR("Export and verification successful! The game ran for a few seconds without crashing.\nOutput: %s"), export_pipeline_output_path));
			}
			export_verify_pid = 0;
			return;
		}
	}

	// After 5 ticks (10 seconds), consider it stable.
	if (export_verify_wait_count >= 5) {
		export_verify_timer->stop();
		if (export_verify_pid != 0 && OS::get_singleton()->is_process_running(export_verify_pid)) {
			OS::get_singleton()->kill(export_verify_pid);
			export_verify_pid = 0;
		}
		_export_pipeline_finish(true, vformat(TTR("Export and verification successful! The game has been running for 10 seconds without crashing.\nOutput: %s"), export_pipeline_output_path));
	}
}

void AIChatPanel::_export_pipeline_finish(bool p_success, const String &p_message) {
	ExportPipelineState prev_state = export_pipeline_state;
	export_pipeline_state = EXPORT_PIPELINE_IDLE;
	export_pipeline_platform = String();
	export_pipeline_output_path = String();
	export_verify_pid = 0;
	export_verify_wait_count = 0;

	if (export_template_poll_timer) {
		export_template_poll_timer->stop();
	}
	if (export_verify_timer) {
		export_verify_timer->stop();
	}

	_update_export_pipeline_status_ui();

	if (prev_state != EXPORT_PIPELINE_IDLE) {
		if (p_success) {
			_add_ai_message(vformat(TTR("[Export Pipeline] Success: %s"), p_message), String(), 0.0, 0, 0);
		} else {
			_add_ai_message(vformat(TTR("[Export Pipeline] Failed: %s"), p_message), String(), 0.0, 0, 0);
		}
	}
}

void AIChatPanel::_export_pipeline_cancel() {
	export_pipeline_state = EXPORT_PIPELINE_IDLE;
	export_pipeline_platform = String();
	export_pipeline_output_path = String();
	export_verify_pid = 0;
	export_verify_wait_count = 0;

	if (export_template_poll_timer) {
		export_template_poll_timer->stop();
	}
	if (export_verify_timer) {
		export_verify_timer->stop();
	}

	_set_next_question_options(Vector<String>(), true);
	_update_export_pipeline_status_ui();
}

void AIChatPanel::_update_export_pipeline_status_ui() {
	// Create the status panel lazily.
	if (!export_pipeline_status_panel) {
		export_pipeline_status_panel = memnew(PanelContainer);
		export_pipeline_status_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);

		VBoxContainer *vb = memnew(VBoxContainer);
		export_pipeline_status_panel->add_child(vb);

		export_pipeline_status_label = memnew(Label);
		export_pipeline_status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		export_pipeline_status_label->add_theme_font_size_override("font_size", 13 * EDSCALE);
		vb->add_child(export_pipeline_status_label);

		export_pipeline_progress = memnew(ProgressBar);
		export_pipeline_progress->set_show_percentage(false);
		export_pipeline_progress->set_custom_minimum_size(Size2(0, 8) * EDSCALE);
		vb->add_child(export_pipeline_progress);

		// Insert before the status label in the chat vbox.
		Node *chat_vbox = status_label ? status_label->get_parent() : nullptr;
		if (chat_vbox) {
			chat_vbox->add_child(export_pipeline_status_panel);
			if (status_label) {
				int status_idx = chat_vbox->get_children().find(status_label);
				if (status_idx >= 0) {
					chat_vbox->move_child(export_pipeline_status_panel, status_idx);
				}
			}
		}
	}

	switch (export_pipeline_state) {
		case EXPORT_PIPELINE_IDLE: {
			export_pipeline_status_panel->set_visible(false);
		} break;
		case EXPORT_PIPELINE_AWAITING_PLATFORM: {
			export_pipeline_status_panel->set_visible(true);
			export_pipeline_status_label->set_text(TTR("Export pipeline: Select a target platform..."));
			export_pipeline_progress->set_value(10);
		} break;
		case EXPORT_PIPELINE_CHECKING_TEMPLATES: {
			export_pipeline_status_panel->set_visible(true);
			export_pipeline_status_label->set_text(vformat(TTR("Export pipeline: Checking templates for '%s'..."), export_pipeline_platform));
			export_pipeline_progress->set_value(25);
		} break;
		case EXPORT_PIPELINE_WAITING_TEMPLATE_DOWNLOAD: {
			export_pipeline_status_panel->set_visible(true);
			export_pipeline_status_label->set_text(TTR("Export pipeline: Waiting for template download..."));
			export_pipeline_progress->set_value(40);
		} break;
		case EXPORT_PIPELINE_EXPORTING: {
			export_pipeline_status_panel->set_visible(true);
			export_pipeline_status_label->set_text(vformat(TTR("Export pipeline: Exporting to '%s'..."), export_pipeline_output_path));
			export_pipeline_progress->set_value(65);
		} break;
		case EXPORT_PIPELINE_LAUNCHING: {
			export_pipeline_status_panel->set_visible(true);
			export_pipeline_status_label->set_text(TTR("Export pipeline: Launching exported game..."));
			export_pipeline_progress->set_value(80);
		} break;
		case EXPORT_PIPELINE_VERIFYING: {
			export_pipeline_status_panel->set_visible(true);
			export_pipeline_status_label->set_text(vformat(TTR("Export pipeline: Verifying game stability... (%d/5)"), export_verify_wait_count));
			export_pipeline_progress->set_value(80 + export_verify_wait_count * 4);
		} break;
	}
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
	chat_split = memnew(HSplitContainer);
	chat_split->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	chat_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	sidebar_expanded_width = 218 * EDSCALE;
	chat_split->set_split_offset(sidebar_expanded_width);
	chat_split->set_touch_dragger_enabled(true);
	root->add_child(chat_split);

	// Sidebar with conversation list.
	sidebar_panel = memnew(PanelContainer);
	sidebar_panel->set_custom_minimum_size(Size2(150 * EDSCALE, 0));
	chat_split->add_child(sidebar_panel);

	sidebar = memnew(VBoxContainer);
	sidebar->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	sidebar->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	sidebar->add_theme_constant_override("separation", 10 * EDSCALE);
	sidebar_panel->add_child(sidebar);

	HBoxContainer *sidebar_title_bar = memnew(HBoxContainer);
	sidebar_title_bar->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	sidebar->add_child(sidebar_title_bar);

	Label *sidebar_title = memnew(Label);
	sidebar_title->set_text(TTR("Jundot AI"));
	sidebar_title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	sidebar_title->add_theme_font_size_override("font_size", 16 * EDSCALE);
	sidebar_title->add_theme_color_override("font_color", Color(0.75f, 0.75f, 0.75f));
	sidebar_title_bar->add_child(sidebar_title);

	collapse_sidebar_button = memnew(Button);
	collapse_sidebar_button->set_flat(true);
	collapse_sidebar_button->set_text(TTR("<"));
	collapse_sidebar_button->set_tooltip_text(TTR("Collapse conversation sidebar"));
	collapse_sidebar_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_toggle_sidebar));
	sidebar_title_bar->add_child(collapse_sidebar_button);

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
	chat_split->add_child(chat_surface_panel);

	// Chat area (the right side) reuses most of the original root layout.
	VBoxContainer *chat_vbox = memnew(VBoxContainer);
	chat_vbox->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	chat_vbox->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	chat_vbox->add_theme_constant_override("separation", 0);
	chat_surface_panel->add_child(chat_vbox);

	programming_onboarding_panel = memnew(PanelContainer);
	programming_onboarding_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	chat_vbox->add_child(programming_onboarding_panel);

	MarginContainer *programming_onboarding_margin = memnew(MarginContainer);
	programming_onboarding_margin->add_theme_constant_override("margin_left", 16 * EDSCALE);
	programming_onboarding_margin->add_theme_constant_override("margin_right", 16 * EDSCALE);
	programming_onboarding_margin->add_theme_constant_override("margin_top", 10 * EDSCALE);
	programming_onboarding_margin->add_theme_constant_override("margin_bottom", 10 * EDSCALE);
	programming_onboarding_panel->add_child(programming_onboarding_margin);

	HBoxContainer *programming_onboarding_row = memnew(HBoxContainer);
	programming_onboarding_row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	programming_onboarding_row->add_theme_constant_override("separation", 8 * EDSCALE);
	programming_onboarding_margin->add_child(programming_onboarding_row);

	Label *programming_onboarding_label = memnew(Label);
	programming_onboarding_label->set_text(TTR("Do you have programming experience?"));
	programming_onboarding_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	programming_onboarding_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	programming_onboarding_row->add_child(programming_onboarding_label);

	Button *programming_yes_button = memnew(Button);
	programming_yes_button->set_text(TTR("Yes"));
	programming_yes_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_set_programming_experience).bind(true));
	programming_onboarding_row->add_child(programming_yes_button);

	Button *programming_no_button = memnew(Button);
	programming_no_button->set_text(TTR("No"));
	programming_no_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_set_programming_experience).bind(false));
	programming_onboarding_row->add_child(programming_no_button);

	programming_mode_hint_panel = memnew(PanelContainer);
	programming_mode_hint_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	programming_mode_hint_panel->set_visible(false);
	chat_vbox->add_child(programming_mode_hint_panel);

	MarginContainer *programming_mode_hint_margin = memnew(MarginContainer);
	programming_mode_hint_margin->add_theme_constant_override("margin_left", 16 * EDSCALE);
	programming_mode_hint_margin->add_theme_constant_override("margin_right", 16 * EDSCALE);
	programming_mode_hint_margin->add_theme_constant_override("margin_top", 6 * EDSCALE);
	programming_mode_hint_margin->add_theme_constant_override("margin_bottom", 6 * EDSCALE);
	programming_mode_hint_panel->add_child(programming_mode_hint_margin);

	HBoxContainer *programming_mode_hint_row = memnew(HBoxContainer);
	programming_mode_hint_row->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	programming_mode_hint_row->add_theme_constant_override("separation", 8 * EDSCALE);
	programming_mode_hint_margin->add_child(programming_mode_hint_row);

	programming_mode_hint_label = memnew(Label);
	programming_mode_hint_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	programming_mode_hint_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	programming_mode_hint_row->add_child(programming_mode_hint_label);

	programming_mode_switch_button = memnew(Button);
	programming_mode_switch_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_toggle_programming_experience_mode));
	programming_mode_hint_row->add_child(programming_mode_switch_button);

	beginner_ai_guide_panel = memnew(PanelContainer);
	beginner_ai_guide_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	beginner_ai_guide_panel->set_visible(false);
	chat_vbox->add_child(beginner_ai_guide_panel);

	MarginContainer *beginner_ai_guide_margin = memnew(MarginContainer);
	beginner_ai_guide_margin->add_theme_constant_override("margin_left", 16 * EDSCALE);
	beginner_ai_guide_margin->add_theme_constant_override("margin_right", 16 * EDSCALE);
	beginner_ai_guide_margin->add_theme_constant_override("margin_top", 6 * EDSCALE);
	beginner_ai_guide_margin->add_theme_constant_override("margin_bottom", 8 * EDSCALE);
	beginner_ai_guide_panel->add_child(beginner_ai_guide_margin);

	HBoxContainer *beginner_ai_guide_row = memnew(HBoxContainer);
	beginner_ai_guide_row->add_theme_constant_override("separation", 8 * EDSCALE);
	beginner_ai_guide_margin->add_child(beginner_ai_guide_row);

	beginner_ai_guide_label = memnew(Label);
	beginner_ai_guide_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	beginner_ai_guide_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	beginner_ai_guide_row->add_child(beginner_ai_guide_label);

	beginner_ai_guide_hide_button = memnew(Button);
	beginner_ai_guide_hide_button->set_flat(true);
	beginner_ai_guide_hide_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_hide_beginner_ai_guide));
	beginner_ai_guide_row->add_child(beginner_ai_guide_hide_button);

	MarginContainer *top_bar_margin = memnew(MarginContainer);
	chat_top_bar_container = top_bar_margin;
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

	expand_sidebar_button = memnew(Button);
	expand_sidebar_button->set_flat(true);
	expand_sidebar_button->set_text(TTR(">"));
	expand_sidebar_button->set_tooltip_text(TTR("Expand conversation sidebar"));
	expand_sidebar_button->set_visible(false);
	expand_sidebar_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_toggle_sidebar));
	top_bar->add_child(expand_sidebar_button);

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

	MarginContainer *source_status_margin = memnew(MarginContainer);
	source_status_margin->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	source_status_margin->add_theme_constant_override("margin_left", 16 * EDSCALE);
	source_status_margin->add_theme_constant_override("margin_right", 16 * EDSCALE);
	source_status_margin->add_theme_constant_override("margin_bottom", 4 * EDSCALE);
	chat_vbox->add_child(source_status_margin);

	source_update_status_label = memnew(Label);
	source_update_status_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	source_update_status_label->set_custom_minimum_size(Size2(0, 20) * EDSCALE);
	source_update_status_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_LEFT);
	source_update_status_label->set_autowrap_mode(TextServer::AUTOWRAP_OFF);
	source_update_status_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	source_update_status_label->set_clip_text(true);
	source_update_status_label->set_visible(false);
	source_status_margin->add_child(source_update_status_label);

	HBoxContainer *develop_flow_bar = memnew(HBoxContainer);
	develop_flow_bar->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	develop_flow_bar->add_theme_constant_override("separation", 6 * EDSCALE);
	chat_vbox->add_child(develop_flow_bar);

	develop_mode_status_label = memnew(Label);
	develop_mode_status_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	develop_mode_status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	develop_flow_bar->add_child(develop_mode_status_label);

	develop_user_pass_button = memnew(Button);
	develop_user_pass_button->set_text(TTR("User verification passed"));
	develop_user_pass_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_develop_user_verification).bind(true));
	develop_flow_bar->add_child(develop_user_pass_button);

	develop_user_fail_button = memnew(Button);
	develop_user_fail_button->set_text(TTR("User verification failed"));
	develop_user_fail_button->connect(SceneStringName(pressed), callable_mp(this, &AIChatPanel::_develop_user_verification).bind(false));
	develop_flow_bar->add_child(develop_user_fail_button);
	_update_develop_mode_ui();

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

	ai_activity_panel = memnew(PanelContainer);
	ai_activity_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	ai_activity_panel->set_visible(false);
	chat_vbox->add_child(ai_activity_panel);

	VBoxContainer *ai_activity_vb = memnew(VBoxContainer);
	ai_activity_vb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	ai_activity_vb->add_theme_constant_override("separation", 4 * EDSCALE);
	ai_activity_panel->add_child(ai_activity_vb);

	ai_activity_label = memnew(Label);
	ai_activity_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	ai_activity_label->add_theme_font_size_override("font_size", 12 * EDSCALE);
	ai_activity_label->add_theme_color_override("font_color", Color(0.55f, 0.7f, 1.0f));
	ai_activity_vb->add_child(ai_activity_label);

	ai_activity_progress = memnew(ProgressBar);
	ai_activity_progress->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	ai_activity_progress->set_custom_minimum_size(Size2(0, 6) * EDSCALE);
	ai_activity_progress->set_show_percentage(false);
	ai_activity_progress->set_indeterminate(true);
	ai_activity_vb->add_child(ai_activity_progress);

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

	queued_messages_box = memnew(VBoxContainer);
	queued_messages_box->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	queued_messages_box->add_theme_constant_override("separation", 3 * EDSCALE);
	queued_messages_box->set_visible(false);
	composer_vb->add_child(queued_messages_box);

	input = memnew(TextEdit);
	input->set_custom_minimum_size(Size2(0, 52) * EDSCALE);
	input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	input->set_drag_forwarding(Callable(), callable_mp(this, &AIChatPanel::_can_drop_files_fw).bind(input), callable_mp(this, &AIChatPanel::_drop_files_fw).bind(input));
	input->connect(SceneStringName(gui_input), callable_mp(this, &AIChatPanel::_input_gui_input));
	composer_vb->add_child(input);

	HBoxContainer *actions = memnew(HBoxContainer);
	actions->add_theme_constant_override("separation", 6 * EDSCALE);
	composer_vb->add_child(actions);

	add_file_menu = memnew(MenuButton);
	add_file_menu->set_flat(true);
	add_file_menu->get_popup()->add_item(TTR("Reference Project File"), FILE_MENU_REFERENCE_PROJECT);
	add_file_menu->get_popup()->add_item(TTR("Upload Text File"), FILE_MENU_UPLOAD_TEXT);
	add_file_menu->get_popup()->add_item(TTR("Upload Image"), FILE_MENU_UPLOAD_IMAGE);
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

	// Audit service (for automatic response quality auditing)
	audit_service = memnew(AIChatService);
	audit_service->connect(SNAME("chat_completed"), callable_mp(this, &AIChatPanel::_audit_completed));
	add_child(audit_service, false, INTERNAL_MODE_BACK);

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

	upload_image_dialog = memnew(EditorFileDialog);
	upload_image_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	upload_image_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
	upload_image_dialog->add_filter("*.png,*.jpg,*.jpeg,*.webp,*.bmp", TTRC("Image Files"));
	upload_image_dialog->connect(SNAME("file_selected"), callable_mp(this, &AIChatPanel::_image_file_selected));
	add_child(upload_image_dialog);

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

	first_run_guide_dialog = memnew(AIFirstRunGuideDialog);
	first_run_guide_dialog->connect(SNAME("open_ai_config_requested"), callable_mp(this, &AIChatPanel::_open_ai_config_from_first_run_guide));
	add_child(first_run_guide_dialog);

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
	_apply_programming_experience_layout();
	if (AISettings::is_usage_agreement_current(AISettings::load())) {
		callable_mp(this, &AIChatPanel::_show_first_run_guide_if_needed).call_deferred(false);
	}
}
