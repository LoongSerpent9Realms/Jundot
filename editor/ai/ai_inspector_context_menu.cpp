/*  ai_inspector_context_menu.cpp                                         */
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
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR THE DEALINGS IN THE SOFTWARE.                   */
/**************************************************************************/

#include "ai_inspector_context_menu.h"

#include "ai_chat_panel.h"

#include "core/object/class_db.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/inspector/editor_property_name_processor.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/text_edit.h"
#include "scene/main/scene_tree.h"

void AIInspectorContextMenu::get_options(const Vector<String> &p_paths) {
	if (p_paths.is_empty()) {
		return;
	}

	// p_paths format: [object_instance_id, property_path]
	const String &property_path = p_paths[1];
	if (property_path.is_empty()) {
		return;
	}

	// Get the property name for display.
	String property_name = property_path;
	// Extract the last part of the path for a cleaner display name.
	int last_slash = property_path.rfind_char('/');
	if (last_slash >= 0) {
		property_name = property_path.substr(last_slash + 1);
	}
	// Process the name for better display.
	property_name = EditorPropertyNameProcessor::get_singleton()->process_name(property_name, EditorPropertyNameProcessor::STYLE_CAPITALIZED, property_path);

	// Store for use in callback.
	last_property_path = property_path;
	last_property_name = property_name;

	// Add the "Ask AI" menu item.
	add_context_menu_item(
			vformat(TTR("Ask AI about \"%s\""), property_name),
			Callable(this, "_ask_ai"),
			Ref<Texture2D>());
}

void AIInspectorContextMenu::_ask_ai(Object *p_property) {
	String property_path = last_property_path;
	String property_name = last_property_name;

	// Find the AI chat panel by traversing the scene tree.
	SceneTree *scene_tree = SceneTree::get_singleton();
	if (!scene_tree) {
		return;
	}

	// Find the root node and search for AI Assistant dock.
	Node *root = scene_tree->get_root();
	if (!root) {
		return;
	}

	AIChatPanel *chat_panel = nullptr;

	// Search for the AI dock by name.
	for (int i = 0; i < root->get_child_count(); i++) {
		Node *child = root->get_child(i);
		if (!child) {
			continue;
		}

		// Search recursively for a node named "AI Assistant".
		chat_panel = _find_chat_panel(child);
		if (chat_panel) {
			break;
		}
	}

	if (!chat_panel) {
		EditorNode::get_singleton()->show_warning(TTR("AI Assistant dock not found. Please ensure the AI plugin is enabled."));
		return;
	}

	// Build the query message.
	String query = vformat(TTR("I'm looking at the property \"%s\" (path: %s) in the editor settings. Can you help me understand:\n\n"
							   "1. What does this setting do?\n"
							   "2. How should I configure it?\n"
							   "3. Are there any best practices or common issues related to this setting?\n\n"
							   "Please explain in detail."),
			property_name, property_path);

	// Find the input TextEdit inside the chat panel.
	TextEdit *input_edit = _find_input_edit(chat_panel);

	if (input_edit) {
		input_edit->set_text(query);
		input_edit->grab_focus();
	} else {
		EditorNode::get_singleton()->show_warning(TTR("Could not find the AI chat input field."));
	}
}

AIChatPanel *AIInspectorContextMenu::_find_chat_panel(Node *p_node) {
	if (!p_node) {
		return nullptr;
	}

	// Check if this node is the AI dock or contains the AI chat panel.
	// The dock is named "AI Assistant" and contains a TabContainer.
	if (p_node->get_name() == SNAME("AI Assistant")) {
		// Find TabContainer inside.
		for (int i = 0; i < p_node->get_child_count(); i++) {
			TabContainer *child_tabs = Object::cast_to<TabContainer>(p_node->get_child(i));
			if (child_tabs) {
				for (int j = 0; j < child_tabs->get_child_count(); j++) {
					AIChatPanel *panel = Object::cast_to<AIChatPanel>(child_tabs->get_child(j));
					if (panel) {
						// Switch to the Chat tab.
						child_tabs->set_current_tab(j);
						return panel;
					}
				}
			}
		}
	}

	// Check if this node is directly an AIChatPanel.
	AIChatPanel *panel = Object::cast_to<AIChatPanel>(p_node);
	if (panel) {
		return panel;
	}

	// Recursively search children.
	for (int i = 0; i < p_node->get_child_count(); i++) {
		AIChatPanel *found = _find_chat_panel(p_node->get_child(i));
		if (found) {
			return found;
		}
	}

	return nullptr;
}

TextEdit *AIInspectorContextMenu::_find_input_edit(Control *p_root) {
	if (!p_root) {
		return nullptr;
	}

	// Check if this is a TextEdit.
	TextEdit *edit = Object::cast_to<TextEdit>(p_root);
	if (edit) {
		return edit;
	}

	// Recursively search children.
	for (int i = 0; i < p_root->get_child_count(); i++) {
		Control *child = Object::cast_to<Control>(p_root->get_child(i));
		if (!child) {
			continue;
		}
		TextEdit *found = _find_input_edit(child);
		if (found) {
			return found;
		}
	}

	return nullptr;
}

void AIInspectorContextMenu::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_ask_ai"), &AIInspectorContextMenu::_ask_ai);
}