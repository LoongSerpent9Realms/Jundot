/*  ai_chat_parser.cpp                                                    */
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

#include "ai_chat_parser.h"

#include "core/io/json.h"
#include "core/variant/dictionary.h"

Vector<String> AIChatParser::_extract_comment_blocks(const String &p_text, const String &p_tag) {
	Vector<String> blocks;
	const String open_tag = "<!-- " + p_tag + " -->";
	const String close_tag = "<!-- END_" + p_tag + " -->";

	int from = 0;
	while (from < p_text.length()) {
		const int start = p_text.find(open_tag, from);
		if (start < 0) {
			break;
		}
		const int content_start = start + open_tag.length();
		const int end = p_text.find(close_tag, content_start);
		if (end < 0) {
			break;
		}
		blocks.push_back(p_text.substr(content_start, end - content_start).strip_edges());
		from = end + close_tag.length();
	}
	return blocks;
}

Vector<String> AIChatParser::_extract_json_blocks(const String &p_text) {
	Vector<String> blocks;
	const String json_fence = "```json";

	int from = 0;
	while (from < p_text.length()) {
		const int start = p_text.find(json_fence, from);
		if (start < 0) {
			break;
		}
		const int content_start = start + json_fence.length();
		const int end = p_text.find("```", content_start);
		if (end < 0) {
			break;
		}
		blocks.push_back(p_text.substr(content_start, end - content_start).strip_edges());
		from = end + 3;
	}
	return blocks;
}

Vector<String> AIChatParser::_split_lines(const String &p_text) {
	Vector<String> lines = p_text.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		lines.write[i] = lines[i].strip_edges();
	}
	return lines;
}

String AIChatParser::_strip_field_prefix(const String &p_line, const String &p_prefix) {
	if (p_line.findn(p_prefix) == 0) {
		return p_line.substr(p_prefix.length()).strip_edges();
	}
	return String();
}

String AIChatParser::_extract_field(const String &p_text, const String &p_key) {
	const Vector<String> lines = _split_lines(p_text);
	String accumulator;
	bool collecting = false;

	for (int i = 0; i < lines.size(); i++) {
		const String stripped = _strip_field_prefix(lines[i], p_key + ":");
		if (!stripped.is_empty()) {
			if (collecting) {
				// New field started; stop collecting the previous one.
				break;
			}
			accumulator = stripped;
			collecting = true;
			continue;
		}
		if (collecting) {
			// Check if this line is a new field (contains ":").
			if (lines[i].find(":") >= 0 && !lines[i].begins_with("-") && lines[i].length() < 80) {
				// Likely a new field; stop collecting.
				break;
			}
			if (!lines[i].is_empty()) {
				accumulator += " " + lines[i];
			}
		}
	}
	return accumulator;
}

AISkillEntry AIChatParser::_parse_skill_from_comment(const String &p_block) {
	AISkillEntry entry = AIToolRegistry::make_skill();
	entry.name = _extract_field(p_block, "NAME");
	entry.description = _extract_field(p_block, "DESCRIPTION");
	entry.prompt_text = _extract_field(p_block, "PROMPT");

	const String perm = _extract_field(p_block, "PERMISSION");
	if (!perm.is_empty()) {
		entry.permission_level = perm.to_lower();
	}

	const String writes = _extract_field(p_block, "WRITES");
	if (writes == "yes" || writes == "true") {
		entry.writes = true;
	}

	const String confirm = _extract_field(p_block, "CONFIRMATION");
	if (confirm == "not required" || confirm == "no" || confirm == "false") {
		entry.requires_confirmation = false;
	}
	return entry;
}

AIMCPServerEntry AIChatParser::_parse_mcp_from_comment(const String &p_block) {
	AIMCPServerEntry entry = AIToolRegistry::make_mcp_server();
	entry.name = _extract_field(p_block, "NAME");
	entry.command = _extract_field(p_block, "COMMAND");
	entry.arguments = _extract_field(p_block, "ARGS");
	entry.url = _extract_field(p_block, "URL");
	entry.capabilities_json = _extract_field(p_block, "CAPABILITIES");
	return entry;
}

