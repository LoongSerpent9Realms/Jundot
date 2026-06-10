/*  ai_context_builder.cpp                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#include "ai_context_builder.h"

#include "ai_memory_store.h"
#include "ai_tool_registry.h"

#include "core/math/math_defs.h"

struct AIMemorySort {
	bool operator()(const AIMemoryEntry &p_left, const AIMemoryEntry &p_right) const {
		if (p_left.updated_at == p_right.updated_at) {
			return p_left.title.naturalnocasecmp_to(p_right.title) < 0;
		}
		return p_left.updated_at > p_right.updated_at;
	}
};

struct AISkillSort {
	bool operator()(const AISkillEntry &p_left, const AISkillEntry &p_right) const {
		if (p_left.updated_at == p_right.updated_at) {
			return p_left.name.naturalnocasecmp_to(p_right.name) < 0;
		}
		return p_left.updated_at > p_right.updated_at;
	}
};

struct AIMCPServerSort {
	bool operator()(const AIMCPServerEntry &p_left, const AIMCPServerEntry &p_right) const {
		if (p_left.updated_at == p_right.updated_at) {
			return p_left.name.naturalnocasecmp_to(p_right.name) < 0;
		}
		return p_left.updated_at > p_right.updated_at;
	}
};

static String _ai_join_tags(const Vector<String> &p_tags) {
	String text;
	for (int i = 0; i < p_tags.size(); i++) {
		if (i > 0) {
			text += ", ";
		}
		text += p_tags[i];
	}
	return text;
}

String AIContextBuilder::_format_memories() {
	Vector<AIMemoryEntry> entries;
	if (AIMemoryStore::load(entries) != OK || entries.is_empty()) {
		return String();
	}

	entries.sort_custom<AIMemorySort>();

	String context = "Project Memories:\n";
	bool has_enabled = false;
	for (const AIMemoryEntry &entry : entries) {
		if (!entry.enabled) {
			continue;
		}

		has_enabled = true;
		String title = entry.title.strip_edges();
		if (title.is_empty()) {
			title = "Untitled Memory";
		}

		context += "- " + title;
		const String tags = _ai_join_tags(entry.tags);
		if (!tags.is_empty()) {
			context += " [" + tags + "]";
		}
		context += ": " + entry.content.strip_edges() + "\n";
	}

	return has_enabled ? context : String();
}

String AIContextBuilder::_format_tools() {
	Vector<AISkillEntry> skills;
	Vector<AIMCPServerEntry> mcp_servers;
	if (AIToolRegistry::load(skills, mcp_servers) != OK || (skills.is_empty() && mcp_servers.is_empty())) {
		return String();
	}

	skills.sort_custom<AISkillSort>();
	mcp_servers.sort_custom<AIMCPServerSort>();

	String context;
	bool has_skills = false;
	for (const AISkillEntry &skill : skills) {
		if (!skill.enabled) {
			continue;
		}
		if (!has_skills) {
			context += "Available Skills (context only; do not execute automatically):\n";
			has_skills = true;
		}

		const String name = skill.name.strip_edges().is_empty() ? String("Untitled Skill") : skill.name.strip_edges();
		context += "- " + name + " (permission: " + skill.permission_level + ", writes: " + (skill.writes ? "yes" : "no") + ", confirmation: " + (skill.requires_confirmation ? "required" : "not required") + ")";
		if (!skill.description.strip_edges().is_empty()) {
			context += ": " + skill.description.strip_edges();
		}
		if (!skill.prompt_text.strip_edges().is_empty()) {
			context += "\n  Skill prompt: " + skill.prompt_text.strip_edges();
		}
		context += "\n";
	}

	bool has_mcp_servers = false;
	for (const AIMCPServerEntry &server : mcp_servers) {
		if (!server.enabled) {
			continue;
		}
		if (!has_mcp_servers) {
			if (!context.is_empty()) {
				context += "\n";
			}
			context += "Configured MCP Servers (capability list only; do not start or call tools automatically):\n";
			has_mcp_servers = true;
		}

		const String name = server.name.strip_edges().is_empty() ? String("Untitled MCP Server") : server.name.strip_edges();
		context += "- " + name;
		if (!server.command.strip_edges().is_empty()) {
			context += " command: " + server.command.strip_edges();
		}
		if (!server.arguments.strip_edges().is_empty()) {
			context += " args: " + server.arguments.strip_edges();
		}
		if (!server.url.strip_edges().is_empty()) {
			context += " url: " + server.url.strip_edges();
		}
		context += " (writes: " + String(server.writes ? "yes" : "no") + ", confirmation: " + String(server.requires_confirmation ? "required" : "not required") + ")";
		if (!server.capabilities_json.strip_edges().is_empty()) {
			context += "\n  Capabilities JSON: " + server.capabilities_json.strip_edges();
		}
		context += "\n";
	}

	return context;
}

String AIContextBuilder::_apply_budget(const String &p_context, int p_budget) {
	if (p_budget <= 0 || p_context.length() <= p_budget) {
		return p_context;
	}

	const String marker = "\n[AI context truncated by character budget.]";
	const int keep = MAX(0, p_budget - marker.length());
	return p_context.left(keep).strip_edges() + marker;
}

String AIContextBuilder::_format_suggestion_instructions() {
	return "You are explicitly authorized to modify the engine's own source code (especially files in editor/ai/) to implement feature requests from the user. "
		   "Do not just describe how a feature could be added — use the available tools to read files, write changes, build, and restart the engine yourself. "
		   "When the user asks for a new feature or an improvement, you have full permission to implement it.\n\n"
		   "You have access to built-in Function Calling tools for reading and modifying files, searching code, building the project, and executing shell commands. "
		   "If MCP server tools are configured, they are available as tools with names prefixed by the server name (e.g. 'servername.toolname'). "
		   "Use these tools when the user asks you to inspect, modify, build, or analyze engine source code. "
		   "You do not need to ask for permission to use a tool — just call it directly. "
		   "IMPORTANT: run_build now runs in the background to avoid blocking the editor UI. After calling run_build, you MUST call check_build_status to get the result. If check_build_status returns 'still running', call it again in subsequent rounds until the build completes. "
		   "When you encounter a build error, read the build log to analyze the error, then use write_file to apply fixes and run_build to verify.\n\n"
		   "=== Engine Source Retrieval ===\n"
		   "You ARE the JunDot engine (a Godot fork). The engine source code lives at an upstream Git repository, not in the current workspace — "
		   "the workspace contains the compiled editor binary or the game project files.\n\n"
		   "To modify yourself, first check if the engine source is available locally:\n"
		   "1. Call search_files('editor/ai/') or read_files to check if editor/ai/ exists\n"
		   "2. If the tools return errors (source not found), the full source is not present.\n"
		   "   In that case, ask the user where to find the engine source code — "
		   "   they may have a Git clone elsewhere, or can provide a Git URL to clone.\n"
		   "3. Once source is accessible (locally or after clone), use read_files to study the existing code, "
		   "   write_file to apply changes, run_build to compile, check_build_status to wait for completion, "
		   "   and restart_engine to load the new build.\n"
		   "4. Workflow: search_files → read_files → write_file → run_build → check_build_status → restart_engine\n\n"
		   "=== Tool Call Protocol (CRITICAL) ===\n"
		   "You MUST use the available tools (read_files, search_files, grep_code, write_file, run_build, etc.) "
		   "to implement requests, not just describe solutions. "
		   "When the user asks you to modify the engine or add a feature:\n"
		   "- BEFORE writing any response, ALWAYS read the relevant source files first using read_files\n"
		   "- Then make the change with write_file\n"
		   "- Then build and verify\n"
		   "Do NOT output a text-only architecture proposal when you have tools to read the real code and make real changes. "
		   "The tools exist for a reason — use them.\n\n"
		   "Suggestion Protocol (when the user asks you to create a skill, MCP server, or memory entry, or when it would be helpful to suggest one):\n"
		   "You may output structured suggestion blocks in your response using HTML comment markers. The user will see a confirmation card and must approve before anything is saved.\n"
		   "Do NOT automatically execute or apply any suggestion.\n\n"
		   "For a Skill suggestion:\n"
		   "<!-- SKILL -->\n"
		   "NAME: <skill name>\n"
		   "DESCRIPTION: <what this skill does>\n"
		   "PROMPT: <the prompt text that defines the skill behavior>\n"
		   "PERMISSION: read|write\n"
		   "WRITES: yes|no\n"
		   "CONFIRMATION: required|not required\n"
		   "<!-- END_SKILL -->\n\n"
		   "For an MCP Server suggestion:\n"
		   "<!-- MCP -->\n"
		   "NAME: <server name>\n"
		   "COMMAND: <executable command>\n"
		   "ARGS: <command arguments>\n"
		   "URL: <server URL if applicable>\n"
		   "CAPABILITIES: <JSON string of capabilities>\n"
		   "<!-- END_MCP -->\n\n"
		   "For a Memory suggestion:\n"
		   "<!-- MEMORY -->\n"
		   "TITLE: <memory title>\n"
		   "TAGS: <comma-separated tags>\n"
		   "CONTENT: <memory content text>\n"
		   "<!-- END_MEMORY -->\n\n"
		   "Alternatively, you may use a JSON code block with a \"type\" field set to \"skill\", \"mcp\", or \"memory\" and the corresponding fields.\n"
		   "Only suggest entries that are genuinely useful. Limit to at most 3 suggestions per response unless the user explicitly asks for more.\n\n"
		   "Repair and feature gate protocol:\n"
		   "When the user reports a defect, crash, regression, missing behavior that should already exist, or a performance bottleneck, classify it as defect or performance_bottleneck and provide a repair task block. Do not claim it is fixed until a patch and tests have actually run.\n"
		   "<!-- REPAIR_TASK -->\n"
		   "TYPE: defect|performance_bottleneck\n"
		   "TITLE: <short issue title>\n"
		   "REPRODUCTION: <user-visible reproduction or symptom>\n"
		   "ROOT_CAUSE: <specific suspected root cause and call path>\n"
		   "FILES: <comma-separated candidate source file paths>\n"
		   "PATCH: <concise patch strategy>\n"
		   "PATCH_TYPE: full|diff (use 'diff' for unified diff patches, 'full' for complete file replacement)\n"
		   "FETCH_URL: <optional raw.githubusercontent.com URL to download the original file before patching>\n"
		   "TEST: <specific test or build command to validate>\n"
		   "RISK: <main regression risk>\n"
		   "PATCH_CODE:\n"
		   "```cpp\n"
		   "// For PATCH_TYPE=full: the complete new file content.\n"
		   "// For PATCH_TYPE=diff: a unified diff like:\n"
		   "// diff --git a/path b/path\n"
		   "// @@ -start,count +start,count @@\n"
		   "// + added line\n"
		   "// - removed line\n"
		   "```\n"
		   "<!-- END_REPAIR_TASK -->\n\n"
		   "When the user asks for a feature expansion, first evaluate whether it should be added to Jundot. Use the configured universality threshold (default 70%), necessity/workaround cost, and Jundot design philosophy. Only mark it suggested if all gates pass.\n"
		   "<!-- FEATURE_GATE -->\n"
		   "TITLE: <feature title>\n"
		   "SUMMARY: <what the feature does>\n"
		   "UNIVERSALITY: <estimated percent of game genres needing this, 0-100>\n"
		   "NECESSITY: <score from 0.0 to 1.0 based on workaround cost>\n"
		   "WORKAROUND_COST: <why existing workflows are or are not enough>\n"
		   "PHILOSOPHY_CONFLICT: <empty if none; otherwise explain conflict>\n"
		   "<!-- END_FEATURE_GATE -->";
}

String AIContextBuilder::build_context(bool p_include_memories, bool p_include_tools, int p_budget, bool p_include_suggestion_instructions) {
	String context;
	if (p_include_memories) {
		context += _format_memories();
	}

	if (p_include_tools) {
		const String tools = _format_tools();
		if (!tools.is_empty()) {
			if (!context.is_empty()) {
				context += "\n";
			}
			context += tools;
		}
	}

	if (p_include_suggestion_instructions) {
		const String instructions = _format_suggestion_instructions();
		if (!instructions.is_empty()) {
			if (!context.is_empty()) {
				context += "\n\n";
			}
			context += instructions;
		}
	}

	return _apply_budget(context.strip_edges(), p_budget);
}
