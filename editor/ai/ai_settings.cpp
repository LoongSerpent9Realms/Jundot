/*  ai_settings.cpp                                                        */
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

#include "core/error/error_macros.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/time.h"
#include "editor/file_system/editor_paths.h"
#include "editor/settings/editor_settings.h"

static constexpr const char *LEGACY_BASE_URL_KEY = "ai_assistant/base_url";
static constexpr const char *LEGACY_MODEL_KEY = "ai_assistant/model";
static constexpr const char *LEGACY_API_KEY_KEY = "ai_assistant/api_key";
static constexpr const char *LEGACY_TEMPERATURE_KEY = "ai_assistant/temperature";
static constexpr const char *LEGACY_MAX_TOKENS_KEY = "ai_assistant/max_tokens";
static constexpr const char *LEGACY_SYSTEM_PROMPT_KEY = "ai_assistant/system_prompt";

String AISettings::get_default_base_url() {
	return "https://api.openai.com/v1";
}

String AISettings::get_default_model() {
	return "gpt-4.1";
}

String AISettings::get_default_system_prompt() {
	return TTR("You are an AI assistant inside the Jundot editor (a Godot Engine fork). You have access to built-in Function Calling tools (read_files, write_file, search_files, grep_code, run_build, check_build_status, read_build_log, fetch_url, shell_command, restart_engine) for reading and modifying source code, searching the project, building the engine, and executing commands.\n\n"
		"When you use a tool, you will receive the result and can continue reasoning. After executing tools, analyze the results and either call more tools if needed or provide a comprehensive summary to the user with the next steps. Do NOT end the conversation with a single sentence — always follow up with a thorough analysis, reasoning, or actionable proposal.\n\n"
		"If MCP tools are configured, they are available as tools with names prefixed by the server name (e.g. 'servername.toolname').\n\n"
		"=== Tool Call Protocol ===\n"
		"- You MUST use the available tools to implement requests, not just describe solutions.\n"
		"- BEFORE writing or suggesting code changes, ALWAYS read the relevant source files first.\n"
		"- run_build runs in the background. After calling it, call check_build_status to get the result. If still running, call it again in subsequent rounds.\n"
		"- When you encounter a build error, read the build log, analyze the error, apply fixes, then rebuild to verify.\n\n"
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

double AISettings::get_default_feature_universality_threshold() {
	return 70.0;
}

double AISettings::get_default_feature_necessity_threshold() {
	return 0.7;
}

bool AISettings::is_usage_agreement_current(const AISettingsData &p_settings) {
	return p_settings.usage_agreement_accepted && p_settings.usage_agreement_version == AISettingsData::CURRENT_USAGE_AGREEMENT_VERSION;
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
			if (editor_settings->has_setting(LEGACY_SYSTEM_PROMPT_KEY)) {
				settings.system_prompt = editor_settings->get(LEGACY_SYSTEM_PROMPT_KEY);
			}
		}
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
	settings.base_url = root.get("base_url", get_default_base_url());
	settings.model = root.get("model", get_default_model());
	settings.api_key = root.get("api_key", String());
	settings.temperature = root.get("temperature", 0.7);
	settings.max_tokens = root.get("max_tokens", 40960);
	settings.system_prompt = root.get("system_prompt", get_default_system_prompt());
	settings.include_project_memories = root.get("include_project_memories", true);
	settings.include_tool_context = root.get("include_tool_context", true);
	settings.tools_enabled = root.get("tools_enabled", true);
	settings.mcp_tools_enabled = root.get("mcp_tools_enabled", false);
	settings.context_char_budget = root.get("context_char_budget", get_default_context_char_budget());
	settings.history_char_budget = root.get("history_char_budget", get_default_history_char_budget());
	settings.auto_suggest_entries = root.get("auto_suggest_entries", true);
	settings.user_extra_instructions = root.get("user_extra_instructions", String());
	settings.usage_agreement_accepted = root.get("usage_agreement_accepted", false);
	settings.usage_agreement_version = root.get("usage_agreement_version", 0);
	settings.usage_agreement_accepted_at = root.get("usage_agreement_accepted_at", String());
	settings.feature_universality_threshold = root.get("feature_universality_threshold", get_default_feature_universality_threshold());
	settings.feature_necessity_threshold = root.get("feature_necessity_threshold", get_default_feature_necessity_threshold());
	settings.feature_design_philosophy_check = root.get("feature_design_philosophy_check", true);
	return settings;
}

Error AISettings::save(const AISettingsData &p_settings) {
	const String path = _get_config_path();
	if (path.is_empty()) {
		return ERR_UNCONFIGURED;
	}

	Dictionary root;
	root["base_url"] = p_settings.base_url;
	root["model"] = p_settings.model;
	root["api_key"] = p_settings.api_key;
	root["temperature"] = p_settings.temperature;
	root["max_tokens"] = p_settings.max_tokens;
	root["system_prompt"] = p_settings.system_prompt;
	root["include_project_memories"] = p_settings.include_project_memories;
	root["include_tool_context"] = p_settings.include_tool_context;
	root["tools_enabled"] = p_settings.tools_enabled;
	root["mcp_tools_enabled"] = p_settings.mcp_tools_enabled;
	root["context_char_budget"] = p_settings.context_char_budget;
	root["history_char_budget"] = p_settings.history_char_budget;
	root["auto_suggest_entries"] = p_settings.auto_suggest_entries;
	root["user_extra_instructions"] = p_settings.user_extra_instructions;
	root["usage_agreement_accepted"] = p_settings.usage_agreement_accepted;
	root["usage_agreement_version"] = p_settings.usage_agreement_version;
	root["usage_agreement_accepted_at"] = p_settings.usage_agreement_accepted_at;
	root["feature_universality_threshold"] = p_settings.feature_universality_threshold;
	root["feature_necessity_threshold"] = p_settings.feature_necessity_threshold;
	root["feature_design_philosophy_check"] = p_settings.feature_design_philosophy_check;
	root["schema_version"] = 1;

	Error err = DirAccess::make_dir_recursive_absolute(path.get_base_dir());
	ERR_FAIL_COND_V_MSG(err != OK, err, "Could not create AI config directory.");

	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE, &err);
	ERR_FAIL_COND_V_MSG(err != OK || file.is_null(), err, "Could not open AI config for writing.");

	file->store_string(JSON::stringify(root, "\t"));
	return OK;
}

Error AISettings::reset_to_defaults() {
	AISettingsData defaults;
	defaults.system_prompt = get_default_system_prompt();
	return save(defaults);
}

Error AISettings::accept_usage_agreement() {
	AISettingsData settings = load();
	settings.usage_agreement_accepted = true;
	settings.usage_agreement_version = AISettingsData::CURRENT_USAGE_AGREEMENT_VERSION;
	settings.usage_agreement_accepted_at = Time::get_singleton()->get_datetime_string_from_system(true);
	return save(settings);
}

Error AISettings::reset_usage_agreement() {
	AISettingsData settings = load();
	settings.usage_agreement_accepted = false;
	settings.usage_agreement_version = 0;
	settings.usage_agreement_accepted_at = String();
	return save(settings);
}
