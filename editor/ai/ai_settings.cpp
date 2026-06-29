/*  ai_settings.cpp                                                       */
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

#include "ai_settings.h"

#include "core/config/project_settings.h"
#include "core/error/error_macros.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/string/translation_server.h"
#include "editor/file_system/editor_paths.h"
#include "editor/settings/editor_settings.h"

static constexpr const char *LEGACY_BASE_URL_KEY = "ai_assistant/base_url";
static constexpr const char *LEGACY_MODEL_KEY = "ai_assistant/model";
static constexpr const char *LEGACY_API_KEY_KEY = "ai_assistant/api_key";
static constexpr const char *LEGACY_TEMPERATURE_KEY = "ai_assistant/temperature";
static constexpr const char *LEGACY_MAX_TOKENS_KEY = "ai_assistant/max_tokens";

static constexpr const char *EDITOR_AI_BASE_URL_KEY = "ai_settings/provider/base_url";
static constexpr const char *EDITOR_AI_BACKEND_TYPE_KEY = "ai_settings/provider/backend_type";
static constexpr const char *EDITOR_AI_JUNDOT_PLUGIN_ID_KEY = "ai_settings/provider/jundot_plugin_id";
static constexpr const char *EDITOR_AI_JUNDOT_PLUGIN_URL_KEY = "ai_settings/provider/jundot_plugin_url";
static constexpr const char *EDITOR_AI_ALLOW_LEGACY_OPENAI_BACKEND_KEY = "ai_settings/provider/allow_legacy_openai_backend";
static constexpr const char *EDITOR_AI_MODEL_KEY = "ai_settings/provider/model";
static constexpr const char *EDITOR_AI_API_KEY_KEY = "ai_settings/provider/api_key";
static constexpr const char *EDITOR_AI_TEMPERATURE_KEY = "ai_settings/provider/temperature";
static constexpr const char *EDITOR_AI_MAX_TOKENS_KEY = "ai_settings/provider/max_tokens";
static constexpr const char *EDITOR_AI_OUTPUT_LANGUAGE_KEY = "ai_settings/general/output_language";
static constexpr const char *EDITOR_AI_HTML_MIN_PROJECT_PROTOTYPE_ENABLED_KEY = "ai_settings/project/enable_html_min_project_prototype";
static constexpr const char *EDITOR_AI_TOOLS_ENABLED_KEY = "ai_settings/tools/enable_function_calling";
static constexpr const char *EDITOR_AI_MCP_TOOLS_ENABLED_KEY = "ai_settings/tools/enable_mcp_tools";
static constexpr const char *EDITOR_AI_CONTEXT_CHAR_BUDGET_KEY = "ai_settings/context/context_char_budget";
static constexpr const char *EDITOR_AI_HISTORY_CHAR_BUDGET_KEY = "ai_settings/context/history_char_budget";
static constexpr const char *EDITOR_AI_MAX_TOOL_ITERATIONS_KEY = "ai_settings/context/max_tool_call_iterations";
static constexpr const char *EDITOR_AI_ENGINE_SOURCE_ROOT_KEY = "ai_settings/engine_source/source_root";
static constexpr const char *EDITOR_AI_ENGINE_SOURCE_CACHE_ROOT_KEY = "ai_settings/engine_source/cache_root";
static constexpr const char *EDITOR_AI_EXTERNAL_API_ENABLED_KEY = "ai_settings/external_api/enabled";
static constexpr const char *EDITOR_AI_EXTERNAL_API_PORT_KEY = "ai_settings/external_api/port";
static constexpr const char *EDITOR_AI_EXTERNAL_API_BIND_ADDRESS_KEY = "ai_settings/external_api/bind_address";

static String _get_system_output_language() {
	String locale;
	if (TranslationServer::get_singleton()) {
		locale = TranslationServer::get_singleton()->get_locale();
	}
	if (locale.is_empty() && OS::get_singleton()) {
		locale = OS::get_singleton()->get_locale();
	}

	locale = locale.replace_char('-', '_');
	const String language = locale.get_slicec('_', 0).to_lower();
	const String script_or_country = locale.get_slicec('_', 1);

	if (language == "zh") {
		if (script_or_country == "Hant" || script_or_country == "TW" || script_or_country == "HK" || script_or_country == "MO") {
			return "Traditional Chinese";
		}
		return "Simplified Chinese";
	}
	if (language == "en") {
		return "English";
	}
	if (language == "ja") {
		return "Japanese";
	}
	if (language == "ko") {
		return "Korean";
	}
	if (language == "es") {
		return "Spanish";
	}
	if (language == "fr") {
		return "French";
	}
	if (language == "de") {
		return "German";
	}

	if (TranslationServer::get_singleton() && !language.is_empty()) {
		const String language_name = TranslationServer::get_singleton()->get_language_name(language);
		if (!language_name.is_empty()) {
			return language_name;
		}
	}

	return "English";
}

