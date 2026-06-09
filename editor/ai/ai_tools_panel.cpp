/*  ai_tools_panel.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#include "ai_tools_panel.h"

#include "ai_importer.h"
#include "ai_memory_store.h"
#include "ai_skill_installer.h"

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
#include "scene/gui/tab_container.h"
#include "scene/gui/text_edit.h"

void AIToolsPanel::_bind_methods() {
}

void AIToolsPanel::_notification(int p_what) {
	if (p_what == NOTIFICATION_TRANSLATION_CHANGED) {
		_update_translations();
	}
}

static String _ai_tools_now_string() {
	return Time::get_singleton()->get_datetime_string_from_system(false, false);
}

void AIToolsPanel::_set_skill_editor_enabled(bool p_enabled) {
	skill_enabled->set_disabled(!p_enabled);
	skill_writes->set_disabled(!p_enabled);
	skill_requires_confirmation->set_disabled(!p_enabled);
	skill_read_only_allowed->set_disabled(!p_enabled);
	skill_name->set_editable(p_enabled);
	skill_description->set_editable(p_enabled);
	skill_permission->set_editable(p_enabled);
	skill_prompt->set_editable(p_enabled);
	skill_delete_button->set_disabled(!p_enabled);
	skill_save_button->set_disabled(!p_enabled);
}

void AIToolsPanel::_set_mcp_editor_enabled(bool p_enabled) {
	mcp_enabled->set_disabled(!p_enabled);
	mcp_writes->set_disabled(!p_enabled);
	mcp_requires_confirmation->set_disabled(!p_enabled);
	mcp_read_only_allowed->set_disabled(!p_enabled);
	mcp_name->set_editable(p_enabled);
	mcp_command->set_editable(p_enabled);
	mcp_arguments->set_editable(p_enabled);
	mcp_url->set_editable(p_enabled);
	mcp_capabilities->set_editable(p_enabled);
	mcp_delete_button->set_disabled(!p_enabled);
	mcp_save_button->set_disabled(!p_enabled);
}

void AIToolsPanel::_update_skill_editor() {
	const bool has_selection = selected_skill >= 0 && selected_skill < skills.size();
	_set_skill_editor_enabled(has_selection);

	if (!has_selection) {
		skill_enabled->set_pressed(false);
		skill_writes->set_pressed(false);
		skill_requires_confirmation->set_pressed(true);
		skill_read_only_allowed->set_pressed(true);
		skill_name->clear();
		skill_description->clear();
		skill_permission->clear();
		skill_prompt->clear();
		return;
	}

	const AISkillEntry &entry = skills[selected_skill];
	skill_enabled->set_pressed(entry.enabled);
	skill_writes->set_pressed(entry.writes);
	skill_requires_confirmation->set_pressed(entry.requires_confirmation);
	skill_read_only_allowed->set_pressed(entry.read_only_allowed);
	skill_name->set_text(entry.name);
	skill_description->set_text(entry.description);
	skill_permission->set_text(entry.permission_level);
	skill_prompt->set_text(entry.prompt_text);
}

void AIToolsPanel::_update_mcp_editor() {
	const bool has_selection = selected_mcp_server >= 0 && selected_mcp_server < mcp_servers.size();
	_set_mcp_editor_enabled(has_selection);

	if (!has_selection) {
		mcp_enabled->set_pressed(false);
		mcp_writes->set_pressed(false);
		mcp_requires_confirmation->set_pressed(true);
		mcp_read_only_allowed->set_pressed(true);
		mcp_name->clear();
		mcp_command->clear();
		mcp_arguments->clear();
		mcp_url->clear();
		mcp_capabilities->clear();
		return;
	}

	const AIMCPServerEntry &entry = mcp_servers[selected_mcp_server];
	mcp_enabled->set_pressed(entry.enabled);
	mcp_writes->set_pressed(entry.writes);
	mcp_requires_confirmation->set_pressed(entry.requires_confirmation);
	mcp_read_only_allowed->set_pressed(entry.read_only_allowed);
	mcp_name->set_text(entry.name);
	mcp_command->set_text(entry.command);
	mcp_arguments->set_text(entry.arguments);
	mcp_url->set_text(entry.url);
	mcp_capabilities->set_text(entry.capabilities_json);
}

void AIToolsPanel::_update_selected_skill_from_editor() {
	ERR_FAIL_INDEX(selected_skill, skills.size());
	AISkillEntry &entry = skills.write[selected_skill];
	entry.enabled = skill_enabled->is_pressed();
	entry.writes = skill_writes->is_pressed();
	entry.requires_confirmation = skill_requires_confirmation->is_pressed();
	entry.read_only_allowed = skill_read_only_allowed->is_pressed();
	entry.name = skill_name->get_text().strip_edges();
	entry.description = skill_description->get_text().strip_edges();
	entry.permission_level = skill_permission->get_text().strip_edges();
	entry.prompt_text = skill_prompt->get_text();
	entry.updated_at = _ai_tools_now_string();
}

void AIToolsPanel::_update_selected_mcp_from_editor() {
	ERR_FAIL_INDEX(selected_mcp_server, mcp_servers.size());
	AIMCPServerEntry &entry = mcp_servers.write[selected_mcp_server];
	entry.enabled = mcp_enabled->is_pressed();
	entry.writes = mcp_writes->is_pressed();
	entry.requires_confirmation = mcp_requires_confirmation->is_pressed();
	entry.read_only_allowed = mcp_read_only_allowed->is_pressed();
	entry.name = mcp_name->get_text().strip_edges();
	entry.command = mcp_command->get_text().strip_edges();
	entry.arguments = mcp_arguments->get_text().strip_edges();
	entry.url = mcp_url->get_text().strip_edges();
	entry.capabilities_json = mcp_capabilities->get_text();
	entry.updated_at = _ai_tools_now_string();
}

void AIToolsPanel::_refresh_skill_list() {
	skill_list->clear();
	for (int i = 0; i < skills.size(); i++) {
		String name = skills[i].name.strip_edges();
		if (name.is_empty()) {
			name = TTR("Untitled Skill");
		}
		if (!skills[i].enabled) {
			name += TTR(" (disabled)");
		}
		skill_list->add_item(name);
		skill_list->set_item_tooltip(-1, skills[i].description);
	}

	if (selected_skill >= skills.size()) {
		selected_skill = skills.size() - 1;
	}
	if (selected_skill >= 0) {
		skill_list->select(selected_skill);
	}
	_update_skill_editor();
}

void AIToolsPanel::_refresh_mcp_list() {
	mcp_list->clear();
	for (int i = 0; i < mcp_servers.size(); i++) {
		String name = mcp_servers[i].name.strip_edges();
		if (name.is_empty()) {
			name = TTR("Untitled MCP Server");
		}
		if (!mcp_servers[i].enabled) {
			name += TTR(" (disabled)");
		}
		mcp_list->add_item(name);
		mcp_list->set_item_tooltip(-1, mcp_servers[i].command.is_empty() ? mcp_servers[i].url : mcp_servers[i].command);
	}

	if (selected_mcp_server >= mcp_servers.size()) {
		selected_mcp_server = mcp_servers.size() - 1;
	}
	if (selected_mcp_server >= 0) {
		mcp_list->select(selected_mcp_server);
	}
	_update_mcp_editor();
}

void AIToolsPanel::_load_registry() {
	// Auto-install bundled default skills if this is a fresh project.
	AISkillInstaller::ensure_defaults_installed();

	const Error err = AIToolRegistry::load(skills, mcp_servers);
	if (err != OK) {
		skills.clear();
		mcp_servers.clear();
		selected_skill = -1;
		selected_mcp_server = -1;
		status_label->set_text(TTR("Could not load AI tool registry."));
		_refresh_skill_list();
		_refresh_mcp_list();
		return;
	}

	selected_skill = skills.is_empty() ? -1 : 0;
	selected_mcp_server = mcp_servers.is_empty() ? -1 : 0;
	_refresh_skill_list();
	_refresh_mcp_list();
	status_label->set_text(TTR("AI tool registry loaded."));
}

void AIToolsPanel::_save_registry() {
	const Error err = AIToolRegistry::save(skills, mcp_servers);
	if (err != OK) {
		status_label->set_text(TTR("Could not save AI tool registry."));
		return;
	}
	status_label->set_text(TTR("AI tool registry saved."));
}

void AIToolsPanel::_select_skill(int p_index) {
	if (selected_skill >= 0 && selected_skill < skills.size()) {
		_update_selected_skill_from_editor();
	}
	selected_skill = p_index;
	_update_skill_editor();
}

void AIToolsPanel::_select_mcp_server(int p_index) {
	if (selected_mcp_server >= 0 && selected_mcp_server < mcp_servers.size()) {
		_update_selected_mcp_from_editor();
	}
	selected_mcp_server = p_index;
	_update_mcp_editor();
}

void AIToolsPanel::_new_skill() {
	if (selected_skill >= 0 && selected_skill < skills.size()) {
		_update_selected_skill_from_editor();
	}
	skills.push_back(AIToolRegistry::make_skill(TTR("New Skill")));
	selected_skill = skills.size() - 1;
	_refresh_skill_list();
	status_label->set_text(TTR("New skill created."));
}

void AIToolsPanel::_delete_skill() {
	ERR_FAIL_INDEX(selected_skill, skills.size());
	skills.remove_at(selected_skill);
	if (selected_skill >= skills.size()) {
		selected_skill = skills.size() - 1;
	}
	_refresh_skill_list();
	_save_registry();
}

void AIToolsPanel::_save_skill() {
	if (selected_skill >= 0 && selected_skill < skills.size()) {
		_update_selected_skill_from_editor();
	}
	_refresh_skill_list();
	_save_registry();
}

void AIToolsPanel::_new_mcp_server() {
	if (selected_mcp_server >= 0 && selected_mcp_server < mcp_servers.size()) {
		_update_selected_mcp_from_editor();
	}
	mcp_servers.push_back(AIToolRegistry::make_mcp_server(TTR("New MCP Server")));
	selected_mcp_server = mcp_servers.size() - 1;
	_refresh_mcp_list();
	status_label->set_text(TTR("New MCP server created."));
}

void AIToolsPanel::_delete_mcp_server() {
	ERR_FAIL_INDEX(selected_mcp_server, mcp_servers.size());
	mcp_servers.remove_at(selected_mcp_server);
	if (selected_mcp_server >= mcp_servers.size()) {
		selected_mcp_server = mcp_servers.size() - 1;
	}
	_refresh_mcp_list();
	_save_registry();
}

void AIToolsPanel::_save_mcp_server() {
	if (selected_mcp_server >= 0 && selected_mcp_server < mcp_servers.size()) {
		_update_selected_mcp_from_editor();
	}
	_refresh_mcp_list();
	_save_registry();
}

void AIToolsPanel::_reload_registry() {
	_load_registry();
}

void AIToolsPanel::_skill_import_pressed() {
	skill_import_file_dialog->popup_file_dialog();
}

void AIToolsPanel::_skill_import_file_selected(const String &p_path) {
	Vector<AISuggestion> suggestions;
	const Error err = AIImporter::preview_from_file(p_path, suggestions);
	if (err != OK || suggestions.is_empty()) {
		status_label->set_text(TTR("No importable entries found in the selected file."));
		return;
	}

	int imported = 0;
	Vector<AISkillEntry> new_skills;
	Vector<AIMCPServerEntry> new_mcp_servers;

	for (int i = 0; i < suggestions.size(); i++) {
		if (suggestions[i].type == AISuggestion::TYPE_SKILL) {
			new_skills.push_back(suggestions[i].skill);
		} else if (suggestions[i].type == AISuggestion::TYPE_MCP_SERVER) {
			new_mcp_servers.push_back(suggestions[i].mcp_server);
		}
	}

	if (!new_skills.is_empty()) {
		for (int i = 0; i < new_skills.size(); i++) {
			skills.push_back(new_skills[i]);
			imported++;
		}
		_refresh_skill_list();
		_save_registry();
	}
	if (!new_mcp_servers.is_empty()) {
		for (int i = 0; i < new_mcp_servers.size(); i++) {
			mcp_servers.push_back(new_mcp_servers[i]);
			imported++;
		}
		_refresh_mcp_list();
		_save_registry();
	}

	status_label->set_text(vformat(TTR("Imported %d entry(s)."), imported));
}

void AIToolsPanel::_skill_import_dir_selected(const String &p_dir) {
	Vector<AISuggestion> suggestions;
	const Error err = AIImporter::preview_from_directory(p_dir, suggestions);
	if (err != OK || suggestions.is_empty()) {
		status_label->set_text(TTR("No importable entries found in the selected directory."));
		return;
	}

	int imported = 0;
	for (int i = 0; i < suggestions.size(); i++) {
		if (suggestions[i].type == AISuggestion::TYPE_SKILL) {
			skills.push_back(suggestions[i].skill);
			imported++;
		} else if (suggestions[i].type == AISuggestion::TYPE_MCP_SERVER) {
			mcp_servers.push_back(suggestions[i].mcp_server);
			imported++;
		}
	}

	if (imported > 0) {
		_refresh_skill_list();
		_refresh_mcp_list();
		_save_registry();
	}
	status_label->set_text(vformat(TTR("Imported %d entry(s) from directory."), imported));
}

void AIToolsPanel::_mcp_import_pressed() {
	mcp_import_file_dialog->popup_file_dialog();
}

void AIToolsPanel::_mcp_import_file_selected(const String &p_path) {
	Vector<AISuggestion> suggestions;
	const Error err = AIImporter::preview_from_file(p_path, suggestions);
	if (err != OK || suggestions.is_empty()) {
		status_label->set_text(TTR("No importable entries found in the selected file."));
		return;
	}

	int imported = 0;
	for (int i = 0; i < suggestions.size(); i++) {
		if (suggestions[i].type == AISuggestion::TYPE_MCP_SERVER) {
			mcp_servers.push_back(suggestions[i].mcp_server);
			imported++;
		}
	}

	if (imported > 0) {
		_refresh_mcp_list();
		_save_registry();
		status_label->set_text(vformat(TTR("Imported %d MCP server(s)."), imported));
	} else {
		status_label->set_text(TTR("No MCP server entries found in the selected file."));
	}
}

void AIToolsPanel::_mcp_import_dir_selected(const String &p_dir) {
	Vector<AISuggestion> suggestions;
	const Error err = AIImporter::preview_from_directory(p_dir, suggestions);
	if (err != OK || suggestions.is_empty()) {
		status_label->set_text(TTR("No importable entries found in the selected directory."));
		return;
	}

	int imported = 0;
	for (int i = 0; i < suggestions.size(); i++) {
		if (suggestions[i].type == AISuggestion::TYPE_MCP_SERVER) {
			mcp_servers.push_back(suggestions[i].mcp_server);
			imported++;
		}
	}

	if (imported > 0) {
		_refresh_mcp_list();
		_save_registry();
		status_label->set_text(vformat(TTR("Imported %d MCP server(s) from directory."), imported));
	} else {
		status_label->set_text(TTR("No MCP server entries found in the selected directory."));
	}
}

void AIToolsPanel::_update_translations() {
	set_name(TTRC("Tools"));
	skill_new_button->set_text(TTR("New"));
	skill_delete_button->set_text(TTR("Delete"));
	skill_save_button->set_text(TTR("Save"));
	skill_import_button->set_text(TTR("Import..."));
	mcp_new_button->set_text(TTR("New"));
	mcp_delete_button->set_text(TTR("Delete"));
	mcp_save_button->set_text(TTR("Save"));
	mcp_import_button->set_text(TTR("Import..."));
	reload_button->set_text(TTR("Reload"));

	skill_enabled->set_text(TTR("Enabled for AI context"));
	skill_writes->set_text(TTR("Writes project data"));
	skill_requires_confirmation->set_text(TTR("Requires confirmation"));
	skill_read_only_allowed->set_text(TTR("Allowed in read-only mode"));
	skill_name->set_placeholder(TTR("Skill name"));
	skill_description->set_placeholder(TTR("Short description"));
	skill_permission->set_placeholder(TTR("Permission level"));
	skill_prompt->set_placeholder(TTR("Prompt text shown to the AI when this skill is enabled."));

	mcp_enabled->set_text(TTR("Enabled for AI context"));
	mcp_writes->set_text(TTR("Writes project data"));
	mcp_requires_confirmation->set_text(TTR("Requires confirmation"));
	mcp_read_only_allowed->set_text(TTR("Allowed in read-only mode"));
	mcp_name->set_placeholder(TTR("MCP server name"));
	mcp_command->set_placeholder(TTR("Command"));
	mcp_arguments->set_placeholder(TTR("Arguments"));
	mcp_url->set_placeholder(TTR("URL"));
	mcp_capabilities->set_placeholder(TTR("Capability list JSON. The editor does not execute MCP tools in this phase."));
}

AIToolsPanel::AIToolsPanel() {
	set_name(TTRC("Tools"));
	add_theme_constant_override("margin_left", 8 * EDSCALE);
	add_theme_constant_override("margin_top", 8 * EDSCALE);
	add_theme_constant_override("margin_right", 8 * EDSCALE);
	add_theme_constant_override("margin_bottom", 8 * EDSCALE);

	VBoxContainer *root = memnew(VBoxContainer);
	root->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	root->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	root->add_theme_constant_override("separation", 8 * EDSCALE);
	add_child(root);

	TabContainer *tabs = memnew(TabContainer);
	tabs->set_theme_type_variation("TabContainerInner");
	tabs->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	tabs->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	root->add_child(tabs);

	HBoxContainer *skills_tab = memnew(HBoxContainer);
	skills_tab->set_name(TTRC("Skills"));
	skills_tab->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	skills_tab->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	skills_tab->add_theme_constant_override("separation", 8 * EDSCALE);
	tabs->add_child(skills_tab);

	VBoxContainer *skill_left = memnew(VBoxContainer);
	skill_left->set_custom_minimum_size(Size2(150, 0) * EDSCALE);
	skill_left->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	skills_tab->add_child(skill_left);

	skill_list = memnew(ItemList);
	skill_list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	skill_list->connect(SceneStringName(item_selected), callable_mp(this, &AIToolsPanel::_select_skill));
	skill_left->add_child(skill_list);

	HBoxContainer *skill_list_actions = memnew(HBoxContainer);
	skill_list_actions->add_theme_constant_override("separation", 4 * EDSCALE);
	skill_left->add_child(skill_list_actions);

	skill_new_button = memnew(Button);
	skill_new_button->connect(SceneStringName(pressed), callable_mp(this, &AIToolsPanel::_new_skill));
	skill_list_actions->add_child(skill_new_button);

	skill_import_button = memnew(Button);
	skill_import_button->connect(SceneStringName(pressed), callable_mp(this, &AIToolsPanel::_skill_import_pressed));
	skill_list_actions->add_child(skill_import_button);

	PanelContainer *skill_editor_panel = memnew(PanelContainer);
	skill_editor_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	skill_editor_panel->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	skills_tab->add_child(skill_editor_panel);

	VBoxContainer *skill_editor = memnew(VBoxContainer);
	skill_editor->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	skill_editor->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	skill_editor->add_theme_constant_override("separation", 6 * EDSCALE);
	skill_editor_panel->add_child(skill_editor);

	skill_enabled = memnew(CheckBox);
	skill_editor->add_child(skill_enabled);
	skill_writes = memnew(CheckBox);
	skill_editor->add_child(skill_writes);
	skill_requires_confirmation = memnew(CheckBox);
	skill_editor->add_child(skill_requires_confirmation);
	skill_read_only_allowed = memnew(CheckBox);
	skill_editor->add_child(skill_read_only_allowed);

	GridContainer *skill_grid = memnew(GridContainer);
	skill_grid->set_columns(2);
	skill_grid->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	skill_editor->add_child(skill_grid);

	Label *skill_name_label = memnew(Label);
	skill_name_label->set_text(TTR("Name"));
	skill_grid->add_child(skill_name_label);
	skill_name = memnew(LineEdit);
	skill_name->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	skill_grid->add_child(skill_name);

	Label *skill_description_label = memnew(Label);
	skill_description_label->set_text(TTR("Description"));
	skill_grid->add_child(skill_description_label);
	skill_description = memnew(LineEdit);
	skill_description->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	skill_grid->add_child(skill_description);

	Label *skill_permission_label = memnew(Label);
	skill_permission_label->set_text(TTR("Permission"));
	skill_grid->add_child(skill_permission_label);
	skill_permission = memnew(LineEdit);
	skill_permission->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	skill_grid->add_child(skill_permission);

	skill_prompt = memnew(TextEdit);
	skill_prompt->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	skill_prompt->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	skill_prompt->set_custom_minimum_size(Size2(0, 160) * EDSCALE);
	skill_editor->add_child(skill_prompt);

	HBoxContainer *skill_actions = memnew(HBoxContainer);
	skill_actions->add_theme_constant_override("separation", 6 * EDSCALE);
	skill_editor->add_child(skill_actions);

	skill_save_button = memnew(Button);
	skill_save_button->connect(SceneStringName(pressed), callable_mp(this, &AIToolsPanel::_save_skill));
	skill_actions->add_child(skill_save_button);

	skill_delete_button = memnew(Button);
	skill_delete_button->connect(SceneStringName(pressed), callable_mp(this, &AIToolsPanel::_delete_skill));
	skill_actions->add_child(skill_delete_button);

	HBoxContainer *mcp_tab = memnew(HBoxContainer);
	mcp_tab->set_name(TTRC("MCP"));
	mcp_tab->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	mcp_tab->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	mcp_tab->add_theme_constant_override("separation", 8 * EDSCALE);
	tabs->add_child(mcp_tab);

	VBoxContainer *mcp_left = memnew(VBoxContainer);
	mcp_left->set_custom_minimum_size(Size2(150, 0) * EDSCALE);
	mcp_left->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	mcp_tab->add_child(mcp_left);

	mcp_list = memnew(ItemList);
	mcp_list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	mcp_list->connect(SceneStringName(item_selected), callable_mp(this, &AIToolsPanel::_select_mcp_server));
	mcp_left->add_child(mcp_list);

	HBoxContainer *mcp_list_actions = memnew(HBoxContainer);
	mcp_list_actions->add_theme_constant_override("separation", 4 * EDSCALE);
	mcp_left->add_child(mcp_list_actions);

	mcp_new_button = memnew(Button);
	mcp_new_button->connect(SceneStringName(pressed), callable_mp(this, &AIToolsPanel::_new_mcp_server));
	mcp_list_actions->add_child(mcp_new_button);

	mcp_import_button = memnew(Button);
	mcp_import_button->connect(SceneStringName(pressed), callable_mp(this, &AIToolsPanel::_mcp_import_pressed));
	mcp_list_actions->add_child(mcp_import_button);

	PanelContainer *mcp_editor_panel = memnew(PanelContainer);
	mcp_editor_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	mcp_editor_panel->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	mcp_tab->add_child(mcp_editor_panel);

	VBoxContainer *mcp_editor = memnew(VBoxContainer);
	mcp_editor->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	mcp_editor->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	mcp_editor->add_theme_constant_override("separation", 6 * EDSCALE);
	mcp_editor_panel->add_child(mcp_editor);

	mcp_enabled = memnew(CheckBox);
	mcp_editor->add_child(mcp_enabled);
	mcp_writes = memnew(CheckBox);
	mcp_editor->add_child(mcp_writes);
	mcp_requires_confirmation = memnew(CheckBox);
	mcp_editor->add_child(mcp_requires_confirmation);
	mcp_read_only_allowed = memnew(CheckBox);
	mcp_editor->add_child(mcp_read_only_allowed);

	GridContainer *mcp_grid = memnew(GridContainer);
	mcp_grid->set_columns(2);
	mcp_grid->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	mcp_editor->add_child(mcp_grid);

	Label *mcp_name_label = memnew(Label);
	mcp_name_label->set_text(TTR("Name"));
	mcp_grid->add_child(mcp_name_label);
	mcp_name = memnew(LineEdit);
	mcp_name->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	mcp_grid->add_child(mcp_name);

	Label *mcp_command_label = memnew(Label);
	mcp_command_label->set_text(TTR("Command"));
	mcp_grid->add_child(mcp_command_label);
	mcp_command = memnew(LineEdit);
	mcp_command->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	mcp_grid->add_child(mcp_command);

	Label *mcp_arguments_label = memnew(Label);
	mcp_arguments_label->set_text(TTR("Arguments"));
	mcp_grid->add_child(mcp_arguments_label);
	mcp_arguments = memnew(LineEdit);
	mcp_arguments->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	mcp_grid->add_child(mcp_arguments);

	Label *mcp_url_label = memnew(Label);
	mcp_url_label->set_text(TTR("URL"));
	mcp_grid->add_child(mcp_url_label);
	mcp_url = memnew(LineEdit);
	mcp_url->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	mcp_grid->add_child(mcp_url);

	mcp_capabilities = memnew(TextEdit);
	mcp_capabilities->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	mcp_capabilities->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	mcp_capabilities->set_custom_minimum_size(Size2(0, 160) * EDSCALE);
	mcp_editor->add_child(mcp_capabilities);

	HBoxContainer *mcp_actions = memnew(HBoxContainer);
	mcp_actions->add_theme_constant_override("separation", 6 * EDSCALE);
	mcp_editor->add_child(mcp_actions);

	mcp_save_button = memnew(Button);
	mcp_save_button->connect(SceneStringName(pressed), callable_mp(this, &AIToolsPanel::_save_mcp_server));
	mcp_actions->add_child(mcp_save_button);

	mcp_delete_button = memnew(Button);
	mcp_delete_button->connect(SceneStringName(pressed), callable_mp(this, &AIToolsPanel::_delete_mcp_server));
	mcp_actions->add_child(mcp_delete_button);

	HBoxContainer *bottom = memnew(HBoxContainer);
	bottom->add_theme_constant_override("separation", 6 * EDSCALE);
	root->add_child(bottom);

	reload_button = memnew(Button);
	reload_button->connect(SceneStringName(pressed), callable_mp(this, &AIToolsPanel::_reload_registry));
	bottom->add_child(reload_button);

	status_label = memnew(Label);
	status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	status_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	bottom->add_child(status_label);

	// Skill import file dialog.
	skill_import_file_dialog = memnew(EditorFileDialog);
	skill_import_file_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	skill_import_file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
	skill_import_file_dialog->add_filter("*.md,*.json,*.txt", TTRC("Skill / MCP / Memory Files"));
	skill_import_file_dialog->connect(SNAME("file_selected"), callable_mp(this, &AIToolsPanel::_skill_import_file_selected));
	add_child(skill_import_file_dialog);

	// Skill import directory dialog.
	skill_import_dir_dialog = memnew(EditorFileDialog);
	skill_import_dir_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	skill_import_dir_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_DIR);
	skill_import_dir_dialog->connect(SNAME("dir_selected"), callable_mp(this, &AIToolsPanel::_skill_import_dir_selected));
	add_child(skill_import_dir_dialog);

	// MCP import file dialog.
	mcp_import_file_dialog = memnew(EditorFileDialog);
	mcp_import_file_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	mcp_import_file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
	mcp_import_file_dialog->add_filter("*.json,*.md", TTRC("MCP Config Files"));
	mcp_import_file_dialog->connect(SNAME("file_selected"), callable_mp(this, &AIToolsPanel::_mcp_import_file_selected));
	add_child(mcp_import_file_dialog);

	// MCP import directory dialog.
	mcp_import_dir_dialog = memnew(EditorFileDialog);
	mcp_import_dir_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	mcp_import_dir_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_DIR);
	mcp_import_dir_dialog->connect(SNAME("dir_selected"), callable_mp(this, &AIToolsPanel::_mcp_import_dir_selected));
	add_child(mcp_import_dir_dialog);

	_update_translations();
	_load_registry();
}
