/**************************************************************************/
/*  ai_tool_registry.cpp                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/**************************************************************************/

#include "ai_tool_registry.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/time.h"
#include "editor/file_system/editor_paths.h"

String AIToolRegistry::_get_default_path() {
	ERR_FAIL_NULL_V(EditorPaths::get_singleton(), String());
	return EditorPaths::get_singleton()->get_project_settings_dir().path_join("ai_tools.json");
}

String AIToolRegistry::_now_string() {
	return Time::get_singleton()->get_datetime_string_from_system(false, false);
}

String AIToolRegistry::_make_id(const String &p_prefix) {
	const int64_t unix_time = (int64_t)Time::get_singleton()->get_unix_time_from_system();
	return p_prefix + "-" + String::num_int64(unix_time);
}

Dictionary AIToolRegistry::_skill_to_dict(const AISkillEntry &p_entry) {
	Dictionary dict;
	dict["id"] = p_entry.id;
	dict["name"] = p_entry.name;
	dict["description"] = p_entry.description;
	dict["prompt_text"] = p_entry.prompt_text;
	dict["permission_level"] = p_entry.permission_level;
	dict["enabled"] = p_entry.enabled;
	dict["writes"] = p_entry.writes;
	dict["requires_confirmation"] = p_entry.requires_confirmation;
	dict["read_only_allowed"] = p_entry.read_only_allowed;
	dict["created_at"] = p_entry.created_at;
	dict["updated_at"] = p_entry.updated_at;
	return dict;
}

AISkillEntry AIToolRegistry::_skill_from_dict(const Dictionary &p_dict) {
	AISkillEntry entry;
	entry.id = p_dict.get("id", _make_id("skill"));
	entry.name = p_dict.get("name", String());
	entry.description = p_dict.get("description", String());
	entry.prompt_text = p_dict.get("prompt_text", String());
	entry.permission_level = p_dict.get("permission_level", String("read"));
	entry.enabled = p_dict.get("enabled", true);
	entry.writes = p_dict.get("writes", false);
	entry.requires_confirmation = p_dict.get("requires_confirmation", true);
	entry.read_only_allowed = p_dict.get("read_only_allowed", !entry.writes);
	entry.created_at = p_dict.get("created_at", _now_string());
	entry.updated_at = p_dict.get("updated_at", entry.created_at);
	return entry;
}

Dictionary AIToolRegistry::_mcp_server_to_dict(const AIMCPServerEntry &p_entry) {
	Dictionary dict;
	dict["id"] = p_entry.id;
	dict["name"] = p_entry.name;
	dict["command"] = p_entry.command;
	dict["arguments"] = p_entry.arguments;
	dict["url"] = p_entry.url;
	dict["capabilities_json"] = p_entry.capabilities_json;
	dict["enabled"] = p_entry.enabled;
	dict["requires_confirmation"] = p_entry.requires_confirmation;
	dict["writes"] = p_entry.writes;
	dict["read_only_allowed"] = p_entry.read_only_allowed;
	dict["created_at"] = p_entry.created_at;
	dict["updated_at"] = p_entry.updated_at;
	return dict;
}

AIMCPServerEntry AIToolRegistry::_mcp_server_from_dict(const Dictionary &p_dict) {
	AIMCPServerEntry entry;
	entry.id = p_dict.get("id", _make_id("mcp"));
	entry.name = p_dict.get("name", String());
	entry.command = p_dict.get("command", String());
	entry.arguments = p_dict.get("arguments", String());
	entry.url = p_dict.get("url", String());
	entry.capabilities_json = p_dict.get("capabilities_json", String());
	entry.enabled = p_dict.get("enabled", true);
	entry.requires_confirmation = p_dict.get("requires_confirmation", true);
	entry.writes = p_dict.get("writes", false);
	entry.read_only_allowed = p_dict.get("read_only_allowed", !entry.writes);
	entry.created_at = p_dict.get("created_at", _now_string());
	entry.updated_at = p_dict.get("updated_at", entry.created_at);
	return entry;
}