static String _get_default_engine_source_cache_root() {
	if (!OS::get_singleton()) {
		return String();
	}
	String user_data_dir = OS::get_singleton()->get_user_data_dir();
	if (user_data_dir.is_empty()) {
		return String();
	}
	return user_data_dir.path_join("engine_source");
}

static bool _editor_setting_was_changed(const StringName &p_key) {
	EditorSettings *editor_settings = EditorSettings::get_singleton();
	if (!editor_settings || !editor_settings->has_setting(p_key)) {
		return false;
	}

	const Variant current_value = editor_settings->get_setting(p_key);
	const Variant default_value = editor_settings->property_get_revert(p_key);
	return current_value != default_value;
}

static bool _should_read_editor_setting(const StringName &p_key, bool p_only_changed) {
	EditorSettings *editor_settings = EditorSettings::get_singleton();
	if (!editor_settings || !editor_settings->has_setting(p_key)) {
		return false;
	}
	return !p_only_changed || _editor_setting_was_changed(p_key);
}

static AIBackendType _backend_type_from_string(const String &p_backend_type) {
	if (p_backend_type == "codex") {
		return AIBackendType::CODEX;
	}
	if (p_backend_type == "legacy_openai") {
		return AIBackendType::LEGACY_OPENAI;
	}
	return AIBackendType::JUNDOT_PLUGIN;
}

static String _backend_type_to_string(AIBackendType p_backend_type) {
	switch (p_backend_type) {
		case AIBackendType::CODEX:
			return "codex";
		case AIBackendType::LEGACY_OPENAI:
			return "legacy_openai";
		case AIBackendType::JUNDOT_PLUGIN:
		default:
			return "jundot_plugin";
	}
}

static void _apply_editor_settings(AISettingsData &r_settings, bool p_only_changed) {
	EditorSettings *editor_settings = EditorSettings::get_singleton();
	if (!editor_settings) {
		return;
	}

	if (_should_read_editor_setting(EDITOR_AI_BASE_URL_KEY, p_only_changed)) {
		r_settings.base_url = editor_settings->get_setting(EDITOR_AI_BASE_URL_KEY);
	}
	if (_should_read_editor_setting(EDITOR_AI_BACKEND_TYPE_KEY, p_only_changed)) {
		const String backend_type = String(editor_settings->get_setting(EDITOR_AI_BACKEND_TYPE_KEY));
		r_settings.backend_type = _backend_type_from_string(backend_type);
	}
	if (_should_read_editor_setting(EDITOR_AI_JUNDOT_PLUGIN_ID_KEY, p_only_changed)) {
		r_settings.jundot_ai_plugin_id = editor_settings->get_setting(EDITOR_AI_JUNDOT_PLUGIN_ID_KEY);
	}
	if (_should_read_editor_setting(EDITOR_AI_JUNDOT_PLUGIN_URL_KEY, p_only_changed)) {
		r_settings.jundot_ai_plugin_url = editor_settings->get_setting(EDITOR_AI_JUNDOT_PLUGIN_URL_KEY);
	}
	if (_should_read_editor_setting(EDITOR_AI_ALLOW_LEGACY_OPENAI_BACKEND_KEY, p_only_changed)) {
		r_settings.allow_legacy_openai_backend = editor_settings->get_setting(EDITOR_AI_ALLOW_LEGACY_OPENAI_BACKEND_KEY);
	}
	if (_should_read_editor_setting(EDITOR_AI_MODEL_KEY, p_only_changed)) {
		r_settings.model = editor_settings->get_setting(EDITOR_AI_MODEL_KEY);
	}
	if (_should_read_editor_setting(EDITOR_AI_API_KEY_KEY, p_only_changed)) {
		r_settings.api_key = editor_settings->get_setting(EDITOR_AI_API_KEY_KEY);
	}
	if (_should_read_editor_setting(EDITOR_AI_TEMPERATURE_KEY, p_only_changed)) {
		r_settings.temperature = editor_settings->get_setting(EDITOR_AI_TEMPERATURE_KEY);
	}
	if (_should_read_editor_setting(EDITOR_AI_MAX_TOKENS_KEY, p_only_changed)) {
		r_settings.max_tokens = editor_settings->get_setting(EDITOR_AI_MAX_TOKENS_KEY);
	}
	if (_should_read_editor_setting(EDITOR_AI_OUTPUT_LANGUAGE_KEY, p_only_changed)) {
		r_settings.output_language = editor_settings->get_setting(EDITOR_AI_OUTPUT_LANGUAGE_KEY);
	}
	if (_should_read_editor_setting(EDITOR_AI_HTML_MIN_PROJECT_PROTOTYPE_ENABLED_KEY, p_only_changed)) {
		r_settings.html_min_project_prototype_enabled = editor_settings->get_setting(EDITOR_AI_HTML_MIN_PROJECT_PROTOTYPE_ENABLED_KEY);
	}
	if (_should_read_editor_setting(EDITOR_AI_TOOLS_ENABLED_KEY, p_only_changed)) {
		r_settings.tools_enabled = editor_settings->get_setting(EDITOR_AI_TOOLS_ENABLED_KEY);
	}
	if (_should_read_editor_setting(EDITOR_AI_MCP_TOOLS_ENABLED_KEY, p_only_changed)) {
		r_settings.mcp_tools_enabled = editor_settings->get_setting(EDITOR_AI_MCP_TOOLS_ENABLED_KEY);
	}
	if (_should_read_editor_setting(EDITOR_AI_CONTEXT_CHAR_BUDGET_KEY, p_only_changed)) {
		r_settings.context_char_budget = editor_settings->get_setting(EDITOR_AI_CONTEXT_CHAR_BUDGET_KEY);
	}
	if (_should_read_editor_setting(EDITOR_AI_HISTORY_CHAR_BUDGET_KEY, p_only_changed)) {
		r_settings.history_char_budget = editor_settings->get_setting(EDITOR_AI_HISTORY_CHAR_BUDGET_KEY);
	}
	if (_should_read_editor_setting(EDITOR_AI_MAX_TOOL_ITERATIONS_KEY, p_only_changed)) {
		r_settings.max_tool_iterations = editor_settings->get_setting(EDITOR_AI_MAX_TOOL_ITERATIONS_KEY);
	}
	if (_should_read_editor_setting(EDITOR_AI_ENGINE_SOURCE_ROOT_KEY, p_only_changed)) {
		r_settings.engine_source_root = editor_settings->get_setting(EDITOR_AI_ENGINE_SOURCE_ROOT_KEY);
	}
	if (_should_read_editor_setting(EDITOR_AI_ENGINE_SOURCE_CACHE_ROOT_KEY, p_only_changed)) {
		r_settings.engine_source_cache_root = editor_settings->get_setting(EDITOR_AI_ENGINE_SOURCE_CACHE_ROOT_KEY);
	}
	if (_should_read_editor_setting(EDITOR_AI_EXTERNAL_API_ENABLED_KEY, p_only_changed)) {
		r_settings.external_api_enabled = editor_settings->get_setting(EDITOR_AI_EXTERNAL_API_ENABLED_KEY);
	}
	if (_should_read_editor_setting(EDITOR_AI_EXTERNAL_API_PORT_KEY, p_only_changed)) {
		r_settings.external_api_port = editor_settings->get_setting(EDITOR_AI_EXTERNAL_API_PORT_KEY);
	}
	if (_should_read_editor_setting(EDITOR_AI_EXTERNAL_API_BIND_ADDRESS_KEY, p_only_changed)) {
		r_settings.external_api_bind_address = editor_settings->get_setting(EDITOR_AI_EXTERNAL_API_BIND_ADDRESS_KEY);
	}
}

