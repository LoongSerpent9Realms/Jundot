/*  ai_settings.h                                                          */
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

#include "core/error/error_list.h"
#include "core/string/ustring.h"

struct AISettingsData {
	static constexpr int CURRENT_USAGE_AGREEMENT_VERSION = 1;

	String base_url = "https://api.openai.com/v1";
	String model = "gpt-4.1";
	String api_key;
	double temperature = 0.7;
	int max_tokens = 1024;
	String system_prompt = "You are an AI assistant inside the Jundot editor (a Godot Engine fork). You have access to built-in Function Calling tools (read_files, write_file, search_files, grep_code, run_build, check_build_status, read_build_log, fetch_url, shell_command, restart_engine) for reading and modifying source code, searching the project, building the engine, and executing commands.\n\n"
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
		"- Keep the conversation going — a single terse response is never sufficient.";
	bool include_project_memories = true;
	bool include_tool_context = true;
	bool tools_enabled = true;
	bool mcp_tools_enabled = false;
	int context_char_budget = 12000;
	int history_char_budget = 16000;
	bool auto_suggest_entries = true;
	String user_extra_instructions; // User-customizable extra instructions appended to system prompt
	bool usage_agreement_accepted = false;
	int usage_agreement_version = 0;
	String usage_agreement_accepted_at;
	double feature_universality_threshold = 70.0;
	double feature_necessity_threshold = 0.7;
	bool feature_design_philosophy_check = true;
};

class AISettings {
	static String _get_config_path();

public:
	static String get_default_base_url();
	static String get_default_model();
	static String get_default_system_prompt();
	static int get_default_context_char_budget();
	static int get_default_history_char_budget();
	static double get_default_feature_universality_threshold();
	static double get_default_feature_necessity_threshold();
	static bool is_usage_agreement_current(const AISettingsData &p_settings);
	static Error accept_usage_agreement();
	static Error reset_usage_agreement();

	static AISettingsData load();
	static Error save(const AISettingsData &p_settings);
	static Error reset_to_defaults();
};