Error AIToolRegistry::_ensure_parent_dir(const String &p_path) {
	const String base_dir = p_path.get_base_dir();
	if (base_dir.is_empty()) {
		return OK;
	}
	return DirAccess::make_dir_recursive_absolute(base_dir);
}

String AIToolRegistry::get_default_path() {
	return _get_default_path();
}

AISkillEntry AIToolRegistry::make_skill(const String &p_name) {
	const String now = _now_string();
	AISkillEntry entry;
	entry.id = _make_id("skill");
	entry.name = p_name;
	entry.created_at = now;
	entry.updated_at = now;
	return entry;
}

AIMCPServerEntry AIToolRegistry::make_mcp_server(const String &p_name) {
	const String now = _now_string();
	AIMCPServerEntry entry;
	entry.id = _make_id("mcp");
	entry.name = p_name;
	entry.created_at = now;
	entry.updated_at = now;
	return entry;
}

Error AIToolRegistry::load(Vector<AISkillEntry> &r_skills, Vector<AIMCPServerEntry> &r_mcp_servers, const String &p_path) {
	r_skills.clear();
	r_mcp_servers.clear();

	const String path = p_path.is_empty() ? _get_default_path() : p_path;
	ERR_FAIL_COND_V_MSG(path.is_empty(), ERR_UNCONFIGURED, "AI tool registry path is empty.");
	if (!FileAccess::exists(path)) {
		return OK;
	}

	Error err = OK;
	const String text = FileAccess::get_file_as_string(path, &err);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Could not read AI tool registry.");
	if (text.strip_edges().is_empty()) {
		return OK;
	}

	JSON json;
	err = json.parse(text);
	ERR_FAIL_COND_V_MSG(err != OK, ERR_PARSE_ERROR, "Could not parse AI tool registry JSON.");

	const Variant data = json.get_data();
	ERR_FAIL_COND_V_MSG(data.get_type() != Variant::DICTIONARY, ERR_PARSE_ERROR, "AI tool registry root must be a dictionary.");

	Dictionary root = data;
	const Variant skills_value = root.get("skills", Array());
	ERR_FAIL_COND_V_MSG(skills_value.get_type() != Variant::ARRAY, ERR_PARSE_ERROR, "AI skill entries must be an array.");

	Array skills = skills_value;
	for (int i = 0; i < skills.size(); i++) {
		if (skills[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		r_skills.push_back(_skill_from_dict(skills[i]));
	}

	const Variant mcp_servers_value = root.get("mcp_servers", Array());
	ERR_FAIL_COND_V_MSG(mcp_servers_value.get_type() != Variant::ARRAY, ERR_PARSE_ERROR, "AI MCP server entries must be an array.");

	Array mcp_servers = mcp_servers_value;
	for (int i = 0; i < mcp_servers.size(); i++) {
		if (mcp_servers[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		r_mcp_servers.push_back(_mcp_server_from_dict(mcp_servers[i]));
	}

	return OK;
}

Error AIToolRegistry::save(const Vector<AISkillEntry> &p_skills, const Vector<AIMCPServerEntry> &p_mcp_servers, const String &p_path) {
	const String path = p_path.is_empty() ? _get_default_path() : p_path;
	ERR_FAIL_COND_V_MSG(path.is_empty(), ERR_UNCONFIGURED, "AI tool registry path is empty.");

	Error err = _ensure_parent_dir(path);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Could not create AI tool registry directory.");

	Array skills;
	for (const AISkillEntry &entry : p_skills) {
		skills.push_back(_skill_to_dict(entry));
	}

	Array mcp_servers;
	for (const AIMCPServerEntry &entry : p_mcp_servers) {
		mcp_servers.push_back(_mcp_server_to_dict(entry));
	}

	Dictionary root;
	root["schema_version"] = SCHEMA_VERSION;
	root["skills"] = skills;
	root["mcp_servers"] = mcp_servers;

	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE, &err);
	ERR_FAIL_COND_V_MSG(err != OK || file.is_null(), err, "Could not open AI tool registry for writing.");
	file->store_string(JSON::stringify(root, "\t"));
	return OK;
}