static void _write_editor_settings(const AISettingsData &p_settings) {
	EditorSettings *editor_settings = EditorSettings::get_singleton();
	if (!editor_settings) {
		return;
	}

	editor_settings->set_setting(EDITOR_AI_BASE_URL_KEY, p_settings.base_url);
	editor_settings->set_setting(EDITOR_AI_BACKEND_TYPE_KEY, _backend_type_to_string(p_settings.backend_type));
	editor_settings->set_setting(EDITOR_AI_JUNDOT_PLUGIN_ID_KEY, p_settings.jundot_ai_plugin_id);
	editor_settings->set_setting(EDITOR_AI_JUNDOT_PLUGIN_URL_KEY, p_settings.jundot_ai_plugin_url);
	editor_settings->set_setting(EDITOR_AI_ALLOW_LEGACY_OPENAI_BACKEND_KEY, p_settings.allow_legacy_openai_backend);
	editor_settings->set_setting(EDITOR_AI_MODEL_KEY, p_settings.model);
	editor_settings->set_setting(EDITOR_AI_API_KEY_KEY, p_settings.api_key);
	editor_settings->set_setting(EDITOR_AI_TEMPERATURE_KEY, p_settings.temperature);
	editor_settings->set_setting(EDITOR_AI_MAX_TOKENS_KEY, p_settings.max_tokens);
	editor_settings->set_setting(EDITOR_AI_OUTPUT_LANGUAGE_KEY, p_settings.output_language);
	editor_settings->set_setting(EDITOR_AI_HTML_MIN_PROJECT_PROTOTYPE_ENABLED_KEY, p_settings.html_min_project_prototype_enabled);
	editor_settings->set_setting(EDITOR_AI_TOOLS_ENABLED_KEY, p_settings.tools_enabled);
	editor_settings->set_setting(EDITOR_AI_MCP_TOOLS_ENABLED_KEY, p_settings.mcp_tools_enabled);
	editor_settings->set_setting(EDITOR_AI_CONTEXT_CHAR_BUDGET_KEY, p_settings.context_char_budget);
	editor_settings->set_setting(EDITOR_AI_HISTORY_CHAR_BUDGET_KEY, p_settings.history_char_budget);
	editor_settings->set_setting(EDITOR_AI_MAX_TOOL_ITERATIONS_KEY, p_settings.max_tool_iterations);
	editor_settings->set_setting(EDITOR_AI_ENGINE_SOURCE_ROOT_KEY, p_settings.engine_source_root);
	editor_settings->set_setting(EDITOR_AI_ENGINE_SOURCE_CACHE_ROOT_KEY, p_settings.engine_source_cache_root);
	editor_settings->set_setting(EDITOR_AI_EXTERNAL_API_ENABLED_KEY, p_settings.external_api_enabled);
	editor_settings->set_setting(EDITOR_AI_EXTERNAL_API_PORT_KEY, p_settings.external_api_port);
	editor_settings->set_setting(EDITOR_AI_EXTERNAL_API_BIND_ADDRESS_KEY, p_settings.external_api_bind_address);
}