AIMemoryEntry AIChatParser::_parse_memory_from_comment(const String &p_block) {
	AIMemoryEntry entry = AIMemoryStore::make_entry();
	entry.title = _extract_field(p_block, "TITLE");

	const String tags_str = _extract_field(p_block, "TAGS");
	if (!tags_str.is_empty()) {
		entry.tags = tags_str.split(",");
		for (int i = 0; i < entry.tags.size(); i++) {
			entry.tags.write[i] = entry.tags[i].strip_edges();
		}
	}

	entry.content = _extract_field(p_block, "CONTENT");
	return entry;
}

static String _extract_code_fence_block(const String &p_text) {
	// Find the PATCH_CODE: key line.
	Vector<String> lines = p_text.split("\n");
	int code_key_idx = -1;
	for (int i = 0; i < lines.size(); i++) {
		if (lines[i].strip_edges().begins_with("PATCH_CODE:")) {
			code_key_idx = i;
			break;
		}
	}
	if (code_key_idx < 0) {
		return String();
	}

	// Scan from the next line for an opening code fence (```).
	for (int i = code_key_idx + 1; i < lines.size(); i++) {
		String trimmed = lines[i].strip_edges();
		if (trimmed.begins_with("```")) {
			// Found opening fence. Collect everything until the closing fence.
			Vector<String> code_lines;
			for (int j = i + 1; j < lines.size(); j++) {
				if (lines[j].strip_edges() == "```") {
					// Join collected lines and return.
					String result;
					for (int k = 0; k < code_lines.size(); k++) {
						if (k > 0) {
							result += "\n";
						}
						result += code_lines[k];
					}
					return result;
				}
				code_lines.push_back(lines[j]);
			}
			// No closing fence — return what we collected.
			String result;
			for (int k = 0; k < code_lines.size(); k++) {
				if (k > 0) {
					result += "\n";
				}
				result += code_lines[k];
			}
			return result;
		}
	}
	return String();
}

AIRepairSuggestion AIChatParser::_parse_repair_from_comment(const String &p_block) {
	AIRepairSuggestion entry;
	entry.issue_type = _extract_field(p_block, "TYPE");
	entry.title = _extract_field(p_block, "TITLE");
	entry.reproduction = _extract_field(p_block, "REPRODUCTION");
	entry.root_cause = _extract_field(p_block, "ROOT_CAUSE");
	entry.patch_summary = _extract_field(p_block, "PATCH");
	entry.patch_type = _extract_field(p_block, "PATCH_TYPE");
	entry.test_command = _extract_field(p_block, "TEST");
	entry.risk = _extract_field(p_block, "RISK");

	const String files = _extract_field(p_block, "FILES");
	if (!files.is_empty()) {
		entry.candidate_files = files.split(",");
		for (int i = 0; i < entry.candidate_files.size(); i++) {
			entry.candidate_files.write[i] = entry.candidate_files[i].strip_edges();
		}
	}

	// FETCH_URL can appear multiple times (one per URL).
	const String fetch_str = _extract_field(p_block, "FETCH_URL");
	if (!fetch_str.is_empty()) {
		// Each URL is separated by a newline or on the same "FETCH_URL: url1, url2" line.
		Vector<String> urls = fetch_str.split("\n");
		for (int i = 0; i < urls.size(); i++) {
			String u = urls[i].strip_edges();
			if (!u.is_empty()) {
				entry.fetch_urls.push_back(u);
			}
		}
	}
	// Also support comma-separated URL on a single line.
	if (entry.fetch_urls.size() == 1 && entry.fetch_urls[0].contains(",")) {
		Vector<String> split = entry.fetch_urls[0].split(",");
		entry.fetch_urls.clear();
		for (int i = 0; i < split.size(); i++) {
			String u = split[i].strip_edges();
			if (!u.is_empty()) {
				entry.fetch_urls.push_back(u);
			}
		}
	}

	// Extract PATCH_CODE from code fence inside the block.
	entry.patch_code = _extract_code_fence_block(p_block);

	// Auto-detect patch_type if not explicitly set.
	if (entry.patch_type.is_empty()) {
		if (entry.patch_code.begins_with("diff --git") || entry.patch_code.find("@@ ") >= 0) {
			entry.patch_type = "diff";
		} else if (!entry.patch_code.is_empty()) {
			entry.patch_type = "full";
		}
	}

	return entry;
}

