/*  ai_tools_panel.h                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#pragma once

#include "ai_chat_parser.h"
#include "ai_tool_registry.h"

#include "scene/gui/margin_container.h"

class Button;
class CheckBox;
class EditorFileDialog;
class ItemList;
class Label;
class LineEdit;
class OptionButton;
class SpinBox;
class TextEdit;

class AIToolsPanel : public MarginContainer {
	GDCLASS(AIToolsPanel, MarginContainer)

	Vector<AISkillEntry> skills;
	Vector<AIMCPServerEntry> mcp_servers;
	int selected_skill = -1;
	int selected_mcp_server = -1;

	ItemList *skill_list = nullptr;
	CheckBox *skill_enabled = nullptr;
	CheckBox *skill_writes = nullptr;
	CheckBox *skill_requires_confirmation = nullptr;
	CheckBox *skill_read_only_allowed = nullptr;
	LineEdit *skill_name = nullptr;
	LineEdit *skill_description = nullptr;
	LineEdit *skill_permission = nullptr;
	TextEdit *skill_prompt = nullptr;
	Button *skill_new_button = nullptr;
	Button *skill_delete_button = nullptr;
	Button *skill_save_button = nullptr;
	Button *skill_import_button = nullptr;

	ItemList *mcp_list = nullptr;
	CheckBox *mcp_enabled = nullptr;
	CheckBox *mcp_writes = nullptr;
	CheckBox *mcp_requires_confirmation = nullptr;
	CheckBox *mcp_read_only_allowed = nullptr;
	LineEdit *mcp_name = nullptr;
	LineEdit *mcp_command = nullptr;
	LineEdit *mcp_arguments = nullptr;
	LineEdit *mcp_url = nullptr;
	TextEdit *mcp_capabilities = nullptr;
	Button *mcp_new_button = nullptr;
	Button *mcp_delete_button = nullptr;
	Button *mcp_save_button = nullptr;
	Button *mcp_import_button = nullptr;
	Label *mcp_status_label = nullptr;
	Button *mcp_test_button = nullptr;
	Button *mcp_refresh_button = nullptr;
	Button *mcp_stop_button = nullptr;
	OptionButton *mcp_lifecycle = nullptr;
	SpinBox *mcp_timeout = nullptr;

	Button *reload_button = nullptr;
	Label *status_label = nullptr;

	EditorFileDialog *skill_import_file_dialog = nullptr;
	EditorFileDialog *skill_import_dir_dialog = nullptr;
	EditorFileDialog *mcp_import_file_dialog = nullptr;
	EditorFileDialog *mcp_import_dir_dialog = nullptr;

	void _load_registry();
	void _save_registry();
	void _refresh_skill_list();
	void _refresh_mcp_list();
	void _select_skill(int p_index);
	void _select_mcp_server(int p_index);
	void _new_skill();
	void _delete_skill();
	void _save_skill();
	void _new_mcp_server();
	void _delete_mcp_server();
	void _save_mcp_server();
	void _reload_registry();
	void _update_skill_editor();
	void _update_mcp_editor();
	void _update_selected_skill_from_editor();
	void _update_selected_mcp_from_editor();
	void _set_skill_editor_enabled(bool p_enabled);
	void _set_mcp_editor_enabled(bool p_enabled);
	void _update_translations();

	// Import callbacks.
	void _skill_import_pressed();
	void _skill_import_file_selected(const String &p_path);
	void _skill_import_dir_selected(const String &p_dir);
	void _mcp_import_pressed();
	void _mcp_import_file_selected(const String &p_path);
	void _mcp_import_dir_selected(const String &p_dir);
	void _mcp_test_connection();
	void _mcp_refresh_tools();
	void _mcp_stop_server();
	void _update_mcp_status();
	void _update_mcp_status_from_runtime();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	AIToolsPanel();
};