String AISettings::get_default_base_url() {
	return "https://api.openai.com/v1";
}

String AISettings::get_default_model() {
	return "gpt-4.1";
}

String AISettings::get_default_system_prompt() {
	return TTR("You are an AI assistant inside the Jundot editor (a Godot Engine fork). You have access to built-in Function Calling tools (batch_tools, list_files, read_files, write_file, edit_file, search_files, grep_code, check_project_scripts, check_ui_layout, build_project, package_project, check_package_status, test_package, run_build, check_build_status, read_build_log, fetch_url, shell_command, restart_engine) for reading and modifying source code, searching the project, validating project work, packaging completed project plans, smoke-testing packages, building the engine, and executing commands.\n\n"
			   "When you use a tool, you will receive the result and can continue reasoning. After executing tools, analyze the results and either call more tools if needed or provide a comprehensive summary to the user with the next steps. Do NOT end the conversation with a single sentence — always follow up with a thorough analysis, reasoning, or actionable proposal.\n\n"
			   "If MCP tools are configured, they are available as tools with names prefixed by the server name (e.g. 'servername.toolname').\n\n"
			   "=== Task Breakdown Protocol ===\n"
			   "- For any non-trivial request, first produce a short ordered task list before executing tools or proposing code changes.\n"
			   "- Keep each task concrete and tied to an observable action, such as inspect files, identify cause, modify files, run validation, or summarize result.\n"
			   "- Put the task list in this machine-readable block; the editor will show it to the user:\n"
			   "<!-- TASK_PLAN -->\nTITLE: <short goal>\nSTEP: <task title> | <short detail> | pending\nSTEP: <task title> | <short detail> | pending\n<!-- END_TASK_PLAN -->\n"
			   "- Do not put code fences inside TASK_PLAN. Keep it compact.\n\n"
			   "=== Tool Call Protocol ===\n"
			   "- You MUST use the available tools to implement requests, not just describe solutions.\n"
			   "- Use `search_files` for glob-style filename searches. Do not call a tool named `glob`; this editor exposes that capability as `search_files` with a `pattern` argument.\n"
			   "- Only call tools that are explicitly provided in the current Function Calling tool list. Do not call Codex/MiMo-style tools such as `memory_search`, `session_list`, `read_file`, or `glob` unless they appear in the actual tool list. For project memory, use the Project Memories already included in context or read `.JundotAI/memory.json` with `read_files`.\n"
			   "- Prefer batch_tools when you can combine independent local actions into one tool call, such as list_files + grep_code + read_files, reading several files, or writing several related files.\n"
			   "- BEFORE writing or suggesting code changes, ALWAYS read the relevant source files first.\n"
			   "- In PROJECT mode, the full autonomous delivery pipeline applies only to empty/minimal projects created from no existing project foundation. If the project already has meaningful content, insert NEXT_QUESTION dialogue checkpoints before broad replacement, restructuring, or reinterpretation.\n"
			   "- For an empty/minimal project with an approved plan, continue through compile/build validation, project/runtime tests, package_project, check_package_status until success/failure, test_package, and then hand the package paths plus validation evidence to the user.\n"
			   "- run_build runs in the background. After calling it, call check_build_status to get the result. If still running, call it again in subsequent rounds.\n"
			   "- When you encounter a build error, read the build log, analyze the error, apply fixes, then rebuild to verify.\n\n"
			   "=== Evidence Freshness Protocol ===\n"
			   "- Treat older chat history as clues, not current truth. Evidence priority is: current editor/project/runtime state > fresh tool checks > latest user confirmation > conversation summaries > old chat messages.\n"
			   "- If the user says an issue is fixed, no longer matters, can be ignored, or verification passed, treat that issue as closed. Do not keep repairing or diagnosing it from older messages.\n"
			   "- Reopen a closed or verified-passed issue only when the latest user message, a fresh log, a fresh tool result, or a current runtime check proves it is failing again.\n"
			   "- When old history says a bug existed but current checks pass, state that the old issue appears resolved and move on to the user's latest request.\n\n"
			   "=== Agent Loop (CRITICAL) ===\n"
			   "- After you finish calling tools and receive the final text response from the model, do NOT stop.\n"
			   "- Analyze what you learned from the tool results.\n"
			   "- Provide a thorough summary of what was done, what was found, or what the user should know.\n"
			   "- Suggest concrete next steps or ask clarifying questions if needed.\n"
			   "- Keep the conversation going — a single terse response is never sufficient.");
}