AIFeatureGateResult AIChatParser::_parse_feature_gate_from_comment(const String &p_block) {
	AIFeatureGateResult result;
	result.title = _extract_field(p_block, "TITLE");
	result.summary = _extract_field(p_block, "SUMMARY");
	result.universality_percent = _extract_field(p_block, "UNIVERSALITY").to_float();
	result.necessity_score = _extract_field(p_block, "NECESSITY").to_float();
	result.workaround_cost = _extract_field(p_block, "WORKAROUND_COST");
	result.philosophy_conflict = _extract_field(p_block, "PHILOSOPHY_CONFLICT");
	return result;
}

AITaskPlan AIChatParser::_parse_task_plan_from_comment(const String &p_block) {
	AITaskPlan plan;
	plan.title = _extract_field(p_block, "TITLE");

	const Vector<String> lines = _split_lines(p_block);
	for (int i = 0; i < lines.size(); i++) {
		String step_text = _strip_field_prefix(lines[i], "STEP:");
		if (step_text.is_empty()) {
			continue;
		}

		Vector<String> parts = step_text.split("|");
		AITaskPlan::Step step;
		step.title = parts.size() > 0 ? parts[0].strip_edges() : String();
		step.detail = parts.size() > 1 ? parts[1].strip_edges() : String();
		step.status = parts.size() > 2 ? parts[2].strip_edges() : String("pending");
		if (!step.title.is_empty()) {
			plan.steps.push_back(step);
		}
	}

	return plan;
}

AISkillEntry AIChatParser::_parse_skill_from_json(const Dictionary &p_dict) {
	AISkillEntry entry = AIToolRegistry::make_skill();
	entry.name = p_dict.get("name", String());
	entry.description = p_dict.get("description", String());
	entry.prompt_text = p_dict.get("prompt_text", String());
	entry.permission_level = p_dict.get("permission_level", "read");
	entry.writes = p_dict.get("writes", false);
	entry.requires_confirmation = p_dict.get("requires_confirmation", true);
	entry.read_only_allowed = p_dict.get("read_only_allowed", true);
	return entry;
}

AIMCPServerEntry AIChatParser::_parse_mcp_from_json(const Dictionary &p_dict) {
	AIMCPServerEntry entry = AIToolRegistry::make_mcp_server();
	entry.name = p_dict.get("name", String());
	entry.command = p_dict.get("command", String());
	entry.arguments = p_dict.get("arguments", String());
	entry.url = p_dict.get("url", String());
	entry.capabilities_json = p_dict.get("capabilities_json", String());
	entry.writes = p_dict.get("writes", false);
	entry.requires_confirmation = p_dict.get("requires_confirmation", true);
	entry.read_only_allowed = p_dict.get("read_only_allowed", true);
	return entry;
}

AIMemoryEntry AIChatParser::_parse_memory_from_json(const Dictionary &p_dict) {
	AIMemoryEntry entry = AIMemoryStore::make_entry();
	entry.title = p_dict.get("title", String());
	entry.content = p_dict.get("content", String());

	const Variant tags_var = p_dict.get("tags", Array());
	if (tags_var.get_type() == Variant::ARRAY) {
		Array tags_arr = tags_var;
		for (int i = 0; i < tags_arr.size(); i++) {
			entry.tags.push_back(String(tags_arr[i]));
		}
	}
	return entry;
}

