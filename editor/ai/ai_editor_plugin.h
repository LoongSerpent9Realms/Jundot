/*  ai_editor_plugin.h                                                     */
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

#pragma once

#include "editor/plugins/editor_plugin.h"

class EditorDock;
class TabContainer;
class AIInspectorContextMenu;
class GitHubAuthService;
class GiteeAuthService;

class AIEditorPlugin : public EditorPlugin {
	GDCLASS(AIEditorPlugin, EditorPlugin)

	Control *main_screen_panel = nullptr;
	EditorDock *ai_dock = nullptr;
	TabContainer *tabs = nullptr;
	Ref<AIInspectorContextMenu> inspector_context_menu_plugin;
	GitHubAuthService *github_auth_service = nullptr;
	GiteeAuthService *gitee_auth_service = nullptr;

	void _create_dock();
	Control *_create_placeholder_panel(const String &p_title, const String &p_description);

public:
	virtual String get_plugin_name() const override { return "AI Assistant"; }
	virtual bool has_main_screen() const override { return true; }
	virtual void make_visible(bool p_visible) override;

	AIEditorPlugin();
	~AIEditorPlugin();
};