int AISettings::get_default_context_char_budget() {
	return 12000;
}

int AISettings::get_default_history_char_budget() {
	return 16000;
}

int AISettings::get_default_max_tool_iterations() {
	return 10;
}

double AISettings::get_default_feature_universality_threshold() {
	return 70.0;
}

double AISettings::get_default_feature_necessity_threshold() {
	return 0.7;
}

String AISettings::get_project_agreement_key(const String &p_project_path) {
	String project_path = p_project_path.strip_edges();
	if (project_path.is_empty() && ProjectSettings::get_singleton()) {
		project_path = ProjectSettings::get_singleton()->get_resource_path();
	}

	project_path = project_path.replace("\\", "/").simplify_path();
	if (project_path.is_empty()) {
		return String();
	}

	return project_path.to_lower();
}

bool AISettings::is_usage_agreement_current(const AISettingsData &p_settings, const String &p_project_path) {
	if (p_settings.usage_agreement_version != AISettingsData::CURRENT_USAGE_AGREEMENT_VERSION) {
		return false;
	}

	const String project_key = get_project_agreement_key(p_project_path);
	if (!project_key.is_empty()) {
		return p_settings.usage_agreement_project_keys.has(project_key);
	}

	return p_settings.usage_agreement_accepted;
}

String AISettings::_get_config_path() {
	ERR_FAIL_NULL_V(EditorPaths::get_singleton(), String());
	return EditorPaths::get_singleton()->get_config_dir().path_join("ai_config.json");
}