AIRepairSuggestion AIChatParser::_parse_repair_from_json(const Dictionary &p_dict) {
	AIRepairSuggestion entry;
	entry.issue_type = p_dict.get("issue_type", String());
	entry.title = p_dict.get("title", String());
	entry.reproduction = p_dict.get("reproduction", String());
	entry.root_cause = p_dict.get("root_cause", String());
	entry.patch_summary = p_dict.get("patch_summary", String());
	entry.patch_type = p_dict.get("patch_type", String());
	entry.patch_code = p_dict.get("patch_code", String());
	entry.test_command = p_dict.get("test_command", String());
	entry.risk = p_dict.get("risk", String());

	const Variant files_var = p_dict.get("candidate_files", Array());
	if (files_var.get_type() == Variant::ARRAY) {
		Array files = files_var;
		for (int i = 0; i < files.size(); i++) {
			entry.candidate_files.push_back(String(files[i]));
		}
	}

	const Variant urls_var = p_dict.get("fetch_urls", Array());
	if (urls_var.get_type() == Variant::ARRAY) {
		Array urls = urls_var;
		for (int i = 0; i < urls.size(); i++) {
			entry.fetch_urls.push_back(String(urls[i]));
		}
	}

	// Auto-detect patch_type if not explicitly set.
	if (entry.patch_type.is_empty()) {
		if (entry.patch_code.begins_with("diff --git") || entry.patch_code.find("@@ ") >= 0) {
			entry.patch_type = "diff";
		} else if (!entry.patch_code.is_empty()) {
			entry.patch_type = "full";
		}
	}

	return entry;
}

AIFeatureGateResult AIChatParser::_parse_feature_gate_from_json(const Dictionary &p_dict) {
	AIFeatureGateResult result;
	result.title = p_dict.get("title", String());
	result.summary = p_dict.get("summary", String());
	result.universality_percent = p_dict.get("universality_percent", 0.0);
	result.necessity_score = p_dict.get("necessity_score", 0.0);
	result.workaround_cost = p_dict.get("workaround_cost", String());
	result.philosophy_conflict = p_dict.get("philosophy_conflict", String());
	return result;
}

AITaskPlan AIChatParser::_parse_task_plan_from_json(const Dictionary &p_dict) {
	AITaskPlan plan;
	plan.title = p_dict.get("title", String());

	const Variant steps_var = p_dict.get("steps", Array());
	if (steps_var.get_type() == Variant::ARRAY) {
		Array steps = steps_var;
		for (int i = 0; i < steps.size(); i++) {
			AITaskPlan::Step step;
			if (steps[i].get_type() == Variant::DICTIONARY) {
				Dictionary step_dict = steps[i];
				step.title = step_dict.get("title", String());
				step.detail = step_dict.get("detail", String());
				step.status = step_dict.get("status", String("pending"));
			} else {
				step.title = String(steps[i]);
				step.status = "pending";
			}
			if (!step.title.strip_edges().is_empty()) {
				plan.steps.push_back(step);
			}
		}
	}

	return plan;
}

