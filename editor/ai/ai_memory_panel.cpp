/*  ai_memory_panel.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#include "ai_memory_panel.h"

#include "ai_importer.h"
#include "ai_memory_store.h"
#include "ai_tool_registry.h"

#include "core/error/error_macros.h"
#include "core/object/callable_mp.h"
#include "core/os/time.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/text_edit.h"

void AIMemoryPanel::_bind_methods() {
}

void AIMemoryPanel::_notification(int p_what) {
	if (p_what == NOTIFICATION_TRANSLATION_CHANGED) {
		_update_translations();
	}
}

Vector<String> AIMemoryPanel::_tags_from_text(const String &p_text) const {
	Vector<String> tags;
	PackedStringArray parts = p_text.split(",");
	for (int i = 0; i < parts.size(); i++) {
		const String tag = parts[i].strip_edges();
		if (!tag.is_empty()) {
			tags.push_back(tag);
		}
	}
	return tags;
}

String AIMemoryPanel::_tags_to_text(const Vector<String> &p_tags) const {
	String text;
	for (int i = 0; i < p_tags.size(); i++) {
		if (i > 0) {
			text += ", ";
		}
		text += p_tags[i];
	}
	return text;
}

void AIMemoryPanel::_set_editor_enabled(bool p_enabled) {
	enabled_check->set_disabled(!p_enabled);
	title_edit->set_editable(p_enabled);
	tags_edit->set_editable(p_enabled);
	content_edit->set_editable(p_enabled);
	delete_button->set_disabled(!p_enabled);
	save_button->set_disabled(!p_enabled);
}

void AIMemoryPanel::_update_editor() {
	const bool has_selection = selected_index >= 0 && selected_index < entries.size();
	_set_editor_enabled(has_selection);

	if (!has_selection) {
		enabled_check->set_pressed(false);
		title_edit->clear();
		tags_edit->clear();
		content_edit->clear();
		return;
	}

	const AIMemoryEntry &entry = entries[selected_index];
	enabled_check->set_pressed(entry.enabled);
	title_edit->set_text(entry.title);
	tags_edit->set_text(_tags_to_text(entry.tags));
	content_edit->set_text(entry.content);
}

void AIMemoryPanel::_update_selected_from_editor() {
	ERR_FAIL_INDEX(selected_index, entries.size());

	AIMemoryEntry &entry = entries.write[selected_index];
	entry.enabled = enabled_check->is_pressed();
	entry.title = title_edit->get_text().strip_edges();
	entry.content = content_edit->get_text();
	entry.tags = _tags_from_text(tags_edit->get_text());
	entry.updated_at = Time::get_singleton()->get_datetime_string_from_system(false, false);
}

void AIMemoryPanel::_refresh_list() {
	memory_list->clear();
	for (int i = 0; i < entries.size(); i++) {
		String title = entries[i].title.strip_edges();
		if (title.is_empty()) {
			title = TTR("Untitled Memory");
		}
		if (!entries[i].enabled) {
			title += TTR(" (disabled)");
		}
		memory_list->add_item(title);
		memory_list->set_item_tooltip(-1, entries[i].content.left(240));
	}

	if (selected_index >= entries.size()) {
		selected_index = entries.size() - 1;
	}
	if (selected_index >= 0) {
		memory_list->select(selected_index);
	}
	_update_editor();
}

void AIMemoryPanel::_load_entries() {
	const Error err = AIMemoryStore::load(entries);
	if (err != OK) {
		status_label->set_text(TTR("Could not load AI memories."));
		entries.clear();
		selected_index = -1;
		_refresh_list();
		return;
	}

	selected_index = entries.is_empty() ? -1 : 0;
	_refresh_list();
	status_label->set_text(TTR("AI memories loaded."));
}

void AIMemoryPanel::_save_entries() {
	const Error err = AIMemoryStore::save(entries);
	if (err != OK) {
		status_label->set_text(TTR("Could not save AI memories."));
		return;
	}
	status_label->set_text(TTR("AI memories saved."));
}

void AIMemoryPanel::_select_memory(int p_index) {
	if (selected_index >= 0 && selected_index < entries.size()) {
		_update_selected_from_editor();
	}

	selected_index = p_index;
	_update_editor();
}

void AIMemoryPanel::_new_memory() {
	if (selected_index >= 0 && selected_index < entries.size()) {
		_update_selected_from_editor();
	}

	AIMemoryEntry entry = AIMemoryStore::make_entry(TTR("New Memory"), String());
	entries.push_back(entry);
	selected_index = entries.size() - 1;
	_refresh_list();
	status_label->set_text(TTR("New memory created."));
}

void AIMemoryPanel::_delete_memory() {
	ERR_FAIL_INDEX(selected_index, entries.size());
	entries.remove_at(selected_index);
	if (selected_index >= entries.size()) {
		selected_index = entries.size() - 1;
	}
	_refresh_list();
	_save_entries();
}

void AIMemoryPanel::_save_memory() {
	if (selected_index >= 0 && selected_index < entries.size()) {
		_update_selected_from_editor();
	}
	_refresh_list();
	_save_entries();
}

void AIMemoryPanel::_reload_memory() {
	_load_entries();
}

void AIMemoryPanel::_import_button_pressed() {
	import_file_dialog->popup_file_dialog();
}

void AIMemoryPanel::_import_file_selected(const String &p_path) {
	Vector<AISuggestion> suggestions;
	const Error err = AIImporter::preview_from_file(p_path, suggestions);
	if (err != OK || suggestions.is_empty()) {
		status_label->set_text(TTR("No importable entries found in the selected file."));
		return;
	}
	_apply_import_suggestions(suggestions);
}

void AIMemoryPanel::_import_dir_selected(const String &p_dir) {
	Vector<AISuggestion> suggestions;
	const Error err = AIImporter::preview_from_directory(p_dir, suggestions);
	if (err != OK || suggestions.is_empty()) {
		status_label->set_text(TTR("No importable entries found in the selected directory."));
		return;
	}
	_apply_import_suggestions(suggestions);
}

void AIMemoryPanel::_apply_import_suggestions(const Vector<AISuggestion> &p_suggestions) {
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

	// Import memories into this panel.
	if (!new_memories.is_empty()) {
		for (int i = 0; i < new_memories.size(); i++) {
			entries.push_back(new_memories[i]);
		}
		_refresh_list();
		_save_entries();
	}

	// Import skills and MCP servers into the tool registry.
	if (!new_skills.is_empty() || !new_mcp_servers.is_empty()) {
		Vector<AISkillEntry> skills;
		Vector<AIMCPServerEntry> mcp_servers;
		AIToolRegistry::load(skills, mcp_servers);
		for (int i = 0; i < new_skills.size(); i++) {
			skills.push_back(new_skills[i]);
		}
		for (int i = 0; i < new_mcp_servers.size(); i++) {
			mcp_servers.push_back(new_mcp_servers[i]);
		}
		AIToolRegistry::save(skills, mcp_servers);
	}

	String summary;
	if (!new_memories.is_empty()) {
		summary += vformat(TTR("%d memory(ies)"), new_memories.size());
	}
	if (!new_skills.is_empty()) {
		if (!summary.is_empty()) {
			summary += ", ";
		}
		summary += vformat(TTR("%d skill(s)"), new_skills.size());
	}
	if (!new_mcp_servers.is_empty()) {
		if (!summary.is_empty()) {
			summary += ", ";
		}
		summary += vformat(TTR("%d MCP server(s)"), new_mcp_servers.size());
	}
	if (!summary.is_empty()) {
		status_label->set_text(vformat(TTR("Imported: %s."), summary));
	}
}

void AIMemoryPanel::_update_translations() {
	set_name(TTRC("Memory"));
	new_button->set_text(TTR("New"));
	delete_button->set_text(TTR("Delete"));
	save_button->set_text(TTR("Save"));
	reload_button->set_text(TTR("Reload"));
	import_button->set_text(TTR("Import..."));
	enabled_check->set_text(TTR("Enabled for AI context"));
	title_edit->set_placeholder(TTR("Memory title"));
	tags_edit->set_placeholder(TTR("Tags, separated by commas"));
	content_edit->set_placeholder(TTR("Project fact, preference, or recurring issue for the AI assistant."));
}

AIMemoryPanel::AIMemoryPanel() {
	set_name(TTRC("Memory"));
	add_theme_constant_override("margin_left", 8 * EDSCALE);
	add_theme_constant_override("margin_top", 8 * EDSCALE);
	add_theme_constant_override("margin_right", 8 * EDSCALE);
	add_theme_constant_override("margin_bottom", 8 * EDSCALE);

	HBoxContainer *root = memnew(HBoxContainer);
	root->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	root->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	root->add_theme_constant_override("separation", 8 * EDSCALE);
	add_child(root);

	VBoxContainer *left = memnew(VBoxContainer);
	left->set_custom_minimum_size(Size2(150, 0) * EDSCALE);
	left->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	root->add_child(left);

	memory_list = memnew(ItemList);
	memory_list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	memory_list->connect(SceneStringName(item_selected), callable_mp(this, &AIMemoryPanel::_select_memory));
	left->add_child(memory_list);

	HBoxContainer *list_actions = memnew(HBoxContainer);
	list_actions->add_theme_constant_override("separation", 4 * EDSCALE);
	left->add_child(list_actions);

	new_button = memnew(Button);
	new_button->connect(SceneStringName(pressed), callable_mp(this, &AIMemoryPanel::_new_memory));
	list_actions->add_child(new_button);

	reload_button = memnew(Button);
	reload_button->connect(SceneStringName(pressed), callable_mp(this, &AIMemoryPanel::_reload_memory));
	list_actions->add_child(reload_button);

	import_button = memnew(Button);
	import_button->connect(SceneStringName(pressed), callable_mp(this, &AIMemoryPanel::_import_button_pressed));
	list_actions->add_child(import_button);

	PanelContainer *editor_panel = memnew(PanelContainer);
	editor_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	editor_panel->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	root->add_child(editor_panel);

	VBoxContainer *editor = memnew(VBoxContainer);
	editor->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	editor->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	editor->add_theme_constant_override("separation", 6 * EDSCALE);
	editor_panel->add_child(editor);

	enabled_check = memnew(CheckBox);
	editor->add_child(enabled_check);

	GridContainer *grid = memnew(GridContainer);
	grid->set_columns(2);
	grid->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	editor->add_child(grid);

	Label *title_label = memnew(Label);
	title_label->set_text(TTR("Title"));
	grid->add_child(title_label);

	title_edit = memnew(LineEdit);
	title_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	grid->add_child(title_edit);

	Label *tags_label = memnew(Label);
	tags_label->set_text(TTR("Tags"));
	grid->add_child(tags_label);

	tags_edit = memnew(LineEdit);
	tags_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	grid->add_child(tags_edit);

	content_edit = memnew(TextEdit);
	content_edit->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	content_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	content_edit->set_custom_minimum_size(Size2(0, 180) * EDSCALE);
	editor->add_child(content_edit);

	HBoxContainer *actions = memnew(HBoxContainer);
	actions->add_theme_constant_override("separation", 6 * EDSCALE);
	editor->add_child(actions);

	save_button = memnew(Button);
	save_button->connect(SceneStringName(pressed), callable_mp(this, &AIMemoryPanel::_save_memory));
	actions->add_child(save_button);

	delete_button = memnew(Button);
	delete_button->connect(SceneStringName(pressed), callable_mp(this, &AIMemoryPanel::_delete_memory));
	actions->add_child(delete_button);

	status_label = memnew(Label);
	status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	status_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	editor->add_child(status_label);

	// Import file dialog.
	import_file_dialog = memnew(EditorFileDialog);
	import_file_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	import_file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
	import_file_dialog->add_filter("*.md,*.json,*.txt", TTRC("Skill / MCP / Memory Files"));
	import_file_dialog->connect(SNAME("file_selected"), callable_mp(this, &AIMemoryPanel::_import_file_selected));
	add_child(import_file_dialog);

	// Import directory dialog.
	import_dir_dialog = memnew(EditorFileDialog);
	import_dir_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	import_dir_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_DIR);
	import_dir_dialog->connect(SNAME("dir_selected"), callable_mp(this, &AIMemoryPanel::_import_dir_selected));
	add_child(import_dir_dialog);

	_update_translations();
	_load_entries();
}
