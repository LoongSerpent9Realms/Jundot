/*  ai_importer.cpp                                                       */
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

#include "ai_importer.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "editor/ai/ai_memory_store.h"
#include "editor/ai/ai_tool_registry.h"

AIImporter::FileFormat AIImporter::_detect_format(const String &p_path) {
	const String filename = p_path.get_file().to_lower();

	// WorkBuddy SKILL.md: filename is exactly "SKILL.md" or contains "skill" and ends ".md".
	if (filename == "skill.md" || (filename.find("skill") >= 0 && filename.ends_with(".md"))) {
		return FORMAT_SKILL_MD;
	}

	// MCP config JSON: filename contains "mcp" and ends ".json".
	if (filename.find("mcp") >= 0 && filename.ends_with(".json")) {
		return FORMAT_MCP_JSON;
	}

	// Any .json that might contain MCP server definitions.
	if (filename.ends_with(".json")) {
		return FORMAT_MCP_JSON;
	}

	// Any .md or .txt file becomes a memory entry.
	if (filename.ends_with(".md") || filename.ends_with(".txt")) {
		return FORMAT_TEXT_MEMORY;
	}

	return FORMAT_UNKNOWN;
}

Error AIImporter::_parse_skill_md(const String &p_content, AISkillEntry &r_skill) {
	r_skill = AIToolRegistry::make_skill();

	const Vector<String> lines = p_content.split("\n");
	bool in_frontmatter = false;
	bool past_frontmatter = false;
	String body;
	int field_count = 0;

	for (int i = 0; i < lines.size(); i++) {
		const String line = lines[i].strip_edges();

		// Parse YAML-style frontmatter between --- delimiters.
		if (line == "---") {
			if (!in_frontmatter && !past_frontmatter) {
				in_frontmatter = true;
				continue;
			} else if (in_frontmatter) {
				in_frontmatter = false;
				past_frontmatter = true;
				continue;
			}
		}

		if (in_frontmatter) {
			// Parse frontmatter fields like "title:", "summary:", etc.
			if (line.find(":") > 0) {
				const int colon_pos = line.find(":");
				const String key = line.left(colon_pos).strip_edges().to_lower();
				const String value = line.substr(colon_pos + 1).strip_edges();

				if (key == "title" || key == "name") {
					r_skill.name = value;
					field_count++;
				} else if (key == "summary" || key == "description") {
					r_skill.description = value;
					field_count++;
				}
			}
			continue;
		}

		// After frontmatter, collect body as prompt_text.
		if (past_frontmatter || (!in_frontmatter && !past_frontmatter)) {
			if (!line.is_empty() || !body.is_empty()) {
				body += lines[i] + "\n";
			}
		}
	}

	// If no frontmatter was found, try to extract a name from the first heading.
	if (r_skill.name.is_empty() && !body.is_empty()) {
		const Vector<String> body_lines = body.split("\n");
		for (int i = 0; i < body_lines.size(); i++) {
			const String bl = body_lines[i].strip_edges();
			if (bl.begins_with("# ")) {
				r_skill.name = bl.substr(2).strip_edges();
				field_count++;
				break;
			}
		}
	}

	if (!body.strip_edges().is_empty()) {
		r_skill.prompt_text = body.strip_edges();
		field_count++;
	}

	// Require at least a name or a prompt to consider the parse successful.
	return (field_count > 0) ? OK : ERR_PARSE_ERROR;
}