void AIChatParser::parse(const String &p_response, Vector<AISuggestion> &r_suggestions) {
	r_suggestions.clear();

	// --- Phase 1: HTML comment blocks (primary format) ---
	const Vector<String> skill_blocks = _extract_comment_blocks(p_response, "SKILL");
	for (int i = 0; i < skill_blocks.size() && r_suggestions.size() < MAX_SUGGESTIONS_PER_RESPONSE; i++) {
		AISuggestion s;
		s.type = AISuggestion::TYPE_SKILL;
		s.skill = _parse_skill_from_comment(skill_blocks[i]);
		r_suggestions.push_back(s);
	}

	const Vector<String> mcp_blocks = _extract_comment_blocks(p_response, "MCP");
	for (int i = 0; i < mcp_blocks.size() && r_suggestions.size() < MAX_SUGGESTIONS_PER_RESPONSE; i++) {
		AISuggestion s;
		s.type = AISuggestion::TYPE_MCP_SERVER;
		s.mcp_server = _parse_mcp_from_comment(mcp_blocks[i]);
		r_suggestions.push_back(s);
	}

	const Vector<String> memory_blocks = _extract_comment_blocks(p_response, "MEMORY");
	for (int i = 0; i < memory_blocks.size() && r_suggestions.size() < MAX_SUGGESTIONS_PER_RESPONSE; i++) {
		AISuggestion s;
		s.type = AISuggestion::TYPE_MEMORY;
		s.memory = _parse_memory_from_comment(memory_blocks[i]);
		r_suggestions.push_back(s);
	}

	// --- Phase 2: JSON code blocks (fallback format) ---
	const Vector<String> json_blocks = _extract_json_blocks(p_response);
	for (int i = 0; i < json_blocks.size() && r_suggestions.size() < MAX_SUGGESTIONS_PER_RESPONSE; i++) {
		JSON json;
		const Error err = json.parse(json_blocks[i]);
		if (err != OK) {
			continue;
		}

		const Variant data = json.get_data();
		if (data.get_type() != Variant::DICTIONARY) {
			continue;
		}

		const Dictionary dict = data;
		const String type = String(dict.get("type", String())).to_lower();

		if (type == "skill") {
			AISuggestion s;
			s.type = AISuggestion::TYPE_SKILL;
			s.skill = _parse_skill_from_json(dict);
			r_suggestions.push_back(s);
		} else if (type == "mcp" || type == "mcp_server") {
			AISuggestion s;
			s.type = AISuggestion::TYPE_MCP_SERVER;
			s.mcp_server = _parse_mcp_from_json(dict);
			r_suggestions.push_back(s);
		} else if (type == "memory") {
			AISuggestion s;
			s.type = AISuggestion::TYPE_MEMORY;
			s.memory = _parse_memory_from_json(dict);
			r_suggestions.push_back(s);
		}
	}
}

