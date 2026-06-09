/*  ai_chat_parser.h                                                       */
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

#include "editor/ai/ai_memory_store.h"
#include "editor/ai/ai_tool_registry.h"
#include "editor/ai/ai_feature_gate.h"

#include "core/string/ustring.h"
#include "core/templates/vector.h"

struct AISuggestion {
	enum Type {
		TYPE_SKILL,
		TYPE_MCP_SERVER,
		TYPE_MEMORY,
	};

	Type type = TYPE_SKILL;
	AISkillEntry skill;
	AIMCPServerEntry mcp_server;
	AIMemoryEntry memory;
};

struct AIRepairSuggestion {
	String issue_type;
	String title;
	String reproduction;
	String root_cause;
	Vector<String> candidate_files;
	String patch_summary;
	String patch_type; // "full" or "diff"
	String patch_code; // full file content or unified diff text
	Vector<String> fetch_urls; // remote URLs to download before applying
	String test_command;
	String risk;
};

class AIChatParser {
	static constexpr int MAX_SUGGESTIONS_PER_RESPONSE = 10;

	static Vector<String> _extract_comment_blocks(const String &p_text, const String &p_tag);
	static Vector<String> _extract_json_blocks(const String &p_text);

	static AISkillEntry _parse_skill_from_comment(const String &p_block);
	static AIMCPServerEntry _parse_mcp_from_comment(const String &p_block);
	static AIMemoryEntry _parse_memory_from_comment(const String &p_block);
	static AIRepairSuggestion _parse_repair_from_comment(const String &p_block);
	static AIFeatureGateResult _parse_feature_gate_from_comment(const String &p_block);

	static AISkillEntry _parse_skill_from_json(const Dictionary &p_dict);
	static AIMCPServerEntry _parse_mcp_from_json(const Dictionary &p_dict);
	static AIMemoryEntry _parse_memory_from_json(const Dictionary &p_dict);
	static AIRepairSuggestion _parse_repair_from_json(const Dictionary &p_dict);
	static AIFeatureGateResult _parse_feature_gate_from_json(const Dictionary &p_dict);

	static String _extract_field(const String &p_text, const String &p_key);
	static Vector<String> _split_lines(const String &p_text);
	static String _strip_field_prefix(const String &p_line, const String &p_prefix);

public:
	static void parse(const String &p_response, Vector<AISuggestion> &r_suggestions);
	static void parse_repair_tasks(const String &p_response, Vector<AIRepairSuggestion> &r_repairs);
	static void parse_feature_gates(const String &p_response, Vector<AIFeatureGateResult> &r_features);
};
