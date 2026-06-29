/*  ai_editor_plugin.cpp                                                   */
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

#include "ai_editor_plugin.h"

#include "ai_chat_panel.h"
#include "ai_config_panel.h"
#include "ai_memory_panel.h"
#include "ai_tools_panel.h"
#include "ai_mcp_manager.h"
#include "ai_inspector_context_menu.h"
#include "ai_cline_integration.h"
#include "github_auth_service.h"
#include "gitee_auth_service.h"

#include "editor/docks/editor_dock.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/inspector/editor_context_menu_plugin.h"
#include "editor/settings/editor_command_palette.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/label.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/box_container.h"

Control *AIEditorPlugin::_create_placeholder_panel(const String &p_title, const String &p_description) {
	MarginContainer *panel = memnew(MarginContainer);
	panel->set_name(p_title);
	panel->add_theme_constant_override("margin_left", 8 * EDSCALE);
	panel->add_theme_constant_override("margin_top", 8 * EDSCALE);
	panel->add_theme_constant_override("margin_right", 8 * EDSCALE);
	panel->add_theme_constant_override("margin_bottom", 8 * EDSCALE);

	VBoxContainer *content = memnew(VBoxContainer);
	content->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	content->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	panel->add_child(content);

	Label *title = memnew(Label);
	title->set_text(p_title);
	title->set_theme_type_variation("HeaderSmall");
	content->add_child(title);

	Label *description = memnew(Label);
	description->set_text(p_description);
	description->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	description->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	content->add_child(description);

	return panel;
}

void AIEditorPlugin::_create_dock() {
	main_screen_panel = memnew(MarginContainer);
	main_screen_panel->set_name(TTRC("AI Assistant"));
	main_screen_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_screen_panel->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	main_screen_panel->add_theme_constant_override("margin_left", 8 * EDSCALE);
	main_screen_panel->add_theme_constant_override("margin_top", 8 * EDSCALE);
	main_screen_panel->add_theme_constant_override("margin_right", 8 * EDSCALE);
	main_screen_panel->add_theme_constant_override("margin_bottom", 8 * EDSCALE);
	EditorNode::get_singleton()->get_editor_main_screen()->get_control()->add_child(main_screen_panel);
	main_screen_panel->hide();

	ai_dock = memnew(EditorDock);
	ai_dock->set_visible(false);
	ai_dock->set_title(TTRC("AI Assistant"));
	ai_dock->set_name(TTRC("AI Assistant"));
	ai_dock->set_layout_key("AIAssistant");
	ai_dock->set_icon_name("Script");
	ai_dock->set_dock_shortcut(ED_SHORTCUT_AND_COMMAND("docks/open_ai_assistant", TTRC("Open AI Assistant Dock")));
	ai_dock->set_default_slot(EditorDock::DOCK_SLOT_RIGHT_BR);
	ai_dock->set_available_layouts(EditorDock::DOCK_LAYOUT_VERTICAL | EditorDock::DOCK_LAYOUT_FLOATING);
	ai_dock->set_custom_minimum_size(Size2(360, 420) * EDSCALE);

	tabs = memnew(TabContainer);
	tabs->set_theme_type_variation("TabContainerInner");
	tabs->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	tabs->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	ai_dock->add_child(tabs);

	tabs->add_child(memnew(AIChatPanel));
	tabs->add_child(memnew(AIConfigPanel));
	tabs->add_child(memnew(AIMemoryPanel));
	tabs->add_child(memnew(AIToolsPanel));

	EditorDockManager::get_singleton()->add_dock(ai_dock);
}

void AIEditorPlugin::make_visible(bool p_visible) {
	if (!main_screen_panel || !tabs || !ai_dock) {
		return;
	}

	if (p_visible) {
		if (tabs->get_parent() != main_screen_panel) {
			tabs->reparent(main_screen_panel);
		}
		main_screen_panel->show();
	} else {
		main_screen_panel->hide();
		if (tabs->get_parent() != ai_dock) {
			tabs->reparent(ai_dock);
		}
	}
}

AIEditorPlugin::AIEditorPlugin() {
	_create_dock();
	AIMCPManager::get_singleton()->initialize();

	github_auth_service = memnew(GitHubAuthService);
	gitee_auth_service = memnew(GiteeAuthService);
	cline_integration.instantiate();

	// Register the inspector context menu plugin for "Ask AI" feature.
	inspector_context_menu_plugin.instantiate();
	EditorContextMenuPluginManager::get_singleton()->add_plugin(
			EditorContextMenuPlugin::CONTEXT_SLOT_INSPECTOR_PROPERTY,
			inspector_context_menu_plugin);
}

AIEditorPlugin::~AIEditorPlugin() {
	AIConfigPanel::stop_managed_mimocode();

	if (main_screen_panel) {
		main_screen_panel->queue_free();
		main_screen_panel = nullptr;
	}
	if (ai_dock) {
		EditorDockManager::get_singleton()->remove_dock(ai_dock);
		ai_dock->queue_free();
	}

	// Unregister the inspector context menu plugin.
	if (inspector_context_menu_plugin.is_valid()) {
		EditorContextMenuPluginManager::get_singleton()->remove_plugin(inspector_context_menu_plugin);
	}

	if (github_auth_service) {
		memdelete(github_auth_service);
		github_auth_service = nullptr;
	}
	if (gitee_auth_service) {
		memdelete(gitee_auth_service);
		gitee_auth_service = nullptr;
	}

	cline_integration.unref();

	AIClineIntegration::cleanup();
	AIMCPManager::cleanup();
}
