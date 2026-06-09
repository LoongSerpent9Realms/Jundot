/*  ai_memory_store.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#include "ai_memory_store.h"

#include "core/error/error_macros.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/time.h"
#include "editor/file_system/editor_paths.h"

String AIMemoryStore::_get_default_path() {
	ERR_FAIL_NULL_V(EditorPaths::get_singleton(), String());
	return EditorPaths::get_singleton()->get_project_settings_dir().path_join("ai_memory.json");
}

String AIMemoryStore::_now_string() {
	return Time::get_singleton()->get_datetime_string_from_system(false, false);
}

String AIMemoryStore::_make_id(const String &p_prefix) {
	const int64_t unix_time = (int64_t)Time::get_singleton()->get_unix_time_from_system();
	return p_prefix + "-" + String::num_int64(unix_time);
}

Vector<String> AIMemoryStore::_string_array_from_variant(const Variant &p_value) {
	Vector<String> values;
	if (p_value.get_type() != Variant::ARRAY) {
		return values;
	}

	Array array = p_value;
	for (int i = 0; i < array.size(); i++) {
		values.push_back(String(array[i]));
	}
	return values;
}

Array AIMemoryStore::_string_array_to_variant(const Vector<String> &p_values) {
	Array array;
	for (const String &value : p_values) {
		array.push_back(value);
	}
	return array;
}

Dictionary AIMemoryStore::_entry_to_dict(const AIMemoryEntry &p_entry) {
	Dictionary dict;
	dict["id"] = p_entry.id;
	dict["title"] = p_entry.title;
	dict["content"] = p_entry.content;
	dict["tags"] = _string_array_to_variant(p_entry.tags);
	dict["enabled"] = p_entry.enabled;
	dict["created_at"] = p_entry.created_at;
	dict["updated_at"] = p_entry.updated_at;
	return dict;
}

AIMemoryEntry AIMemoryStore::_entry_from_dict(const Dictionary &p_dict) {
	AIMemoryEntry entry;
	entry.id = p_dict.get("id", _make_id("memory"));
	entry.title = p_dict.get("title", String());
	entry.content = p_dict.get("content", String());
	entry.tags = _string_array_from_variant(p_dict.get("tags", Array()));
	entry.enabled = p_dict.get("enabled", true);
	entry.created_at = p_dict.get("created_at", _now_string());
	entry.updated_at = p_dict.get("updated_at", entry.created_at);
	return entry;
}

Error AIMemoryStore::_ensure_parent_dir(const String &p_path) {
	const String base_dir = p_path.get_base_dir();
	if (base_dir.is_empty()) {
		return OK;
	}
	return DirAccess::make_dir_recursive_absolute(base_dir);
}

String AIMemoryStore::get_default_path() {
	return _get_default_path();
}

AIMemoryEntry AIMemoryStore::make_entry(const String &p_title, const String &p_content) {
	const String now = _now_string();
	AIMemoryEntry entry;
	entry.id = _make_id("memory");
	entry.title = p_title;
	entry.content = p_content;
	entry.enabled = true;
	entry.created_at = now;
	entry.updated_at = now;
	return entry;
}

Error AIMemoryStore::load(Vector<AIMemoryEntry> &r_entries, const String &p_path) {
	r_entries.clear();

	const String path = p_path.is_empty() ? _get_default_path() : p_path;
	ERR_FAIL_COND_V_MSG(path.is_empty(), ERR_UNCONFIGURED, "AI memory store path is empty.");
	if (!FileAccess::exists(path)) {
		return OK;
	}

	Error err = OK;
	const String text = FileAccess::get_file_as_string(path, &err);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Could not read AI memory store.");
	if (text.strip_edges().is_empty()) {
		return OK;
	}

	JSON json;
	err = json.parse(text);
	ERR_FAIL_COND_V_MSG(err != OK, ERR_PARSE_ERROR, "Could not parse AI memory store JSON.");

	const Variant data = json.get_data();
	ERR_FAIL_COND_V_MSG(data.get_type() != Variant::DICTIONARY, ERR_PARSE_ERROR, "AI memory store root must be a dictionary.");

	Dictionary root = data;
	const Variant entries_value = root.get("entries", Array());
	ERR_FAIL_COND_V_MSG(entries_value.get_type() != Variant::ARRAY, ERR_PARSE_ERROR, "AI memory entries must be an array.");

	Array entries = entries_value;
	for (int i = 0; i < entries.size(); i++) {
		if (entries[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		r_entries.push_back(_entry_from_dict(entries[i]));
	}

	return OK;
}

Error AIMemoryStore::save(const Vector<AIMemoryEntry> &p_entries, const String &p_path) {
	const String path = p_path.is_empty() ? _get_default_path() : p_path;
	ERR_FAIL_COND_V_MSG(path.is_empty(), ERR_UNCONFIGURED, "AI memory store path is empty.");

	Error err = _ensure_parent_dir(path);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Could not create AI memory store directory.");

	Array entries;
	for (const AIMemoryEntry &entry : p_entries) {
		entries.push_back(_entry_to_dict(entry));
	}

	Dictionary root;
	root["schema_version"] = SCHEMA_VERSION;
	root["entries"] = entries;

	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE, &err);
	ERR_FAIL_COND_V_MSG(err != OK || file.is_null(), err, "Could not open AI memory store for writing.");
	file->store_string(JSON::stringify(root, "\t"));
	return OK;
}