AISettingsData AISettings::load() {
	AISettingsData settings;
	const String path = _get_config_path();
	if (path.is_empty()) {
		return settings;
	}

	if (!FileAccess::exists(path)) {
		EditorSettings *editor_settings = EditorSettings::get_singleton();
		if (editor_settings) {
			if (editor_settings->has_setting(LEGACY_BASE_URL_KEY)) {
				settings.base_url = editor_settings->get(LEGACY_BASE_URL_KEY);
			}
			if (editor_settings->has_setting(LEGACY_MODEL_KEY)) {
				settings.model = editor_settings->get(LEGACY_MODEL_KEY);
			}
			if (editor_settings->has_setting(LEGACY_API_KEY_KEY)) {
				settings.api_key = editor_settings->get(LEGACY_API_KEY_KEY);
			}
			if (editor_settings->has_setting(LEGACY_TEMPERATURE_KEY)) {
				settings.temperature = editor_settings->get(LEGACY_TEMPERATURE_KEY);
			}
			if (editor_settings->has_setting(LEGACY_MAX_TOKENS_KEY)) {
				settings.max_tokens = editor_settings->get(LEGACY_MAX_TOKENS_KEY);
			}
		}
		settings.system_prompt = get_default_system_prompt();
		_apply_editor_settings(settings, false);
		return settings;
	}

	Error err = OK;
	const String content = FileAccess::get_file_as_string(path, &err);
	if (err != OK || content.is_empty()) {
		return settings;
	}

	JSON json;
	err = json.parse(content);
	if (err != OK) {
		return settings;
	}

	const Variant data = json.get_data();
	if (data.get_type() != Variant::DICTIONARY) {
		return settings;
	}

	const Dictionary root = data;
	const String backend_type = root.get("backend_type", String("jundot_plugin"));
	settings.backend_type = _backend_type_from_string(backend_type);
	settings.jundot_ai_plugin_id = root.get("jundot_ai_plugin_id", String(JUNDOT_MIMOCODE_PLUGIN_ID));
	settings.jundot_ai_plugin_url = root.get("jundot_ai_plugin_url", String("http://127.0.0.1:4096"));
	settings.allow_legacy_openai_backend = root.get("allow_legacy_openai_backend", false);
	settings.base_url = root.get("base_url", get_default_base_url());
	settings.model = root.get("model", get_default_model());
	settings.api_key = root.get("api_key", String());
	settings.temperature = root.get("temperature", 0.7);
	settings.max_tokens = root.get("max_tokens", 40960);
	settings.system_prompt = get_default_system_prompt();
	settings.include_project_memories = root.get("include_project_memories", true);
	settings.include_tool_context = root.get("include_tool_context", true);
	settings.tools_enabled = root.get("tools_enabled", true);
	settings.develop_mode = root.get("develop_mode", false);
	settings.mcp_tools_enabled = root.get("mcp_tools_enabled", false);
	settings.context_char_budget = root.get("context_char_budget", get_default_context_char_budget());
	settings.history_char_budget = root.get("history_char_budget", get_default_history_char_budget());
	settings.max_tool_iterations = root.get("max_tool_iterations", get_default_max_tool_iterations());
	settings.auto_suggest_entries = root.get("auto_suggest_entries", true);
	settings.html_min_project_prototype_enabled = root.get("html_min_project_prototype_enabled", false);
	settings.user_extra_instructions = root.get("user_extra_instructions", String());
	settings.output_language = root.get("output_language", "auto");
	settings.usage_agreement_accepted = root.get("usage_agreement_accepted", false);
	settings.usage_agreement_version = root.get("usage_agreement_version", 0);
	settings.usage_agreement_accepted_at = root.get("usage_agreement_accepted_at", String());
	const Variant project_keys_value = root.get("usage_agreement_project_keys", Array());
	if (project_keys_value.get_type() == Variant::ARRAY) {
		Array project_keys = project_keys_value;
		for (int i = 0; i < project_keys.size(); i++) {
			const String project_key = String(project_keys[i]).strip_edges();
			if (!project_key.is_empty() && !settings.usage_agreement_project_keys.has(project_key)) {
				settings.usage_agreement_project_keys.push_back(project_key);
			}
		}
	}
	settings.feature_universality_threshold = root.get("feature_universality_threshold", get_default_feature_universality_threshold());
	settings.feature_necessity_threshold = root.get("feature_necessity_threshold", get_default_feature_necessity_threshold());
	settings.feature_design_philosophy_check = root.get("feature_design_philosophy_check", true);
	int mode_int = root.get("context_mode", 0);
	settings.context_mode = (mode_int == 1) ? AIContextMode::ENGINE : AIContextMode::PROJECT;
	settings.engine_source_root = root.get("engine_source_root", "");
	settings.engine_source_cache_root = root.get("engine_source_cache_root", "");
	settings.engine_source_repository_url = JUNDOT_ENGINE_SOURCE_REPOSITORY_URL;
	settings.encrypt_engine_source_cache = true;
	settings.external_api_enabled = root.get("external_api_enabled", false);
	settings.external_api_port = root.get("external_api_port", 8080);
	settings.external_api_bind_address = root.get("external_api_bind_address", "127.0.0.1");

	settings.github_oauth_client_id = root.get("github_oauth_client_id", String());
	settings.github_oauth_client_secret = root.get("github_oauth_client_secret", String());
	if (root.has("github_token")) {
		Dictionary gt = root["github_token"];
		settings.github_token.access_token = gt.get("access_token", String());
		settings.github_token.refresh_token = gt.get("refresh_token", String());
		settings.github_token.token_type = gt.get("token_type", String());
		settings.github_token.scope = gt.get("scope", String());
		settings.github_token.expires_at = gt.get("expires_at", 0);
	}
	if (root.has("github_user")) {
		Dictionary gu = root["github_user"];
		settings.github_user.login = gu.get("login", String());
		settings.github_user.name = gu.get("name", String());
		settings.github_user.avatar_url = gu.get("avatar_url", String());
		settings.github_user.html_url = gu.get("html_url", String());
		settings.github_user.email = gu.get("email", String());
	}

	settings.gitee_oauth_client_id = root.get("gitee_oauth_client_id", String());
	settings.gitee_oauth_client_secret = root.get("gitee_oauth_client_secret", String());
	if (root.has("gitee_token")) {
		Dictionary gt = root["gitee_token"];
		settings.gitee_token.access_token = gt.get("access_token", String());
		settings.gitee_token.refresh_token = gt.get("refresh_token", String());
		settings.gitee_token.token_type = gt.get("token_type", String());
		settings.gitee_token.scope = gt.get("scope", String());
		settings.gitee_token.expires_at = gt.get("expires_at", 0);
	}
	if (root.has("gitee_user")) {
		Dictionary gu = root["gitee_user"];
		settings.gitee_user.login = gu.get("login", String());
		settings.gitee_user.name = gu.get("name", String());
		settings.gitee_user.avatar_url = gu.get("avatar_url", String());
		settings.gitee_user.html_url = gu.get("html_url", String());
		settings.gitee_user.email = gu.get("email", String());
	}

	_apply_editor_settings(settings, true);
	return settings;
}

