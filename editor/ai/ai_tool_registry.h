/**************************************************************************/
/*  ai_tool_registry.h                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/**************************************************************************/

#pragma once

#include "core/error/error_list.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"

struct AISkillEntry {
	String id;
	String name;
	String description;
	String prompt_text;
	String permission_level = "read";
	bool enabled = true;
	bool writes = false;
	bool requires_confirmation = true;
	bool read_only_allowed = true;
	String created_at;
	String updated_at;
};

struct AIMCPServerEntry {
	String id;
	String name;
	String command;
	String arguments;
	String url;
	String capabilities_json;
	bool enabled = true;
	bool requires_confirmation = true;
	bool writes = false;
	bool read_only_allowed = true;
	String created_at;
	String updated_at;
};

class AIToolRegistry {
	static constexpr int SCHEMA_VERSION = 1;

	static String _get_default_path();
	static String _now_string();
	static String _make_id(const String &p_prefix);
	static Dictionary _skill_to_dict(const AISkillEntry &p_entry);
	static AISkillEntry _skill_from_dict(const Dictionary &p_dict);
	static Dictionary _mcp_server_to_dict(const AIMCPServerEntry &p_entry);
	static AIMCPServerEntry _mcp_server_from_dict(const Dictionary &p_dict);
	static Error _ensure_parent_dir(const String &p_path);

public:
	static String get_default_path();
	static AISkillEntry make_skill(const String &p_name = String());
	static AIMCPServerEntry make_mcp_server(const String &p_name = String());
	static Error load(Vector<AISkillEntry> &r_skills, Vector<AIMCPServerEntry> &r_mcp_servers, const String &p_path = String());
	static Error save(const Vector<AISkillEntry> &p_skills, const Vector<AIMCPServerEntry> &p_mcp_servers, const String &p_path = String());
};
