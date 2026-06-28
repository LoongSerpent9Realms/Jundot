/*  ai_memory_store.h                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#pragma once

#include "core/error/error_list.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

struct AIMemoryEntry {
	String id;
	String title;
	String content;
	Vector<String> tags;
	bool enabled = true;
	String created_at;
	String updated_at;
};

class AIMemoryStore {
	static constexpr int SCHEMA_VERSION = 1;

	static String _get_default_path();
	static String _now_string();
	static String _make_id(const String &p_prefix);
	static Dictionary _entry_to_dict(const AIMemoryEntry &p_entry);
	static AIMemoryEntry _entry_from_dict(const Dictionary &p_dict);
	static Vector<String> _string_array_from_variant(const Variant &p_value);
	static Array _string_array_to_variant(const Vector<String> &p_values);
	static Error _ensure_parent_dir(const String &p_path);

public:
	static String get_default_path();
	static AIMemoryEntry make_entry(const String &p_title = String(), const String &p_content = String());
	static Error load(Vector<AIMemoryEntry> &r_entries, const String &p_path = String());
	static Error save(const Vector<AIMemoryEntry> &p_entries, const String &p_path = String());
};
