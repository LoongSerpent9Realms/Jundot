/*  ai_memory_panel.h                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#pragma once

#include "ai_chat_parser.h"
#include "ai_memory_store.h"

#include "scene/gui/margin_container.h"

class Button;
class CheckBox;
class EditorFileDialog;
class ItemList;
class Label;
class LineEdit;
class TextEdit;

class AIMemoryPanel : public MarginContainer {
	GDCLASS(AIMemoryPanel, MarginContainer)

	Vector<AIMemoryEntry> entries;
	int selected_index = -1;

	ItemList *memory_list = nullptr;
	CheckBox *enabled_check = nullptr;
	LineEdit *title_edit = nullptr;
	LineEdit *tags_edit = nullptr;
	TextEdit *content_edit = nullptr;
	Button *new_button = nullptr;
	Button *delete_button = nullptr;
	Button *save_button = nullptr;
	Button *reload_button = nullptr;
	Button *import_button = nullptr;
	EditorFileDialog *import_file_dialog = nullptr;
	EditorFileDialog *import_dir_dialog = nullptr;
	Label *status_label = nullptr;

	void _load_entries();
	void _save_entries();
	void _refresh_list();
	void _select_memory(int p_index);
	void _new_memory();
	void _delete_memory();
	void _save_memory();
	void _reload_memory();
	void _update_editor();
	void _update_selected_from_editor();
	void _set_editor_enabled(bool p_enabled);
	Vector<String> _tags_from_text(const String &p_text) const;
	String _tags_to_text(const Vector<String> &p_tags) const;
	void _update_translations();

	// Import callbacks.
	void _import_button_pressed();
	void _import_file_selected(const String &p_path);
	void _import_dir_selected(const String &p_dir);
	void _apply_import_suggestions(const Vector<AISuggestion> &p_suggestions);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	AIMemoryPanel();
};