Error AISettings::save(const AISettingsData &p_settings) {
	const String path = _get_config_path();
	if (path.is_empty()) {
		return ERR_UNCONFIGURED;
	}

	Dictionary root;
	root["backend_type"] = _backend_type_to_string(p_settings.backend_type);
	root["jundot_ai_plugin_id"] = p_settings.jundot_ai_plugin_id;
	root["jundot_ai_plugin_url"] = p_settings.jundot_ai_plugin_url;
	root["allow_legacy_openai_backend"] = p_settings.allow_legacy_openai_backend;
	root["base_url"] = p_settings.base_url;
	root["model"] = p_settings.model;
	root["api_key"] = p_settings.api_key;
	root["temperature"] = p_settings.temperature;
	root["max_tokens"] = p_settings.max_tokens;
	root["system_prompt"] = get_default_system_prompt();
	root["include_project_memories"] = p_settings.include_project_memories;
	root["include_tool_context"] = p_settings.include_tool_context;
	root["tools_enabled"] = p_settings.tools_enabled;
	root["develop_mode"] = p_settings.develop_mode;
	root["mcp_tools_enabled"] = p_settings.mcp_tools_enabled;
	root["context_char_budget"] = p_settings.context_char_budget;
	root["history_char_budget"] = p_settings.history_char_budget;
	root["max_tool_iterations"] = p_settings.max_tool_iterations;
	root["auto_suggest_entries"] = p_settings.auto_suggest_entries;
	root["html_min_project_prototype_enabled"] = p_settings.html_min_project_prototype_enabled;
	root["user_extra_instructions"] = p_settings.user_extra_instructions;
	root["output_language"] = p_settings.output_language;
	root["usage_agreement_accepted"] = p_settings.usage_agreement_accepted;
	root["usage_agreement_version"] = p_settings.usage_agreement_version;
	root["usage_agreement_accepted_at"] = p_settings.usage_agreement_accepted_at;
	Array usage_agreement_project_keys;
	for (const String &project_key : p_settings.usage_agreement_project_keys) {
		if (!project_key.is_empty() && !usage_agreement_project_keys.has(project_key)) {
			usage_agreement_project_keys.push_back(project_key);
		}
	}
	root["usage_agreement_project_keys"] = usage_agreement_project_keys;
	root["feature_universality_threshold"] = p_settings.feature_universality_threshold;
	root["feature_necessity_threshold"] = p_settings.feature_necessity_threshold;
	root["feature_design_philosophy_check"] = p_settings.feature_design_philosophy_check;
	root["context_mode"] = (p_settings.context_mode == AIContextMode::ENGINE) ? 1 : 0;
	root["engine_source_root"] = p_settings.engine_source_root;
	root["engine_source_cache_root"] = p_settings.engine_source_cache_root;
	root["engine_source_repository_url"] = JUNDOT_ENGINE_SOURCE_REPOSITORY_URL;
	root["encrypt_engine_source_cache"] = true;
	root["external_api_enabled"] = p_settings.external_api_enabled;
	root["external_api_port"] = p_settings.external_api_port;
	root["external_api_bind_address"] = p_settings.external_api_bind_address;

	root["github_oauth_client_id"] = p_settings.github_oauth_client_id;
	root["github_oauth_client_secret"] = p_settings.github_oauth_client_secret;
	Dictionary github_token;
	github_token["access_token"] = p_settings.github_token.access_token;
	github_token["refresh_token"] = p_settings.github_token.refresh_token;
	github_token["token_type"] = p_settings.github_token.token_type;
	github_token["scope"] = p_settings.github_token.scope;
	github_token["expires_at"] = p_settings.github_token.expires_at;
	root["github_token"] = github_token;
	Dictionary github_user;
	github_user["login"] = p_settings.github_user.login;
	github_user["name"] = p_settings.github_user.name;
	github_user["avatar_url"] = p_settings.github_user.avatar_url;
	github_user["html_url"] = p_settings.github_user.html_url;
	github_user["email"] = p_settings.github_user.email;
	root["github_user"] = github_user;

	root["gitee_oauth_client_id"] = p_settings.gitee_oauth_client_id;
	root["gitee_oauth_client_secret"] = p_settings.gitee_oauth_client_secret;
	Dictionary gitee_token;
	gitee_token["access_token"] = p_settings.gitee_token.access_token;
	gitee_token["refresh_token"] = p_settings.gitee_token.refresh_token;
	gitee_token["token_type"] = p_settings.gitee_token.token_type;
	gitee_token["scope"] = p_settings.gitee_token.scope;
	gitee_token["expires_at"] = p_settings.gitee_token.expires_at;
	root["gitee_token"] = gitee_token;
	Dictionary gitee_user;
	gitee_user["login"] = p_settings.gitee_user.login;
	gitee_user["name"] = p_settings.gitee_user.name;
	gitee_user["avatar_url"] = p_settings.gitee_user.avatar_url;
	gitee_user["html_url"] = p_settings.gitee_user.html_url;
	gitee_user["email"] = p_settings.gitee_user.email;
	root["gitee_user"] = gitee_user;

	root["schema_version"] = 1;

	Error err = DirAccess::make_dir_recursive_absolute(path.get_base_dir());
	ERR_FAIL_COND_V_MSG(err != OK, err, "Could not create AI config directory.");

	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE, &err);
	ERR_FAIL_COND_V_MSG(err != OK || file.is_null(), err, "Could not open AI config for writing.");

	file->store_string(JSON::stringify(root, "\t"));
	_write_editor_settings(p_settings);
	return OK;
}