void AIChatParser::parse_next_questions(const String &p_response, Vector<String> &r_questions) {
	r_questions.clear();

	const Vector<String> question_blocks = _extract_comment_blocks(p_response, "NEXT_QUESTION");
	for (int i = 0; i < question_blocks.size() && r_questions.size() < 4; i++) {
		const String question = _extract_field(question_blocks[i], "QUESTION").strip_edges();
		if (!question.is_empty()) {
			r_questions.push_back(question);
		}
	}

	const Vector<String> json_blocks = _extract_json_blocks(p_response);
	for (int i = 0; i < json_blocks.size() && r_questions.size() < 4; i++) {
		JSON json;
		if (json.parse(json_blocks[i]) != OK || json.get_data().get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Variant data = json.get_data();
		const Dictionary dict = data;
		const String type = String(dict.get("type", String())).to_lower();
		if (type != "next_questions" && type != "next_question") {
			continue;
		}

		const Variant questions_var = dict.get("questions", Array());
		if (questions_var.get_type() == Variant::ARRAY) {
			const Array questions = questions_var;
			for (int j = 0; j < questions.size() && r_questions.size() < 4; j++) {
				const String question = String(questions[j]).strip_edges();
				if (!question.is_empty()) {
					r_questions.push_back(question);
				}
			}
		} else {
			const String question = String(dict.get("question", String())).strip_edges();
			if (!question.is_empty()) {
				r_questions.push_back(question);
			}
		}
	}
}

void AIChatParser::parse_task_plans(const String &p_response, Vector<AITaskPlan> &r_task_plans) {
	r_task_plans.clear();

	const Vector<String> task_blocks = _extract_comment_blocks(p_response, "TASK_PLAN");
	for (int i = 0; i < task_blocks.size() && r_task_plans.size() < MAX_SUGGESTIONS_PER_RESPONSE; i++) {
		AITaskPlan plan = _parse_task_plan_from_comment(task_blocks[i]);
		if (!plan.steps.is_empty()) {
			r_task_plans.push_back(plan);
		}
	}

	const Vector<String> json_blocks = _extract_json_blocks(p_response);
	for (int i = 0; i < json_blocks.size() && r_task_plans.size() < MAX_SUGGESTIONS_PER_RESPONSE; i++) {
		JSON json;
		if (json.parse(json_blocks[i]) != OK || json.get_data().get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Variant data = json.get_data();
		const Dictionary dict = data;
		const String type = String(dict.get("type", String())).to_lower();
		if (type == "task_plan" || type == "plan") {
			AITaskPlan plan = _parse_task_plan_from_json(dict);
			if (!plan.steps.is_empty()) {
				r_task_plans.push_back(plan);
			}
		}
	}
}

String AIChatParser::strip_task_plan_blocks(const String &p_response) {
	String result = p_response;
	const String open_tag = "<!-- TASK_PLAN -->";
	const String close_tag = "<!-- END_TASK_PLAN -->";

	int from = 0;
	while (from < result.length()) {
		const int start = result.find(open_tag, from);
		if (start < 0) {
			break;
		}
		const int end = result.find(close_tag, start + open_tag.length());
		if (end < 0) {
			break;
		}
		result = result.substr(0, start) + result.substr(end + close_tag.length());
		from = start;
	}
	return result.strip_edges();
}

String AIChatParser::strip_next_question_blocks(const String &p_response) {
	String result = p_response;
	const String open_tag = "<!-- NEXT_QUESTION -->";
	const String close_tag = "<!-- END_NEXT_QUESTION -->";

	int from = 0;
	while (from < result.length()) {
		const int start = result.find(open_tag, from);
		if (start < 0) {
			break;
		}
		const int end = result.find(close_tag, start + open_tag.length());
		if (end < 0) {
			break;
		}
		result = result.substr(0, start) + result.substr(end + close_tag.length());
		from = start;
	}
	return result.strip_edges();
}

void AIChatParser::parse_repair_tasks(const String &p_response, Vector<AIRepairSuggestion> &r_repairs) {
	r_repairs.clear();

	const Vector<String> repair_blocks = _extract_comment_blocks(p_response, "REPAIR_TASK");
	for (int i = 0; i < repair_blocks.size() && r_repairs.size() < MAX_SUGGESTIONS_PER_RESPONSE; i++) {
		r_repairs.push_back(_parse_repair_from_comment(repair_blocks[i]));
	}

	const Vector<String> json_blocks = _extract_json_blocks(p_response);
	for (int i = 0; i < json_blocks.size() && r_repairs.size() < MAX_SUGGESTIONS_PER_RESPONSE; i++) {
		JSON json;
		if (json.parse(json_blocks[i]) != OK || json.get_data().get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Variant data = json.get_data();
		const Dictionary dict = data;
		const String type = String(dict.get("type", String())).to_lower();
		if (type == "repair" || type == "repair_task") {
			r_repairs.push_back(_parse_repair_from_json(dict));
		}
	}
}

void AIChatParser::parse_feature_gates(const String &p_response, Vector<AIFeatureGateResult> &r_features) {
	r_features.clear();

	const Vector<String> feature_blocks = _extract_comment_blocks(p_response, "FEATURE_GATE");
	for (int i = 0; i < feature_blocks.size() && r_features.size() < MAX_SUGGESTIONS_PER_RESPONSE; i++) {
		r_features.push_back(_parse_feature_gate_from_comment(feature_blocks[i]));
	}

	const Vector<String> json_blocks = _extract_json_blocks(p_response);
	for (int i = 0; i < json_blocks.size() && r_features.size() < MAX_SUGGESTIONS_PER_RESPONSE; i++) {
		JSON json;
		if (json.parse(json_blocks[i]) != OK || json.get_data().get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Variant data = json.get_data();
		const Dictionary dict = data;
		const String type = String(dict.get("type", String())).to_lower();
		if (type == "feature_gate" || type == "feature") {
			r_features.push_back(_parse_feature_gate_from_json(dict));
		}
	}
}