Error AIImporter::_parse_mcp_json(const String &p_content, Vector<AIMCPServerEntry> &r_servers) {
	r_servers.clear();

	JSON json;
	const Error err = json.parse(p_content);
	if (err != OK) {
		return ERR_PARSE_ERROR;
	}

	const Variant data = json.get_data();
	if (data.get_type() != Variant::DICTIONARY) {
		return ERR_PARSE_ERROR;
	}

	const Dictionary root = data;

	// Format 1: { "mcpServers": { "server-name": { ... } } } (standard MCP config).
	const Variant servers_var = root.get("mcpServers", Variant());
	if (servers_var.get_type() == Variant::DICTIONARY) {
		const Dictionary servers_dict = servers_var;
		const Array keys = servers_dict.keys();
		for (int i = 0; i < keys.size() && r_servers.size() < MAX_IMPORT_ENTRIES; i++) {
			const String server_name = keys[i];
			const Variant server_val = servers_dict[server_name];
			if (server_val.get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary server_dict = server_val;

			AIMCPServerEntry entry = AIToolRegistry::make_mcp_server();
			entry.name = server_name;
			entry.command = server_dict.get("command", String());
			entry.url = server_dict.get("url", String());

			const Variant args_var = server_dict.get("args", Variant());
			if (args_var.get_type() == Variant::ARRAY) {
				const Array args_arr = args_var;
				String args_str;
				for (int j = 0; j < args_arr.size(); j++) {
					if (j > 0) {
						args_str += " ";
					}
					String arg = String(args_arr[j]);
					if (arg.find(" ") >= 0 || arg.find("\"") >= 0) {
						arg = "\"" + arg.replace("\"", "\\\"") + "\"";
					}
					args_str += arg;
				}
				entry.arguments = args_str;
			}

			const Variant env_var = server_dict.get("env", Variant());
			if (env_var.get_type() == Variant::DICTIONARY) {
				entry.environment = env_var;
			}

			r_servers.push_back(entry);
		}
	}

	// Format 2: { "servers": [ { "name": ..., ... } ] } (array format).
	if (r_servers.is_empty()) {
		const Variant arr_var = root.get("servers", Variant());
		if (arr_var.get_type() == Variant::ARRAY) {
			const Array servers_arr = arr_var;
			for (int i = 0; i < servers_arr.size() && r_servers.size() < MAX_IMPORT_ENTRIES; i++) {
				if (servers_arr[i].get_type() != Variant::DICTIONARY) {
					continue;
				}
				const Dictionary server_dict = servers_arr[i];
				AIMCPServerEntry entry = AIToolRegistry::make_mcp_server();
				entry.name = server_dict.get("name", String());
				entry.command = server_dict.get("command", String());
				entry.arguments = server_dict.get("arguments", String());
				entry.url = server_dict.get("url", String());
				entry.environment = server_dict.get("environment", server_dict.get("env", Dictionary()));
				entry.capabilities_json = server_dict.get("capabilities_json", String());
				if (!entry.name.is_empty()) {
					r_servers.push_back(entry);
				}
			}
		}
	}

	return r_servers.is_empty() ? ERR_PARSE_ERROR : OK;
}

Error AIImporter::_parse_text_memory(const String &p_path, const String &p_content, AIMemoryEntry &r_memory) {
	r_memory = AIMemoryStore::make_entry();
	r_memory.title = p_path.get_file().get_basename(); // filename without extension as title.
	r_memory.content = p_content.strip_edges();
	return r_memory.content.is_empty() ? ERR_PARSE_ERROR : OK;
}

Error AIImporter::preview_from_file(const String &p_path, Vector<AISuggestion> &r_suggestions) {
	r_suggestions.clear();

	const int64_t size = FileAccess::get_size(p_path);
	if (size < 0) {
		return ERR_FILE_NOT_FOUND;
	}
	if (size > MAX_FILE_SIZE) {
		return ERR_FILE_UNRECOGNIZED;
	}

	Error err = OK;
	const String content = FileAccess::get_file_as_string(p_path, &err);
	if (err != OK || content.is_empty()) {
		return err != OK ? err : ERR_PARSE_ERROR;
	}

	const FileFormat format = _detect_format(p_path);
	switch (format) {
		case FORMAT_SKILL_MD: {
			AISuggestion s;
			s.type = AISuggestion::TYPE_SKILL;
			const Error parse_err = _parse_skill_md(content, s.skill);
			if (parse_err == OK) {
				r_suggestions.push_back(s);
			}
		} break;
		case FORMAT_MCP_JSON: {
			Vector<AIMCPServerEntry> servers;
			const Error parse_err = _parse_mcp_json(content, servers);
			if (parse_err == OK) {
				for (int i = 0; i < servers.size() && r_suggestions.size() < MAX_IMPORT_ENTRIES; i++) {
					AISuggestion s;
					s.type = AISuggestion::TYPE_MCP_SERVER;
					s.mcp_server = servers[i];
					r_suggestions.push_back(s);
				}
			}
		} break;
		case FORMAT_TEXT_MEMORY: {
			AISuggestion s;
			s.type = AISuggestion::TYPE_MEMORY;
			const Error parse_err = _parse_text_memory(p_path, content, s.memory);
			if (parse_err == OK) {
				r_suggestions.push_back(s);
			}
		} break;
		default:
			return ERR_PARSE_ERROR;
	}

	return r_suggestions.is_empty() ? ERR_PARSE_ERROR : OK;
}

Error AIImporter::preview_from_directory(const String &p_dir, Vector<AISuggestion> &r_suggestions) {
	r_suggestions.clear();

	Ref<DirAccess> dir = DirAccess::open(p_dir);
	if (dir.is_null()) {
		return ERR_FILE_NOT_FOUND;
	}

	dir->list_dir_begin();
	String filename = dir->get_next();
	while (!filename.is_empty() && r_suggestions.size() < MAX_IMPORT_ENTRIES) {
		if (filename == "." || filename == ".." || filename.begins_with(".")) {
			filename = dir->get_next();
			continue;
		}

		const String full_path = p_dir.path_join(filename);
		if (dir->current_is_dir()) {
			// Recurse into subdirectories.
			Vector<AISuggestion> sub_suggestions;
			preview_from_directory(full_path, sub_suggestions);
			for (int i = 0; i < sub_suggestions.size() && r_suggestions.size() < MAX_IMPORT_ENTRIES; i++) {
				r_suggestions.push_back(sub_suggestions[i]);
			}
		} else {
			Vector<AISuggestion> file_suggestions;
			const Error err = preview_from_file(full_path, file_suggestions);
			if (err == OK) {
				for (int i = 0; i < file_suggestions.size() && r_suggestions.size() < MAX_IMPORT_ENTRIES; i++) {
					r_suggestions.push_back(file_suggestions[i]);
				}
			}
		}
		filename = dir->get_next();
	}
	dir->list_dir_end();

	return r_suggestions.is_empty() ? ERR_PARSE_ERROR : OK;
}

Error AIImporter::import_suggestions(const Vector<AISuggestion> &p_suggestions) {
	if (p_suggestions.is_empty()) {
		return OK;
	}

	// Collect suggestions by type.
	Vector<AISkillEntry> new_skills;
	Vector<AIMCPServerEntry> new_mcp_servers;
	Vector<AIMemoryEntry> new_memories;

	for (int i = 0; i < p_suggestions.size(); i++) {
		switch (p_suggestions[i].type) {
			case AISuggestion::TYPE_SKILL:
				new_skills.push_back(p_suggestions[i].skill);
				break;
			case AISuggestion::TYPE_MCP_SERVER:
				new_mcp_servers.push_back(p_suggestions[i].mcp_server);
				break;
			case AISuggestion::TYPE_MEMORY:
				new_memories.push_back(p_suggestions[i].memory);
				break;
		}
	}

	// Save skills.
	if (!new_skills.is_empty()) {
		Vector<AISkillEntry> skills;
		Vector<AIMCPServerEntry> mcp_servers;
		AIToolRegistry::load(skills, mcp_servers);
		for (int i = 0; i < new_skills.size(); i++) {
			skills.push_back(new_skills[i]);
		}
		AIToolRegistry::save(skills, mcp_servers);
	}

	// Save MCP servers.
	if (!new_mcp_servers.is_empty()) {
		Vector<AISkillEntry> skills;
		Vector<AIMCPServerEntry> mcp_servers;
		AIToolRegistry::load(skills, mcp_servers);
		for (int i = 0; i < new_mcp_servers.size(); i++) {
			mcp_servers.push_back(new_mcp_servers[i]);
		}
		AIToolRegistry::save(skills, mcp_servers);
	}

	// Save memories.
	if (!new_memories.is_empty()) {
		Vector<AIMemoryEntry> entries;
		AIMemoryStore::load(entries);
		for (int i = 0; i < new_memories.size(); i++) {
			entries.push_back(new_memories[i]);
		}
		AIMemoryStore::save(entries);
	}

	return OK;
}