Error AISettings::reset_to_defaults() {
	AISettingsData defaults;
	defaults.system_prompt = get_default_system_prompt();
	defaults.engine_source_cache_root = _get_default_engine_source_cache_root();
	defaults.engine_source_repository_url = JUNDOT_ENGINE_SOURCE_REPOSITORY_URL;
	return save(defaults);
}

Error AISettings::accept_usage_agreement(const String &p_project_path) {
	AISettingsData settings = load();
	settings.usage_agreement_version = AISettingsData::CURRENT_USAGE_AGREEMENT_VERSION;
	settings.usage_agreement_accepted_at = Time::get_singleton()->get_datetime_string_from_system(true);
	const String project_key = get_project_agreement_key(p_project_path);
	if (!project_key.is_empty()) {
		if (!settings.usage_agreement_project_keys.has(project_key)) {
			settings.usage_agreement_project_keys.push_back(project_key);
		}
	} else {
		settings.usage_agreement_accepted = true;
	}
	return save(settings);
}

Error AISettings::reset_usage_agreement(const String &p_project_path) {
	AISettingsData settings = load();
	const String project_key = get_project_agreement_key(p_project_path);
	if (!project_key.is_empty()) {
		settings.usage_agreement_project_keys.erase(project_key);
	} else {
		settings.usage_agreement_accepted = false;
		settings.usage_agreement_version = 0;
		settings.usage_agreement_accepted_at = String();
		settings.usage_agreement_project_keys.clear();
	}
	return save(settings);
}

String AISettings::get_effective_system_prompt(const AISettingsData &p_settings) {
	String prompt;
	switch (p_settings.context_mode) {
		case AIContextMode::ENGINE:
			prompt = p_settings.engine_system_prompt;
			if (prompt.is_empty()) {
				prompt = get_default_system_prompt();
			}
			break;
		case AIContextMode::PROJECT:
		default:
			prompt = p_settings.project_system_prompt;
			if (prompt.is_empty()) {
				prompt = get_default_system_prompt();
			}
			if (p_settings.html_min_project_prototype_enabled) {
				prompt += "\n\n=== Optional HTML Minimum Project Prototype Gate ===\n"
						  "When the user describes a new project or game idea and the feature is enabled, you may directly create a tiny standalone HTML prototype before touching Godot project files. Use this only when a fast playable or visual example would help the user judge direction, controls, screen flow, or core feel.\n"
						  "- Keep the prototype minimal and disposable: one self-contained .html file under `.JundotAI/prototypes/`, with inline CSS/JavaScript and no external assets unless already present in the project.\n"
						  "- During this preview step, do not create or modify Godot scenes, scripts, resources, or project settings. Only write the HTML prototype and any required `.JundotAI/prototypes/` support file.\n"
						  "- After presenting the HTML prototype, ask the user for approval through the NEXT_QUESTION protocol before continuing into real Godot project work. Treat the HTML as a review aid, not production source.\n"
						  "- If the user explicitly asks to skip the HTML preview, or gives a clear direct implementation request, continue with the normal project workflow.\n";
			}
			break;
	}
	if (!p_settings.user_extra_instructions.is_empty()) {
		prompt += "\n\n=== User Extra Instructions ===\n" + p_settings.user_extra_instructions;
	}
	String output_language = p_settings.output_language.strip_edges();
	if (output_language.is_empty() || output_language == "auto") {
		output_language = _get_system_output_language();
	}
	if (!output_language.is_empty()) {
		prompt += "\n\n=== Output Language ===\nRespond to the user in " + output_language + ".";
	}
	return prompt;
}

String AISettings::get_engine_source_root(const AISettingsData &p_settings) {
	if (!p_settings.engine_source_root.is_empty()) {
		return p_settings.engine_source_root;
	}

	// Fall back to a nearby source checkout when running from a developer tree.
	// Packaged editor builds do not include source code, so project.jundot/project.godot is not
	// a valid engine source root here.
	String exe_path = OS::get_singleton()->get_executable_path();
	if (!exe_path.is_empty()) {
		String exe_dir = exe_path.get_base_dir();
		String candidate = exe_dir;
		for (int i = 0; i < 6; i++) {
			if (FileAccess::exists(candidate.path_join("SConstruct"))) {
				return candidate;
			}
			if (candidate.get_base_dir() == candidate) {
				break;
			}
			candidate = candidate.get_base_dir();
		}
	}
	return String();
}
