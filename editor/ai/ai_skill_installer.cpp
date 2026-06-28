/*  ai_skill_installer.cpp                                                 */
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

#include "ai_skill_installer.h"

#include "ai_defaults_data.h"
#include "ai_importer.h"
#include "ai_tool_registry.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/templates/hash_set.h"
#include "editor/file_system/editor_paths.h"

String AISkillInstaller::_find_defaults_dir() {
	// Bundled with engine installation: {editor_data_dir}/ai_defaults/
	if (EditorPaths::get_singleton()) {
		const String data_dir = EditorPaths::get_singleton()->get_data_dir();
		if (!data_dir.is_empty()) {
			const String candidate = data_dir.path_join("ai_defaults");
			if (DirAccess::exists(candidate)) {
				return candidate;
			}
		}
	}
	return String();
}

String AISkillInstaller::_find_fallback_defaults_dir() {
	// Development mode: {executable_dir}/../editor/ai/defaults/
	// This covers running from bin/ while the source tree is one level up.
	const String exe_dir = OS::get_singleton()->get_executable_path().get_base_dir();
	const String candidate = exe_dir.path_join("../editor/ai/defaults");
	if (DirAccess::exists(candidate)) {
		return candidate;
	}
	return String();
}

String AISkillInstaller::get_defaults_dir() {
	String dir = _find_defaults_dir();
	if (!dir.is_empty()) {
		return dir;
	}
	return _find_fallback_defaults_dir();
}

bool AISkillInstaller::_install_builtin_defaults() {
	// Fallback for packaged builds: install default skills compiled into the binary.
	Vector<AISkillEntry> builtins = AIDefaultsData::get_default_skills();
	if (builtins.is_empty()) {
		return false;
	}

	// 1. Load the project's existing skills.
	Vector<AISkillEntry> existing_skills;
	Vector<AIMCPServerEntry> existing_mcp_servers;
	AIToolRegistry::load(existing_skills, existing_mcp_servers);

	// 2. Build a set of existing skill names for deduplication.
	HashSet<String> existing_names;
	for (const AISkillEntry &entry : existing_skills) {
		existing_names.insert(entry.name.strip_edges().to_lower());
	}

	// 3. Only install built-in skills that aren't already present.
	Vector<AISkillEntry> new_skills;
	for (int i = 0; i < builtins.size(); i++) {
		const String name = builtins[i].name.strip_edges().to_lower();
		if (existing_names.has(name)) {
			continue;
		}
		AISkillEntry entry = AIToolRegistry::make_skill(builtins[i].name);
		entry.description = builtins[i].description;
		entry.prompt_text = builtins[i].prompt_text;
		new_skills.push_back(entry);
	}

	if (new_skills.is_empty()) {
		return false;
	}

	// 4. Merge new skills with existing ones and save.
	for (int i = 0; i < new_skills.size(); i++) {
		existing_skills.push_back(new_skills[i]);
	}
	AIToolRegistry::save(existing_skills, existing_mcp_servers);
	return true;
}

bool AISkillInstaller::ensure_defaults_installed() {
	// 1. Find the bundled defaults directory.
	const String defaults_dir = get_defaults_dir();
	if (defaults_dir.is_empty()) {
		// Fall back to built-in data compiled into the binary.
		return _install_builtin_defaults();
	}

	// 2. Preview all default skills from the directory.
	Vector<AISuggestion> suggestions;
	const Error preview_err = AIImporter::preview_from_directory(defaults_dir, suggestions);
	if (preview_err != OK || suggestions.is_empty()) {
		return false;
	}

	// 3. Load the project's existing skills to avoid duplicates.
	Vector<AISkillEntry> existing_skills;
	Vector<AIMCPServerEntry> existing_mcp_servers;
	AIToolRegistry::load(existing_skills, existing_mcp_servers);

	// 4. Build a set of existing skill names for fast lookup.
	HashSet<String> existing_names;
	for (const AISkillEntry &entry : existing_skills) {
		existing_names.insert(entry.name.strip_edges().to_lower());
	}

	// 5. Filter out defaults that already exist, keep only new ones.
	Vector<AISuggestion> new_suggestions;
	for (const AISuggestion &s : suggestions) {
		if (s.type != AISuggestion::TYPE_SKILL) {
			continue;
		}
		const String name = s.skill.name.strip_edges().to_lower();
		if (!existing_names.has(name)) {
			new_suggestions.push_back(s);
		}
	}

	// 6. Import only the missing ones.
	if (!new_suggestions.is_empty()) {
		AIImporter::import_suggestions(new_suggestions);
		return true;
	}

	return false;
}
