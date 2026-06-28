/**************************************************************************/
/*  project_manager.cpp                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             JUNDOT ENGINE                               */
/*                        https://jundotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
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
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "project_manager.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/os/keyboard.h"
#include "core/os/os.h"
#include "core/version.h"
#include "editor/ai/ai_chat_service.h"
#include "editor/ai/ai_config_panel.h"
#include "editor/ai/ai_memory_store.h"
#include "editor/ai/ai_settings.h"
#include "editor/ai/ai_usage_agreement_dialog.h"
#include "editor/asset_library/asset_library_editor_plugin.h"
#include "editor/doc/editor_help.h"
#include "editor/editor_string_names.h"
#include "editor/gui/editor_about.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/gui/editor_title_bar.h"
#include "editor/gui/editor_version_button.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/project_manager/ai_source_manager.h"
#include "editor/project_manager/engine_update_label.h"
#include "editor/project_manager/project_dialog.h"
#include "editor/project_manager/project_list.h"
#include "editor/project_manager/project_tag.h"
#include "editor/project_manager/quick_settings_dialog.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "editor/themes/editor_theme_manager.h"
#include "editor/update/update_dialog.h"
#include "editor/update/update_manager.h"
#include "main/main.h"
#include "scene/gui/check_box.h"
#include "scene/gui/flow_container.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/menu_bar.h"
#include "scene/gui/option_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/separator.h"
#include "scene/main/http_request.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/theme/theme_db.h"
#include "servers/display/display_server.h"
#include "servers/navigation_3d/navigation_server_3d.h"

#ifndef PHYSICS_2D_DISABLED
#include "servers/physics_2d/physics_server_2d.h"
#endif // PHYSICS_2D_DISABLED

#ifndef PHYSICS_3D_DISABLED
#include "servers/physics_3d/physics_server_3d.h"
#endif // PHYSICS_3D_DISABLED

#include "modules/modules_enabled.gen.h" // For gdscript, mono. (For editor help highlighter).

constexpr int JUNDOT4_CONFIG_VERSION = 5;

ProjectManager *ProjectManager::singleton = nullptr;

static String _get_default_ai_source_cache_root() {
	if (!OS::get_singleton()) {
		return String();
	}
	const String user_data_dir = OS::get_singleton()->get_user_data_dir();
	if (user_data_dir.is_empty()) {
		return String();
	}
	return user_data_dir.path_join("engine_source");
}

static bool _is_ai_engine_source_root(const String &p_path) {
	return !p_path.is_empty() && FileAccess::exists(p_path.path_join("SConstruct"));
}

static String _find_ai_engine_source_root_in_cache(const String &p_cache_path) {
	if (_is_ai_engine_source_root(p_cache_path)) {
		return p_cache_path;
	}

	Ref<DirAccess> dir = DirAccess::open(p_cache_path);
	if (dir.is_null()) {
		return String();
	}

	dir->list_dir_begin();
	String name = dir->get_next();
	while (!name.is_empty()) {
		if (dir->current_is_dir() && !name.begins_with(".")) {
			const String candidate = p_cache_path.path_join(name);
			if (_is_ai_engine_source_root(candidate)) {
				dir->list_dir_end();
				return candidate;
			}
		}
		name = dir->get_next();
	}
	dir->list_dir_end();
	return String();
}

// Notifications.

void ProjectManager::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			Engine::get_singleton()->set_editor_hint(false);

			Window *main_window = get_window();
			if (main_window) {
				// Handle macOS fullscreen and extend-to-title changes.
				main_window->connect("titlebar_changed", callable_mp(this, &ProjectManager::_titlebar_resized));
			}

			// Theme has already been created in the constructor, so we can skip that step.
			_update_theme(true);
		} break;

		case NOTIFICATION_READY: {
			DisplayServer::get_singleton()->screen_set_keep_on(EDITOR_GET("interface/editor/display/keep_screen_on"));
			const int default_sorting = (int)EDITOR_GET("project_manager/sorting_order");
			filter_option->select(default_sorting);
			project_list->set_order_option(default_sorting, false);

			_select_main_view(MAIN_VIEW_PROJECTS);
			_update_list_placeholder();
			_titlebar_resized();
			callable_mp(this, &ProjectManager::_maybe_prompt_ai_source_cache).call_deferred();
		} break;

		case NOTIFICATION_TRANSLATION_CHANGED: {
			// TRANSLATORS: This refers to the application where users manage their Jundot projects.
			SceneTree::get_singleton()->get_root()->set_title(JUNDOT_VERSION_NAME + String(" - ") + TTR("Project Manager", "Application"));

			const String line1 = TTR("You don't have any projects yet.");
			const String line2 = TTR("Get started by creating a new one,\nimporting one that exists, or by downloading a project template from the Asset Store!");
			empty_list_message->set_text(vformat("[center][b]%s[/b] %s[/center]", line1, line2));

			_titlebar_resized();
		} break;

		case NOTIFICATION_VISIBILITY_CHANGED: {
			set_process_shortcut_input(is_visible_in_tree());
		} break;

		case NOTIFICATION_WM_CLOSE_REQUEST: {
			_dim_window();
		} break;

		case NOTIFICATION_WM_ABOUT: {
			_show_about();
		} break;

		case EditorSettings::NOTIFICATION_EDITOR_SETTINGS_CHANGED: {
			if (EditorThemeManager::is_generated_theme_outdated()) {
				_update_theme();
			}
			_update_list_placeholder();
		} break;
	}
}

// Utility data.

Ref<Texture2D> ProjectManager::_file_dialog_get_icon(const String &p_path) {
	if (p_path.has_extension("jundot")) {
		return singleton->icon_type_cache["JundotMonochrome"];
	}

	return singleton->icon_type_cache["Object"];
}

Ref<Texture2D> ProjectManager::_file_dialog_get_thumbnail(const String &p_path) {
	if (p_path.has_extension("jundot")) {
		return singleton->icon_type_cache["JundotFile"];
	}

	return Ref<Texture2D>();
}

void ProjectManager::_build_icon_type_cache(Ref<Theme> p_theme) {
	if (p_theme.is_null()) {
		return;
	}
	List<StringName> tl;
	p_theme->get_icon_list(EditorStringName(EditorIcons), &tl);
	for (const StringName &name : tl) {
		icon_type_cache[name] = p_theme->get_icon(name, EditorStringName(EditorIcons));
	}
}

// Main layout.

void ProjectManager::_update_size_limits() {
	const Size2 minimum_size = Size2(720, 450) * EDSCALE;

	// Define a minimum window size to prevent UI elements from overlapping or being cut off.
	Window *w = Object::cast_to<Window>(SceneTree::get_singleton()->get_root());
	if (w) {
		// Calling Window methods this early doesn't sync properties with DS.
		w->set_min_size(minimum_size);
		DisplayServer::get_singleton()->window_set_min_size(minimum_size);
	}
	Size2 real_size = DisplayServer::get_singleton()->window_get_size();

	Rect2i screen_rect = DisplayServer::get_singleton()->screen_get_usable_rect(DisplayServer::get_singleton()->window_get_current_screen());
	if (screen_rect.size != Vector2i()) {
		// Center the window on the screen.
		Vector2i window_position;
		window_position.x = screen_rect.position.x + (screen_rect.size.x - real_size.x) / 2;
		window_position.y = screen_rect.position.y + (screen_rect.size.y - real_size.y) / 2;

		// Limit popup menus to prevent unusably long lists.
		// We try to set it to half the screen resolution, but no smaller than the minimum window size.
		Size2 half_screen_rect = (screen_rect.size * EDSCALE) / 2;
		Size2 maximum_popup_size = half_screen_rect.max(minimum_size);
		quick_settings_dialog->update_size_limits(maximum_popup_size);
	}
}

void ProjectManager::_update_theme(bool p_skip_creation) {
	if (!p_skip_creation) {
		theme = EditorThemeManager::generate_theme(theme);
		DisplayServer::set_early_window_clear_color_override(true, theme->get_color("background", EditorStringName(Editor)));
	}

	Vector<Ref<Theme>> editor_themes;
	editor_themes.push_back(theme);
	editor_themes.push_back(ThemeDB::get_singleton()->get_default_theme());

	ThemeContext *node_tc = ThemeDB::get_singleton()->get_theme_context(this);
	if (node_tc) {
		node_tc->set_themes(editor_themes);
	} else {
		ThemeDB::get_singleton()->create_theme_context(this, editor_themes);
	}

	Window *owner_window = get_window();
	if (owner_window) {
		ThemeContext *window_tc = ThemeDB::get_singleton()->get_theme_context(owner_window);
		if (window_tc) {
			window_tc->set_themes(editor_themes);
		} else {
			ThemeDB::get_singleton()->create_theme_context(owner_window, editor_themes);
		}
	}

	// Update styles.
	{
		const int top_bar_separation = get_theme_constant("top_bar_separation", EditorStringName(Editor));
		root_container->add_theme_constant_override("margin_left", top_bar_separation);
		root_container->add_theme_constant_override("margin_top", top_bar_separation);
		root_container->add_theme_constant_override("margin_bottom", top_bar_separation);
		root_container->add_theme_constant_override("margin_right", top_bar_separation);
		main_vbox->add_theme_constant_override("separation", top_bar_separation);

		background_panel->add_theme_style_override(SceneStringName(panel), get_theme_stylebox("Background", EditorStringName(EditorStyles)));
		main_view_container->add_theme_style_override(SceneStringName(panel), get_theme_stylebox("panel_container", "ProjectManager"));

		title_bar_logo->set_button_icon(get_editor_theme_icon("TitleBarLogo"));

		_set_main_view_icon(MAIN_VIEW_PROJECTS, get_editor_theme_icon("ProjectList"));
		_set_main_view_icon(MAIN_VIEW_ASSETLIB, get_editor_theme_icon("AssetStore"));

		// Project list.
		{
			loading_label->add_theme_font_override(SceneStringName(font), get_theme_font("bold", EditorStringName(EditorFonts)));
			project_list_panel->add_theme_style_override(SceneStringName(panel), get_theme_stylebox("project_list", "ProjectManager"));

			empty_list_create_project->set_button_icon(get_editor_theme_icon("Add"));
			empty_list_import_project->set_button_icon(get_editor_theme_icon("Load"));
			empty_list_open_assetlib->set_button_icon(get_editor_theme_icon("AssetStore"));

			empty_list_online_warning->add_theme_font_override(SceneStringName(font), get_theme_font("italic", EditorStringName(EditorFonts)));
			empty_list_online_warning->add_theme_color_override(SceneStringName(font_color), get_theme_color("font_placeholder_color", EditorStringName(Editor)));

			// Top bar.
			search_box->set_right_icon(get_editor_theme_icon("Search"));
			quick_settings_button->set_button_icon(get_editor_theme_icon("Tools"));

			// Sidebar.
			create_btn->set_button_icon(get_editor_theme_icon("Add"));
			import_btn->set_button_icon(get_editor_theme_icon("Load"));
			scan_btn->set_button_icon(get_editor_theme_icon("Search"));
			open_btn->set_button_icon(get_editor_theme_icon("Edit"));
			open_options_btn->set_button_icon(get_editor_theme_icon("Collapse"));
			run_btn->set_button_icon(get_editor_theme_icon("Play"));
			rename_btn->set_button_icon(get_editor_theme_icon("Rename"));
			duplicate_btn->set_button_icon(get_editor_theme_icon("Duplicate"));
			manage_tags_btn->set_button_icon(get_editor_theme_icon("Script"));
			erase_btn->set_button_icon(get_editor_theme_icon("Remove"));
			erase_missing_btn->set_button_icon(get_editor_theme_icon("Clear"));
			create_tag_btn->set_button_icon(get_editor_theme_icon("Add"));
			donate_btn->set_button_icon(get_editor_theme_icon("Heart"));

			tag_error->add_theme_color_override(SceneStringName(font_color), get_theme_color("error_color", EditorStringName(Editor)));
			tag_edit_error->add_theme_color_override(SceneStringName(font_color), get_theme_color("error_color", EditorStringName(Editor)));

			const int h_separation = get_theme_constant("sidebar_button_icon_separation", "ProjectManager");
			create_btn->add_theme_constant_override("h_separation", h_separation);
			import_btn->add_theme_constant_override("h_separation", h_separation);
			scan_btn->add_theme_constant_override("h_separation", h_separation);
			open_btn->add_theme_constant_override("h_separation", h_separation);
			run_btn->add_theme_constant_override("h_separation", h_separation);
			rename_btn->add_theme_constant_override("h_separation", h_separation);
			duplicate_btn->add_theme_constant_override("h_separation", h_separation);
			manage_tags_btn->add_theme_constant_override("h_separation", h_separation);
			erase_btn->add_theme_constant_override("h_separation", h_separation);
			erase_missing_btn->add_theme_constant_override("h_separation", h_separation);

			open_btn_container->add_theme_constant_override("separation", 0);
			open_options_popup->set_item_icon(0, get_editor_theme_icon("Notification"));
			open_options_popup->set_item_icon(1, get_editor_theme_icon("NodeWarning"));
		}

		// Dialogs.
		migration_guide_button->set_button_icon(get_editor_theme_icon("ExternalLink"));

		// Asset store popup.
		if (asset_library && EDITOR_GET("interface/theme/style") == "Classic") {
			// Removes extra border margins.
			asset_library->add_theme_style_override(SceneStringName(panel), memnew(StyleBoxEmpty));
		}
	}
#ifdef ANDROID_ENABLED
	DisplayServer::get_singleton()->window_set_color(theme->get_color("background", EditorStringName(Editor)));
#endif
}

Button *ProjectManager::_add_main_view(MainViewTab p_id, const String &p_name, const Ref<Texture2D> &p_icon, Control *p_view_control) {
	ERR_FAIL_INDEX_V(p_id, MAIN_VIEW_MAX, nullptr);
	ERR_FAIL_COND_V(main_view_map.has(p_id), nullptr);
	ERR_FAIL_COND_V(main_view_toggle_map.has(p_id), nullptr);

	Button *toggle_button = memnew(Button);
	toggle_button->set_flat(true);
	toggle_button->set_theme_type_variation("MainScreenButton");
	toggle_button->set_toggle_mode(true);
	toggle_button->set_button_group(main_view_toggles_group);
	toggle_button->set_text(p_name);
	toggle_button->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_select_main_view).bind((int)p_id));

	main_view_toggles->add_child(toggle_button);
	main_view_toggle_map[p_id] = toggle_button;

	_set_main_view_icon(p_id, p_icon);

	p_view_control->set_visible(false);
	main_view_container->add_child(p_view_control);
	main_view_map[p_id] = p_view_control;

	return toggle_button;
}

void ProjectManager::_set_main_view_icon(MainViewTab p_id, const Ref<Texture2D> &p_icon) {
	ERR_FAIL_INDEX(p_id, MAIN_VIEW_MAX);
	ERR_FAIL_COND(!main_view_toggle_map.has(p_id));

	Button *toggle_button = main_view_toggle_map[p_id];

	Ref<Texture2D> old_icon = toggle_button->get_button_icon();
	if (old_icon.is_valid()) {
		old_icon->disconnect_changed(callable_mp((Control *)toggle_button, &Control::update_minimum_size));
	}

	if (p_icon.is_valid()) {
		toggle_button->set_button_icon(p_icon);
		// Make sure the control is updated if the icon is reimported.
		p_icon->connect_changed(callable_mp((Control *)toggle_button, &Control::update_minimum_size));
	} else {
		toggle_button->set_button_icon(Ref<Texture2D>());
	}
}

void ProjectManager::_select_main_view(int p_id) {
	MainViewTab view_id = (MainViewTab)p_id;

	ERR_FAIL_INDEX(view_id, MAIN_VIEW_MAX);
	ERR_FAIL_COND(!main_view_map.has(view_id));
	ERR_FAIL_COND(!main_view_toggle_map.has(view_id));

	if (current_main_view != view_id) {
		main_view_toggle_map[current_main_view]->set_pressed_no_signal(false);
		main_view_map[current_main_view]->set_visible(false);
		current_main_view = view_id;
	}
	main_view_toggle_map[current_main_view]->set_pressed_no_signal(true);
	main_view_map[current_main_view]->set_visible(true);

#ifndef ANDROID_ENABLED
	if (current_main_view == MAIN_VIEW_PROJECTS && search_box->is_inside_tree()) {
		// Automatically grab focus when the user moves from the Templates tab
		// back to the Projects tab.
		// Needs to be deferred, otherwise the focus outline is always drawn.
		callable_mp((Control *)search_box, &Control::grab_focus).call_deferred(true);
	}

	// The Templates tab's search field is focused on display in the asset
	// library editor plugin code.
#endif
}

void ProjectManager::_show_about() {
	about_dialog->popup_centered(Size2(780, 500) * EDSCALE);
}

void ProjectManager::_open_asset_library_confirmed() {
	const int network_mode = EDITOR_GET("network/connection/network_mode");
	if (network_mode == EditorSettings::NETWORK_OFFLINE) {
		EditorSettings::get_singleton()->set_setting("network/connection/network_mode", EditorSettings::NETWORK_ONLINE);
		EditorSettings::get_singleton()->notify_changes();
		EditorSettings::get_singleton()->save();
	}

	_select_main_view(MAIN_VIEW_ASSETLIB);
}

void ProjectManager::_project_list_menu_option(int p_option) {
	switch (p_option) {
		case ProjectList::MENU_EDIT:
			_open_selected_projects();
			break;

		case ProjectList::MENU_EDIT_VERBOSE:
			open_in_verbose_mode = true;
			_open_selected_projects_check_warnings();
			break;

		case ProjectList::MENU_EDIT_RECOVERY:
			_open_recovery_mode_ask(true);
			break;

		case ProjectList::MENU_RUN:
			_run_project_confirm();
			break;

		case ProjectList::MENU_SHOW_IN_FILE_MANAGER:
			_show_project_in_file_manager();
			break;

		case ProjectList::MENU_COPY_PATH: {
			const Vector<ProjectList::Item> &selected_list = project_list->get_selected_projects();
			if (selected_list.is_empty()) {
				return;
			}
			DisplayServer::get_singleton()->clipboard_set(selected_list[0].path);
		} break;

		case ProjectList::MENU_RENAME:
			_rename_project();
			break;

		case ProjectList::MENU_MANAGE_TAGS:
			_manage_project_tags();
			break;

		case ProjectList::MENU_DUPLICATE:
			_duplicate_project();
			break;

		case ProjectList::MENU_REMOVE:
			_erase_project();
			break;
	}
}

void ProjectManager::_show_error(const String &p_message, const Size2 &p_min_size) {
	error_dialog->set_text(p_message);
	error_dialog->popup_centered(p_min_size);
}

void ProjectManager::_dim_window() {
	// This method must be called before calling `get_tree()->quit()`.
	// Otherwise, its effect won't be visible

	// Dim the project manager window while it's quitting to make it clearer that it's busy.
	// No transition is applied, as the effect needs to be visible immediately
	float c = 0.5f;
	Color dim_color = Color(c, c, c);
	set_modulate(dim_color);
}

// Quick settings.

void ProjectManager::_show_quick_settings() {
	quick_settings_dialog->popup_centered(Size2(640, 200) * EDSCALE);
}

void ProjectManager::_restart_confirmed() {
	List<String> args = OS::get_singleton()->get_cmdline_args();
	Error err = OS::get_singleton()->create_instance(args);
	ERR_FAIL_COND(err);

	_dim_window();
	get_tree()->quit();
}

// Hot-update system handlers.

void ProjectManager::_on_update_download_requested(const String &p_version, const String &p_url) {
	if (!update_dialog || !update_manager) {
		return;
	}

	UpdateManifest info;
	if (engine_update_label && engine_update_label->get_available_manifest().get_version_string() == p_version) {
		info = engine_update_label->get_available_manifest();
	} else {
		// Graceful fallback for callers that only know the version and URL.
		info.version.full = p_version;
		info.download_url = p_url;
	}

	update_dialog->set_update_info(info);
	update_dialog->popup_centered();
}

void ProjectManager::_on_update_now_requested() {
	if (!update_manager) {
		return;
	}

	const int pid = update_manager->trigger_launcher_update();
	if (pid < 0) {
		update_dialog->set_update_finished(false, TTR("Could not start the Jundot updater. Make sure JundotLauncher.exe is installed next to the engine."));
	}
}

void ProjectManager::_on_update_launcher_started() {
	if (update_dialog) {
		update_dialog->set_update_started();
	}
}

void ProjectManager::_on_update_launcher_finished(int p_exit_code) {
	if (!update_dialog) {
		return;
	}
	if (p_exit_code == 0) {
		update_dialog->set_update_finished(true, TTR("Update completed. Restart Jundot to use the new version."));
	} else {
		update_dialog->set_update_finished(false, vformat(TTR("The updater stopped with exit code %d. Check the updater window for details."), p_exit_code));
	}
}

void ProjectManager::_on_skip_version_requested() {
	// Save skipped version so EngineUpdateLabel doesn't nag again.
	// For now, just dismiss. Persistence can be added via UpdateStateStore.
}

void ProjectManager::_maybe_prompt_ai_source_cache() {
	if (ai_source_prompt_shown) {
		return;
	}
	ai_source_prompt_shown = true;

	AISettingsData settings = AISettings::load();
	String cache_path = settings.engine_source_cache_root.strip_edges();
	if (cache_path.is_empty()) {
		cache_path = _get_default_ai_source_cache_root();
	}

	const String cached_source_root = _find_ai_engine_source_root_in_cache(cache_path);
	if (!cached_source_root.is_empty()) {
		settings.engine_source_root = cached_source_root;
		settings.engine_source_cache_root = cache_path;
		settings.engine_source_repository_url = JUNDOT_ENGINE_SOURCE_REPOSITORY_URL;
		settings.encrypt_engine_source_cache = true;
		AISettings::save(settings);
		return;
	}

	ai_source_prompt_dialog->popup_centered(Size2(520, 150) * EDSCALE);
}

void ProjectManager::_open_ai_source_manager_from_prompt() {
	_show_ai_source_manager();
}

void ProjectManager::_show_ai_source_manager() {
	if (ai_source_manager_dialog) {
		ai_source_manager_dialog->popup_centered_on_parent(get_window());
	}
}

// Project list.

void ProjectManager::_update_list_placeholder() {
	if (project_list->get_project_count() > 0) {
		empty_list_placeholder->hide();
		return;
	}

	empty_list_open_assetlib->set_visible(asset_library);

	const int network_mode = EDITOR_GET("network/connection/network_mode");
	if (network_mode == EditorSettings::NETWORK_OFFLINE) {
		empty_list_open_assetlib->set_text(TTRC("Go Online and Open Asset Store"));
		empty_list_online_warning->set_visible(true);
	} else {
		empty_list_open_assetlib->set_text(TTRC("Open Asset Store"));
		empty_list_online_warning->set_visible(false);
	}

	empty_list_placeholder->show();
}

void ProjectManager::_scan_projects() {
	scan_dir->popup_file_dialog();
}

void ProjectManager::_run_project() {
	const HashSet<String> &selected_list = project_list->get_selected_project_keys();

	if (selected_list.size() < 1) {
		return;
	}

	if (selected_list.size() > 1) {
		multi_run_ask->set_text(vformat(TTR("Are you sure to run %d projects at once?"), selected_list.size()));
		multi_run_ask->popup_centered();
	} else {
		_run_project_confirm();
	}
}

void ProjectManager::_run_project_confirm() {
	Vector<ProjectList::Item> selected_list = project_list->get_selected_projects();

	for (int i = 0; i < selected_list.size(); ++i) {
		const String &selected_main = selected_list[i].main_scene;
		if (selected_main.is_empty()) {
			_show_error(TTRC("Can't run project: Project has no main scene defined.\nPlease edit the project and set the main scene in the Project Settings under the \"Application\" category."));
			continue;
		}

		const String &path = selected_list[i].path;

		// `.substr(6)` on `ProjectSettings::get_singleton()->get_imported_files_path()` strips away the leading "res://".
		if (!DirAccess::exists(path.path_join(ProjectSettings::get_singleton()->get_imported_files_path().substr(6)))) {
			_show_error(TTRC("Can't run project: Assets need to be imported first.\nPlease edit the project to trigger the initial import."));
			continue;
		}

		print_line("Running project: " + path);

		List<String> args;

		for (const String &a : Main::get_forwardable_cli_arguments(Main::CLI_SCOPE_PROJECT)) {
			args.push_back(a);
		}

		args.push_back("--path");
		args.push_back(path);

		Error err = OS::get_singleton()->create_instance(args);
		ERR_FAIL_COND(err);
	}
}

void ProjectManager::_open_selected_projects() {
	// Show loading text to tell the user that the project manager is busy loading.
	// This is especially important for the Web project manager.
	loading_label->show();

	const HashSet<String> &selected_list = project_list->get_selected_project_keys();
	for (const String &path : selected_list) {
		String conf = path.path_join("project.jundot");

		if (!FileAccess::exists(conf)) {
			loading_label->hide();
			_show_error(vformat(TTR("Can't open project at '%s'.\nProject file doesn't exist or is inaccessible."), path));
			return;
		}

		print_line("Editing project: " + path);

		List<String> args;

		for (const String &a : Main::get_forwardable_cli_arguments(Main::CLI_SCOPE_TOOL)) {
			args.push_back(a);
		}

		args.push_back("--path");
		args.push_back(path);

		args.push_back("--editor");

		if (open_in_recovery_mode) {
			args.push_back("--recovery-mode");
		}

		if (open_in_verbose_mode) {
			args.push_back("--verbose");
		}

		Error err = OS::get_singleton()->create_instance(args);
		if (err != OK) {
			loading_label->hide();
			_show_error(vformat(TTR("Can't open project at '%s'.\nFailed to start the editor."), path));
			ERR_PRINT(vformat("Failed to start an editor instance for the project at '%s', error code %d.", path, err));
			return;
		}
	}

	project_list->project_opening_initiated = true;

	_dim_window();
	get_tree()->quit();
}

void ProjectManager::_open_selected_projects_check_warnings() {
	const HashSet<String> &selected_list = project_list->get_selected_project_keys();
	if (selected_list.size() < 1) {
		return;
	}

	const Size2i popup_min_size = Size2i(400.0 * EDSCALE, 0);

	if (selected_list.size() > 1) {
		multi_open_ask->set_text(vformat(TTR("You requested to open %d projects in parallel. Do you confirm?\nNote that usual checks for engine version compatibility will be bypassed."), selected_list.size()));
		multi_open_ask->popup_centered(popup_min_size);
		return;
	}

	ProjectList::Item project = project_list->get_selected_projects()[0];
	if (project.missing) {
		return;
	}

	// Update the project settings or don't open.
	const int config_version = project.version;
	PackedStringArray unsupported_features = project.unsupported_features;

	ask_update_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_LEFT); // Reset in case of previous center align.
	ask_update_backup->set_pressed(false);
	full_convert_button->hide();
	migration_guide_button->hide();
	ask_update_backup->hide();

	ask_update_settings->get_ok_button()->set_text("OK");

	// Check if the config_version property was empty or 0.
	if (config_version == 0) {
		ask_update_label->set_text(vformat(TTR("The selected project \"%s\" does not specify its supported Jundot version in its configuration file (\"project.jundot\").\n\nProject path: %s\n\nIf you proceed with opening it, it will be converted to Jundot's current configuration file format.\n\nWarning: You won't be able to open the project with previous versions of the engine anymore."), project.project_name, project.path));
		ask_update_settings->popup_centered(popup_min_size);
		return;
	}
	// Check if we need to convert project settings from an earlier engine version.
	if (config_version < ProjectSettings::CONFIG_VERSION) {
		if (config_version == JUNDOT4_CONFIG_VERSION - 1 && ProjectSettings::CONFIG_VERSION == JUNDOT4_CONFIG_VERSION) { // Conversion from Jundot 3 to 4.
			full_convert_button->show();
			ask_update_label->set_text(vformat(TTR("The selected project \"%s\" was generated by Jundot 3.x, and needs to be converted for Jundot 4.x.\n\nProject path: %s\n\nYou have three options:\n- Convert only the configuration file (\"project.jundot\"). Use this to open the project without attempting to convert its scenes, resources and scripts.\n- Convert the entire project including its scenes, resources and scripts (recommended if you are upgrading).\n- Do nothing and go back.\n\nWarning: If you select a conversion option, you won't be able to open the project with previous versions of the engine anymore."), project.project_name, project.path));
			ask_update_settings->get_ok_button()->set_text(TTRC("Convert project.jundot Only"));
		} else {
			ask_update_label->set_text(vformat(TTR("The selected project \"%s\" was generated by an older engine version, and needs to be converted for this version.\n\nProject path: %s\n\nDo you want to convert it?\n\nWarning: You won't be able to open the project with previous versions of the engine anymore."), project.project_name, project.path));
			ask_update_settings->get_ok_button()->set_text(TTRC("Convert project.jundot"));
		}
		ask_update_backup->show();
		migration_guide_button->show();
		ask_update_settings->popup_centered(popup_min_size);
		ask_update_settings->get_cancel_button()->grab_focus(); // To prevent accidents.
		return;
	}
	// Check if the file was generated by a newer, incompatible engine version.
	if (config_version > ProjectSettings::CONFIG_VERSION) {
		_show_error(vformat(TTR("Can't open project \"%s\" at the following path:\n\n%s\n\nThe project settings were created by a newer engine version, whose settings are not compatible with this version."), project.project_name, project.path), popup_min_size);
		return;
	}
	// Check if the project is using features not supported by this build of Jundot.
	if (!unsupported_features.is_empty()) {
		String warning_message = "";
		for (int i = 0; i < unsupported_features.size(); i++) {
			const String &feature = unsupported_features[i];
			if (feature == "Double Precision") {
				ask_update_backup->show();
				warning_message += TTR("Warning: This project uses double precision floats, but this version of\nJundot uses single precision floats. Opening this project may cause data loss.\n\n");
				unsupported_features.remove_at(i);
				i--;
			} else if (feature == "C#") {
				warning_message += TTR("Warning: This project uses C#, but this build of Jundot does not have\nthe Mono module. If you proceed you will not be able to use any C# scripts.\n\n");
				unsupported_features.remove_at(i);
				i--;
			} else if (ProjectList::project_feature_looks_like_version(feature)) {
				ask_update_backup->show();
				migration_guide_button->show();
				version_convert_feature = feature;
				warning_message += vformat(TTR("Warning: This project was last edited in Jundot %s. Opening will change it to Jundot %s.\n\n"), Variant(feature), Variant(JUNDOT_VERSION_BRANCH));
				unsupported_features.remove_at(i);
				i--;
			}
		}
		if (!unsupported_features.is_empty()) {
			String unsupported_features_str = String(", ").join(unsupported_features);
			warning_message += vformat(TTR("Warning: This project uses the following features not supported by this build of Jundot:\n\n%s\n\n"), unsupported_features_str);
		}
		warning_message += TTR("Open anyway? Project will be modified.");
		ask_update_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		ask_update_label->set_text(warning_message);
		ask_update_settings->popup_centered(popup_min_size);
		return;
	}

	// Open if the project is up-to-date.
	_open_selected_projects();
}

void ProjectManager::_open_selected_projects_check_recovery_mode() {
	Vector<ProjectList::Item> selected_projects = project_list->get_selected_projects();

	if (selected_projects.is_empty()) {
		return;
	}

	const ProjectList::Item &project = selected_projects[0];
	if (project.missing) {
		return;
	}

	open_in_verbose_mode = false;
	open_in_recovery_mode = false;
	// Check if the project failed to load during last startup.
	if (project.recovery_mode) {
		_open_recovery_mode_ask(false);
		return;
	}

	_open_selected_projects_check_warnings();
}

void ProjectManager::_open_selected_projects_with_migration() {
	if (ask_update_backup->is_pressed() && project_list->get_selected_projects().size() == 1) {
		ask_update_settings->hide();
		ask_update_backup->set_pressed(false);

		_duplicate_project_with_action(POST_DUPLICATE_ACTION_OPEN);
		return;
	}

#ifndef DISABLE_DEPRECATED
	if (project_list->get_selected_projects().size() == 1) {
		// Only migrate if a single project is opened.
		_minor_project_migrate();
	}
#endif
	_open_selected_projects();
}

void ProjectManager::_install_project(const String &p_zip_path, const String &p_title) {
	project_dialog->set_mode(ProjectDialog::MODE_INSTALL);
	project_dialog->set_zip_path(p_zip_path);
	project_dialog->set_zip_title(p_title);
	project_dialog->show_dialog();
}

void ProjectManager::_import_project() {
	project_dialog->set_mode(ProjectDialog::MODE_IMPORT);
	project_dialog->ask_for_path_and_show();
}

String ProjectManager::_ai_project_sanitize_english_name(const String &p_name) const {
	String sanitized;
	bool last_was_space = false;
	for (int i = 0; i < p_name.length(); i++) {
		const char32_t c = p_name.unicode_at(i);
		const bool is_ascii_letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
		const bool is_digit = c >= '0' && c <= '9';
		const bool is_separator = c == ' ' || c == '_' || c == '-';
		if (is_ascii_letter || is_digit) {
			sanitized += String::chr(c);
			last_was_space = false;
		} else if (is_separator && !last_was_space && !sanitized.is_empty()) {
			sanitized += " ";
			last_was_space = true;
		}
	}
	sanitized = sanitized.strip_edges();
	if (sanitized.is_empty()) {
		return "New Project";
	}
	return sanitized;
}

String ProjectManager::_ai_project_extract_json_block(const String &p_content) const {
	const int json_block_start = p_content.find("```json");
	if (json_block_start >= 0) {
		const int block_content_start = p_content.find("\n", json_block_start);
		const int block_end = p_content.find("```", block_content_start + 1);
		if (block_content_start >= 0 && block_end > block_content_start) {
			return p_content.substr(block_content_start + 1, block_end - block_content_start - 1).strip_edges();
		}
	}

	const int object_start = p_content.find("{");
	const int object_end = p_content.rfind("}");
	if (object_start >= 0 && object_end > object_start) {
		return p_content.substr(object_start, object_end - object_start + 1).strip_edges();
	}

	return String();
}

static String _ai_project_escape_bbcode_text(const String &p_text) {
	return p_text.replace("[", "[lb]");
}

static bool _ai_project_is_markdown_table_separator(const String &p_line) {
	bool has_dash = false;
	for (int i = 0; i < p_line.length(); i++) {
		const char32_t c = p_line.unicode_at(i);
		if (c == '-') {
			has_dash = true;
			continue;
		}
		if (c == '|' || c == ':' || c == ' ') {
			continue;
		}
		return false;
	}
	return has_dash;
}

static String _ai_project_format_inline_markdown_for_bbcode(const String &p_text) {
	String out;
	bool bold = false;
	int pos = 0;
	while (pos < p_text.length()) {
		const int marker = p_text.find("**", pos);
		if (marker < 0) {
			out += _ai_project_escape_bbcode_text(p_text.substr(pos));
			break;
		}
		out += _ai_project_escape_bbcode_text(p_text.substr(pos, marker - pos));
		out += bold ? "[/b]" : "[b]";
		bold = !bold;
		pos = marker + 2;
	}
	if (bold) {
		out += "[/b]";
	}
	return out;
}

String ProjectManager::_ai_project_format_chat_text_for_panel(const String &p_text) const {
	PackedStringArray lines = p_text.strip_edges().split("\n");
	String out;
	for (int i = 0; i < lines.size(); i++) {
		String line = lines[i].strip_edges();
		if (line.is_empty()) {
			out += "\n";
			continue;
		}

		if (line.begins_with("|") && line.ends_with("|")) {
			if (_ai_project_is_markdown_table_separator(line)) {
				continue;
			}

			PackedStringArray cells = line.split("|", false);
			Vector<String> clean_cells;
			for (int j = 0; j < cells.size(); j++) {
				const String cell = cells[j].strip_edges();
				if (!cell.is_empty()) {
					clean_cells.push_back(cell);
				}
			}
			if (clean_cells.size() >= 2) {
				out += "- [b]" + _ai_project_escape_bbcode_text(clean_cells[0]) + "[/b]: ";
				for (int j = 1; j < clean_cells.size(); j++) {
					if (j > 1) {
						out += " / ";
					}
					out += _ai_project_escape_bbcode_text(clean_cells[j]);
				}
				out += "\n";
				continue;
			}
		}

		while (line.begins_with("#")) {
			line = line.substr(1).strip_edges();
		}
		out += _ai_project_format_inline_markdown_for_bbcode(line) + "\n";
	}
	return out.strip_edges();
}

void ProjectManager::_ai_project_chat_append(const String &p_speaker, const String &p_text) {
	if (!ai_project_chat_log) {
		return;
	}

	if (!ai_project_chat_log->get_parsed_text().is_empty()) {
		ai_project_chat_log->append_text("\n\n");
	}
	ai_project_chat_log->append_text("[b]" + _ai_project_escape_bbcode_text(p_speaker) + "[/b]\n");
	ai_project_chat_log->append_text(_ai_project_format_chat_text_for_panel(p_text));
	ai_project_chat_log->scroll_to_line(100000);
}

void ProjectManager::_ai_project_send_user_message(const String &p_message) {
	ERR_FAIL_NULL(ai_project_chat_service);

	const String message = p_message.strip_edges();
	if (message.is_empty() || ai_project_chat_service->is_requesting()) {
		return;
	}
	if (!_ensure_ai_usage_agreement_for_project(String())) {
		ai_usage_pending_ai_message = message;
		return;
	}

	_ai_project_chat_append(TTR("You"), message);

	Dictionary user_message;
	user_message["role"] = "user";
	user_message["content"] = message;
	ai_project_chat_messages.push_back(user_message);

	AISettingsData settings = AISettings::load();

	Array messages;
	Dictionary system_message;
	String system_prompt =
			"You are the Jundot Project Manager creation assistant. Talk with the user in their language, "
			"summarize what project they want, and when enough information is available propose an ASCII English project name. "
			"The project will be created with Jundot. Never ask the user to choose a game engine, technology stack, web framework, Unity, Godot, Python, Phaser, PixiJS, or similar platform. "
			"Assume Jundot with GDScript by default; use C# only if the user explicitly asks for C#. "
			"Ask only about game concept, art style, genre, theme, core mechanics, mood, target scope, and project name/content. "
			"This chat is displayed in a narrow Project Manager panel: do not use Markdown tables, code blocks, wide comparison grids, or long option lists. Prefer short bullets and ask at most three concise questions at a time. "
			"When you need the user to choose options, put them only in the final JSON questions array instead of writing Q1/Q2/Q3 in prose. Each question object must contain id, text, type, options, and allow_custom. type must be single or multiple. "
			"The project_name must contain only English letters, digits, spaces, hyphens, or underscores. "
			"Every reply must include a final JSON code block with exactly these fields: "
			"project_name, project_brief, ready_to_create, questions. Set ready_to_create to true only when the project name and content are clear. "
			"Do not claim the project was created; the editor will create it after the user confirms.";
	const String configured_language = settings.output_language.strip_edges();
	if (!configured_language.is_empty() && configured_language != "auto") {
		system_prompt += "\nRespond to the user in " + configured_language + ". Keep project_name in English.";
	}
	system_message["role"] = "system";
	system_message["content"] = system_prompt;
	messages.push_back(system_message);
	for (int i = 0; i < ai_project_chat_messages.size(); i++) {
		messages.push_back(ai_project_chat_messages[i]);
	}

	ai_project_chat_send_btn->set_disabled(true);
	ai_project_chat_input->set_editable(false);
	if (ai_project_summary_label) {
		ai_project_summary_label->set_text(TTR("Thinking..."));
	}

	ai_project_chat_service->configure(settings);
	const Error err = ai_project_chat_service->send_messages(messages);
	if (err != OK) {
		ai_project_chat_send_btn->set_disabled(false);
		ai_project_chat_input->set_editable(true);
		if (ai_project_summary_label) {
			ai_project_summary_label->set_text(TTR("AI chat is not ready."));
		}
		_ai_project_chat_append(TTR("Jundot AI"), TTR("AI chat is not ready. Check AI settings and usage agreement, then try again."));
	}
}

void ProjectManager::_ai_project_chat_send() {
	ERR_FAIL_NULL(ai_project_chat_input);

	const String message = ai_project_chat_input->get_text().strip_edges();
	if (message.is_empty()) {
		return;
	}
	ai_project_chat_input->clear();
	_ai_project_send_user_message(message);
}

void ProjectManager::_ai_project_chat_completed(int p_result, int p_response_code, const String &p_content, const Dictionary &p_json, const String &p_raw_body, double p_elapsed_seconds, const String &p_think_content, int p_prompt_tokens, int p_completion_tokens) {
	ai_project_chat_send_btn->set_disabled(false);
	ai_project_chat_input->set_editable(true);

	if (p_result != HTTPRequest::RESULT_SUCCESS || p_response_code < 200 || p_response_code >= 300) {
		if (ai_project_summary_label) {
			ai_project_summary_label->set_text(TTR("AI request failed."));
		}
		_ai_project_chat_append(TTR("Jundot AI"), vformat(TTR("AI request failed. Result: %d, response code: %d."), p_result, p_response_code));
		return;
	}

	String visible_content = p_content.strip_edges();
	const int json_block_start = visible_content.find("```json");
	if (json_block_start >= 0) {
		visible_content = visible_content.left(json_block_start).strip_edges();
	}
	if (visible_content.is_empty()) {
		visible_content = TTR("I summarized the project. Review the project name below.");
	}
	_ai_project_chat_append(TTR("Jundot AI"), visible_content);

	Dictionary assistant_message;
	assistant_message["role"] = "assistant";
	assistant_message["content"] = p_content.strip_edges();
	ai_project_chat_messages.push_back(assistant_message);

	_ai_project_update_summary_from_response(p_content);
}

void ProjectManager::_ai_project_chat_stream_data(const String &p_delta, const String &p_full_content, int p_completion_tokens) {
	// The compact Project Manager panel shows the finalized assistant message.
}

void ProjectManager::_ai_project_update_summary_from_response(const String &p_content) {
	const String json_text = _ai_project_extract_json_block(p_content);
	if (json_text.is_empty()) {
		_ai_project_update_create_button();
		return;
	}

	JSON json;
	const Error err = json.parse(json_text);
	if (err != OK || json.get_data().get_type() != Variant::DICTIONARY) {
		_ai_project_update_create_button();
		return;
	}

	Dictionary summary_data = json.get_data();
	const bool ready = summary_data.get("ready_to_create", false);
	const String name = _ai_project_sanitize_english_name(String(summary_data.get("project_name", String())));
	const String brief = String(summary_data.get("project_brief", String())).strip_edges();
	if (ready && !name.is_empty() && !brief.is_empty()) {
		ai_project_suggested_name = name;
		ai_project_suggested_brief = brief;
		if (ai_project_summary_label) {
			ai_project_summary_label->set_text(vformat(TTR("Ready: %s"), ai_project_suggested_name));
		}
	}

	const Variant questions_value = summary_data.get("questions", Array());
	if (questions_value.get_type() == Variant::ARRAY) {
		_ai_project_set_questions(questions_value);
	}

	_ai_project_update_create_button();
}

void ProjectManager::_ai_project_update_create_button() {
	if (ai_project_create_btn) {
		ai_project_create_btn->set_disabled(ai_project_suggested_name.is_empty() || ai_project_suggested_brief.is_empty());
	}
}

void ProjectManager::_ai_project_create_from_summary() {
	if (ai_project_suggested_name.is_empty()) {
		return;
	}

	ai_project_pending_memory_name = ai_project_suggested_name;
	ai_project_pending_memory_brief = ai_project_suggested_brief;

	project_dialog->set_mode(ProjectDialog::MODE_NEW);
	project_dialog->show_dialog();
	project_dialog->set_project_name(ai_project_suggested_name);
}

void ProjectManager::_ai_project_save_memory(const String &p_project_path) {
	if (ai_project_pending_memory_name.is_empty() || ai_project_pending_memory_brief.is_empty()) {
		return;
	}

	const String memory_path = p_project_path.path_join(".JundotAI").path_join("memory.json");
	Vector<AIMemoryEntry> entries;
	AIMemoryStore::load(entries, memory_path);

	String content = vformat("Project name: %s\n\nProject brief:\n%s", ai_project_pending_memory_name, ai_project_pending_memory_brief);
	if (!ai_project_chat_messages.is_empty()) {
		content += "\n\nCreation conversation summary:\n";
		for (int i = MAX(0, ai_project_chat_messages.size() - 8); i < ai_project_chat_messages.size(); i++) {
			Dictionary msg = ai_project_chat_messages[i];
			content += vformat("- %s: %s\n", String(msg.get("role", String())), String(msg.get("content", String())).strip_edges().replace("\n", " "));
		}
	}

	AIMemoryEntry entry = AIMemoryStore::make_entry("Project concept brief", content.strip_edges());
	entry.tags.push_back("project_creation");
	entry.tags.push_back("brief");
	entries.push_back(entry);
	AIMemoryStore::save(entries, memory_path);

	_ai_project_chat_append(TTR("Jundot AI"), TTR("Project created. I saved this brief into the project's AI memory so the editor chat can continue from it."));
	ai_project_pending_memory_name.clear();
	ai_project_pending_memory_brief.clear();
}

void ProjectManager::_ai_project_set_questions(const Array &p_questions) {
	ai_project_questions.clear();
	for (int i = 0; i < p_questions.size(); i++) {
		if (p_questions[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		Dictionary question = p_questions[i];
		const String text = String(question.get("text", String())).strip_edges();
		const Variant options_value = question.get("options", Array());
		if (text.is_empty() || options_value.get_type() != Variant::ARRAY || Array(options_value).is_empty()) {
			continue;
		}

		String type = String(question.get("type", "single")).strip_edges().to_lower();
		if (type != "multiple") {
			type = "single";
		}
		question["type"] = type;
		question["answer"] = Array();
		question["custom_answer"] = String();
		ai_project_questions.push_back(question);
	}

	ai_project_question_index = 0;
	_ai_project_show_question(0);
}

void ProjectManager::_ai_project_store_current_question_answer() {
	if (ai_project_question_index < 0 || ai_project_question_index >= ai_project_questions.size()) {
		return;
	}

	Dictionary question = ai_project_questions[ai_project_question_index];
	Array answer;
	for (int i = 0; i < ai_project_question_option_checks.size(); i++) {
		if (ai_project_question_option_checks[i]->is_pressed()) {
			answer.push_back(ai_project_question_option_checks[i]->get_text());
		}
	}
	question["answer"] = answer;
	if (ai_project_question_custom) {
		question["custom_answer"] = ai_project_question_custom->get_text().strip_edges();
	}
	ai_project_questions[ai_project_question_index] = question;
}

void ProjectManager::_ai_project_show_question(int p_index) {
	if (!ai_project_questions_panel) {
		return;
	}
	if (ai_project_questions.is_empty()) {
		ai_project_questions_panel->hide();
		return;
	}

	ai_project_question_index = CLAMP(p_index, 0, ai_project_questions.size() - 1);
	Dictionary question = ai_project_questions[ai_project_question_index];
	ai_project_questions_panel->show();

	if (ai_project_question_title) {
		ai_project_question_title->set_text(vformat(TTR("Question %d of %d"), ai_project_question_index + 1, ai_project_questions.size()));
	}
	if (ai_project_question_text) {
		const String type = String(question.get("type", "single")) == "multiple" ? TTR("Multiple choice") : TTR("Single choice");
		ai_project_question_text->set_text(String(question.get("text", String())) + "\n" + type);
	}

	if (ai_project_question_options) {
		while (ai_project_question_options->get_child_count() > 0) {
			Node *child = ai_project_question_options->get_child(0);
			ai_project_question_options->remove_child(child);
			child->queue_free();
		}
	}
	ai_project_question_option_checks.clear();

	const Array options = question.get("options", Array());
	const Array answer = question.get("answer", Array());
	for (int i = 0; i < options.size(); i++) {
		CheckBox *option = memnew(CheckBox);
		option->set_text(String(options[i]));
		option->set_pressed(answer.has(String(options[i])));
		option->connect(SceneStringName(toggled), callable_mp(this, &ProjectManager::_ai_project_question_option_toggled).bind(i));
		ai_project_question_options->add_child(option);
		ai_project_question_option_checks.push_back(option);
	}

	if (ai_project_question_custom) {
		ai_project_question_custom->set_text(String(question.get("custom_answer", String())));
		ai_project_question_custom->set_visible(bool(question.get("allow_custom", true)));
	}

	_ai_project_update_question_nav();
}

void ProjectManager::_ai_project_question_option_toggled(bool p_pressed, int p_option_index) {
	if (p_pressed && ai_project_question_index >= 0 && ai_project_question_index < ai_project_questions.size()) {
		Dictionary question = ai_project_questions[ai_project_question_index];
		if (String(question.get("type", "single")) != "multiple") {
			for (int i = 0; i < ai_project_question_option_checks.size(); i++) {
				if (i != p_option_index) {
					ai_project_question_option_checks[i]->set_pressed_no_signal(false);
				}
			}
		}
	}
	_ai_project_store_current_question_answer();
	_ai_project_update_question_nav();
}

void ProjectManager::_ai_project_question_custom_changed(const String &p_text) {
	_ai_project_store_current_question_answer();
	_ai_project_update_question_nav();
}

void ProjectManager::_ai_project_question_prev() {
	_ai_project_store_current_question_answer();
	_ai_project_show_question(ai_project_question_index - 1);
}

void ProjectManager::_ai_project_question_next() {
	_ai_project_store_current_question_answer();
	_ai_project_show_question(ai_project_question_index + 1);
}

void ProjectManager::_ai_project_question_submit() {
	_ai_project_store_current_question_answer();
	String message = TTR("My answers are:") + "\n";
	for (int i = 0; i < ai_project_questions.size(); i++) {
		Dictionary question = ai_project_questions[i];
		Array answer = question.get("answer", Array());
		const String custom_answer = String(question.get("custom_answer", String())).strip_edges();
		String answer_text;
		for (int j = 0; j < answer.size(); j++) {
			if (j > 0) {
				answer_text += ", ";
			}
			answer_text += String(answer[j]);
		}
		if (!custom_answer.is_empty()) {
			if (!answer_text.is_empty()) {
				answer_text += ", ";
			}
			answer_text += custom_answer;
		}
		if (answer_text.is_empty()) {
			answer_text = TTR("Skipped");
		}
		message += vformat("Q%d: %s\n%s: %s\n", i + 1, String(question.get("text", String())), TTR("Answer"), answer_text);
	}

	ai_project_questions.clear();
	_ai_project_show_question(0);
	_ai_project_send_user_message(message);
}

void ProjectManager::_ai_project_update_question_nav() {
	if (ai_project_question_prev_btn) {
		ai_project_question_prev_btn->set_disabled(ai_project_question_index <= 0);
	}
	if (ai_project_question_next_btn) {
		ai_project_question_next_btn->set_disabled(ai_project_question_index >= ai_project_questions.size() - 1);
	}
	if (ai_project_question_submit_btn) {
		ai_project_question_submit_btn->set_disabled(ai_project_questions.is_empty());
	}
}

void ProjectManager::_ai_project_clear_chat() {
	if (ai_project_chat_service && ai_project_chat_service->is_requesting()) {
		ai_project_chat_service->cancel_request();
	}

	ai_project_chat_messages.clear();
	ai_project_suggested_name.clear();
	ai_project_suggested_brief.clear();
	ai_project_pending_memory_name.clear();
	ai_project_pending_memory_brief.clear();
	ai_project_questions.clear();
	ai_project_question_index = 0;

	if (ai_project_chat_log) {
		ai_project_chat_log->clear();
	}
	if (ai_project_summary_label) {
		ai_project_summary_label->set_text(TTR("Tell me what you want to create."));
	}
	if (ai_project_chat_input) {
		ai_project_chat_input->clear();
		ai_project_chat_input->set_editable(true);
	}
	if (ai_project_chat_send_btn) {
		ai_project_chat_send_btn->set_disabled(false);
	}

	_ai_project_update_create_button();
	_ai_project_show_question(0);
	_ai_project_chat_append(TTR("Jundot AI"), TTR("Conversation cleared. Tell me the game concept you want to create with Jundot."));
}

void ProjectManager::_show_ai_config_dialog() {
	ERR_FAIL_NULL(ai_config_dialog);
	ai_config_dialog->popup_centered_clamped(Size2(760, 620) * EDSCALE, 0.9);
}

void ProjectManager::_new_project() {
	project_dialog->set_mode(ProjectDialog::MODE_NEW);
	project_dialog->show_dialog();
}

void ProjectManager::_rename_project() {
	const Vector<ProjectList::Item> &selected_list = project_list->get_selected_projects();

	if (selected_list.is_empty()) {
		return;
	}

	for (const ProjectList::Item &E : selected_list) {
		project_dialog->set_project_name(E.project_name);
		project_dialog->set_project_path(E.path);
		project_dialog->set_mode(ProjectDialog::MODE_RENAME);
		project_dialog->show_dialog();
	}
}

void ProjectManager::_duplicate_project() {
	_duplicate_project_with_action(POST_DUPLICATE_ACTION_NONE);
}

void ProjectManager::_duplicate_project_with_action(PostDuplicateAction p_post_action) {
	Vector<ProjectList::Item> selected_projects = project_list->get_selected_projects();
	if (selected_projects.is_empty()) {
		return;
	}

	post_duplicate_action = p_post_action;

	const ProjectList::Item &project = selected_projects[0];

	project_dialog->set_mode(ProjectDialog::MODE_DUPLICATE);
	project_dialog->set_project_name(vformat("%s (%s)", project.project_name, p_post_action == POST_DUPLICATE_ACTION_NONE ? "Copy" : project.project_version));
	project_dialog->set_original_project_path(project.path);
	project_dialog->set_duplicate_can_edit(p_post_action == POST_DUPLICATE_ACTION_NONE);
	project_dialog->show_dialog(false);
}

void ProjectManager::_show_project_in_file_manager() {
	const Vector<ProjectList::Item> &selected_list = project_list->get_selected_projects();
	if (selected_list.is_empty()) {
		return;
	}

	for (const ProjectList::Item &E : selected_list) {
		OS::get_singleton()->shell_show_in_file_manager(E.path, true);
	}
}

void ProjectManager::_erase_project() {
	const HashSet<String> &selected_list = project_list->get_selected_project_keys();

	if (selected_list.is_empty()) {
		return;
	}

	String confirm_message;
	if (selected_list.size() >= 2) {
		confirm_message = vformat(TTR("Remove %d projects from the list?"), selected_list.size());
	} else {
		confirm_message = TTRC("Remove this project from the list?");
	}

	erase_ask_label->set_text(confirm_message);
	//delete_project_contents->set_pressed(false);
	erase_ask->popup_centered();
}

void ProjectManager::_erase_missing_projects() {
	erase_missing_ask->set_text(TTRC("Remove all missing projects from the list?\nThe project folders' contents won't be modified."));
	erase_missing_ask->popup_centered();
}

void ProjectManager::_erase_project_confirm() {
	project_list->erase_selected_projects(false);
	_update_project_buttons();
	_update_list_placeholder();
}

void ProjectManager::_erase_missing_projects_confirm() {
	project_list->erase_missing_projects();
	_update_project_buttons();
	_update_list_placeholder();
}

void ProjectManager::_update_project_buttons() {
	Vector<ProjectList::Item> selected_projects = project_list->get_selected_projects();
	bool empty_selection = selected_projects.is_empty();

	bool is_missing_project_selected = false;
	for (int i = 0; i < selected_projects.size(); ++i) {
		if (selected_projects[i].missing) {
			is_missing_project_selected = true;
			break;
		}
	}

	erase_btn->set_disabled(empty_selection);
	open_btn->set_disabled(empty_selection || is_missing_project_selected);
	open_options_btn->set_disabled(empty_selection || is_missing_project_selected);
	rename_btn->set_disabled(empty_selection || is_missing_project_selected);
	duplicate_btn->set_disabled(empty_selection || is_missing_project_selected);
	manage_tags_btn->set_disabled(empty_selection || is_missing_project_selected || selected_projects.size() > 1);
	run_btn->set_disabled(empty_selection || is_missing_project_selected);

	erase_missing_btn->set_disabled(!project_list->is_any_project_missing());
}

bool ProjectManager::_ensure_ai_usage_agreement_for_project(const String &p_project_path, bool p_popup) {
	if (AISettings::is_usage_agreement_current(AISettings::load(), p_project_path)) {
		return true;
	}

	if (!p_popup || !ai_usage_agreement_dialog) {
		return false;
	}

	if (ai_usage_agreement_dialog->is_visible()) {
		return false;
	}

	ai_usage_agreement_project_path = p_project_path;
	ai_usage_agreement_dialog->set_project_path(p_project_path);
	ai_usage_agreement_dialog->popup_centered(Size2(420, 220) * EDSCALE);
	return false;
}

void ProjectManager::_ai_usage_agreement_accepted() {
	if (!ai_usage_pending_ai_message.is_empty()) {
		const String pending_message = ai_usage_pending_ai_message;
		ai_usage_pending_ai_message.clear();
		_ai_project_send_user_message(pending_message);
	}
}

void ProjectManager::_ai_usage_agreement_rejected() {
	ai_usage_pending_ai_message.clear();
}

void ProjectManager::_open_options_popup() {
	Rect2 rect = open_btn_container->get_screen_rect();
	rect.position.y += rect.size.height;
	open_options_popup->set_size(Size2(rect.size.width, 0));
	open_options_popup->set_position(rect.position);

	open_options_popup->popup();
}

void ProjectManager::_open_recovery_mode_ask(bool manual) {
	String recovery_mode_details;

	// Only show the initial crash preamble if this popup wasn't manually triggered.
	if (!manual) {
		recovery_mode_details +=
				TTR("It looks like Jundot crashed when opening this project the last time. If you're having problems editing this project, you can try to open it in Recovery Mode.") +
				String::utf8("\n\n");
	}

	recovery_mode_details +=
			TTR("Recovery Mode is a special mode that may help to recover projects that crash the engine during initialization. This mode temporarily disables the following features:") +
			String::utf8("\n\n•  ") + TTR("Tool scripts") +
			String::utf8("\n•  ") + TTR("Editor plugins") +
			String::utf8("\n•  ") + TTR("GDExtension addons") +
			String::utf8("\n•  ") + TTR("Automatic scene restoring") +
			String::utf8("\n\n") + TTR("This mode is intended only for basic editing to troubleshoot such issues, and therefore it will not be possible to run the project during this mode. It is also a good idea to make a backup of your project before proceeding.") +
			String::utf8("\n\n") + TTR("Edit the project in Recovery Mode?");

	open_recovery_mode_ask->set_text(recovery_mode_details);
	open_recovery_mode_ask->popup_centered(Size2(550, 70) * EDSCALE);
}

void ProjectManager::_on_projects_updated() {
	Vector<ProjectList::Item> selected_projects = project_list->get_selected_projects();
	int index = 0;
	for (int i = 0; i < selected_projects.size(); ++i) {
		index = project_list->refresh_project(selected_projects[i].path);
	}
	if (index != -1) {
		project_list->ensure_project_visible(index);
	}

	project_list->update_dock_menu();
}

void ProjectManager::_on_open_options_selected(int p_option) {
	switch (p_option) {
		case 0: // Edit in verbose mode.
			open_in_verbose_mode = true;
			_open_selected_projects_check_warnings();
			break;
		case 1: // Edit in recovery mode.
			_open_recovery_mode_ask(true);
			break;
	}
}

void ProjectManager::_on_recovery_mode_popup_open_normal() {
	open_recovery_mode_ask->hide();
	open_in_recovery_mode = false;
	_open_selected_projects_check_warnings();
}

void ProjectManager::_on_recovery_mode_popup_open_recovery() {
	open_in_recovery_mode = true;
	_open_selected_projects_check_warnings();
}

void ProjectManager::_on_project_created(const String &dir, bool edit) {
	project_list->add_project(dir, false);
	project_list->save_config();
	search_box->clear();

	int i = project_list->refresh_project(dir);
	project_list->ensure_project_visible(i);
	_update_list_placeholder();

	_ai_project_save_memory(dir);

	if (edit) {
		_open_selected_projects_check_warnings();
	}

	project_list->update_dock_menu();
}

void ProjectManager::_on_project_duplicated(const String &p_original_path, const String &p_duplicate_path, bool p_edit) {
	if (post_duplicate_action == POST_DUPLICATE_ACTION_NONE) {
		_on_project_created(p_duplicate_path, p_edit);
	} else {
		project_list->add_project(p_duplicate_path, false);
		project_list->save_config();

		if (post_duplicate_action == POST_DUPLICATE_ACTION_OPEN) {
			_open_selected_projects_with_migration();
		} else if (post_duplicate_action == POST_DUPLICATE_ACTION_FULL_CONVERSION) {
			_full_convert_button_pressed();
		}

		project_list->update_dock_menu();
	}

	post_duplicate_action = POST_DUPLICATE_ACTION_NONE;
}

void ProjectManager::_on_order_option_changed(int p_idx) {
	if (is_inside_tree()) {
		project_list->set_order_option(p_idx, true);
	}
}

void ProjectManager::_on_search_term_changed(const String &p_term) {
	project_list->set_search_term(p_term);
	project_list->sort_projects();

	// Select the first visible project in the list.
	// This makes it possible to open a project without ever touching the mouse,
	// as the search field is automatically focused on startup.
	project_list->select_first_visible_project();
	_update_project_buttons();
}

void ProjectManager::_on_search_term_submitted(const String &p_text) {
	if (current_main_view != MAIN_VIEW_PROJECTS) {
		return;
	}

	_open_selected_projects_check_recovery_mode();
}

LineEdit *ProjectManager::get_search_box() {
	return search_box;
}

// Project tag management.

void ProjectManager::_manage_project_tags() {
	for (int i = 0; i < project_tags->get_child_count(); i++) {
		project_tags->get_child(i)->queue_free();
	}

	const ProjectList::Item item = project_list->get_selected_projects()[0];
	current_project_tags = item.tags;
	for (const String &tag : current_project_tags) {
		ProjectTag *tag_control = memnew(ProjectTag(tag, true));
		project_tags->add_child(tag_control);
		tag_control->connect_button_to(callable_mp(this, &ProjectManager::_delete_project_tag).bind(tag));
	}

	tag_edit_error->hide();
	tag_manage_dialog->popup_centered(Vector2i(500, 0) * EDSCALE);
}

void ProjectManager::_add_project_tag(const String &p_tag) {
	if (current_project_tags.has(p_tag)) {
		return;
	}
	current_project_tags.append(p_tag);

	ProjectTag *tag_control = memnew(ProjectTag(p_tag, true));
	project_tags->add_child(tag_control);
	tag_control->connect_button_to(callable_mp(this, &ProjectManager::_delete_project_tag).bind(p_tag));
}

void ProjectManager::_delete_project_tag(const String &p_tag) {
	current_project_tags.erase(p_tag);
	for (int i = 0; i < project_tags->get_child_count(); i++) {
		ProjectTag *tag_control = Object::cast_to<ProjectTag>(project_tags->get_child(i));
		if (tag_control && tag_control->get_tag() == p_tag) {
			memdelete(tag_control);
			break;
		}
	}
}

void ProjectManager::_apply_project_tags() {
	PackedStringArray tags;
	for (int i = 0; i < project_tags->get_child_count(); i++) {
		ProjectTag *tag_control = Object::cast_to<ProjectTag>(project_tags->get_child(i));
		if (tag_control) {
			tags.append(tag_control->get_tag());
		}
	}

	const String project_jundot = project_list->get_selected_projects()[0].path.path_join("project.jundot");
	ProjectSettings *cfg = memnew(ProjectSettings(project_jundot));
	if (!cfg->is_project_loaded()) {
		memdelete(cfg);
		tag_edit_error->set_text(vformat(TTR("Couldn't load project at '%s'. It may be missing or corrupted."), project_jundot));
		tag_edit_error->show();
		callable_mp((Window *)tag_manage_dialog, &Window::show).call_deferred(); // Make sure the dialog does not disappear.
		return;
	} else {
		tags.sort();
		cfg->set("application/config/tags", tags);
		Error err = cfg->save_custom(project_jundot);
		memdelete(cfg);

		if (err != OK) {
			tag_edit_error->set_text(vformat(TTR("Couldn't save project at '%s' (error %d)."), project_jundot, err));
			tag_edit_error->show();
			callable_mp((Window *)tag_manage_dialog, &Window::show).call_deferred();
			return;
		}
	}

	_on_projects_updated();
}

void ProjectManager::_set_new_tag_name(const String p_name) {
	create_tag_dialog->get_ok_button()->set_disabled(true);
	if (p_name.strip_edges().is_empty()) {
		tag_error->set_text(TTRC("Tag name can't be empty."));
		return;
	}

	if (p_name[0] == '_' || p_name[p_name.length() - 1] == '_') {
		tag_error->set_text(TTRC("Tag name can't begin or end with underscore."));
		return;
	}

	bool was_underscore = false;
	for (const char32_t &c : p_name.span()) {
		// Treat spaces as underscores, as we convert spaces to underscores automatically in the tag input field.
		if (c == '_' || c == ' ') {
			if (was_underscore) {
				tag_error->set_text(TTRC("Tag name can't contain consecutive underscores or spaces."));
				return;
			}
			was_underscore = true;
		} else {
			was_underscore = false;
		}
	}

	for (const String &c : forbidden_tag_characters) {
		if (p_name.contains(c)) {
			tag_error->set_text(vformat(TTR("These characters are not allowed in tags: %s."), String(" ").join(forbidden_tag_characters)));
			return;
		}
	}

	tag_error->set_text("");
	create_tag_dialog->get_ok_button()->set_disabled(false);
}

void ProjectManager::_create_new_tag() {
	if (!tag_error->get_text().is_empty()) {
		return;
	}
	create_tag_dialog->hide(); // When using text_submitted, need to hide manually.

	// Enforce a valid tag name (no spaces, lowercase only) automatically.
	// The project manager displays underscores as spaces, and capitalization is performed automatically.
	const String new_tag = new_tag_name->get_text().strip_edges().to_lower().replace_char(' ', '_');
	add_new_tag(new_tag);
	_add_project_tag(new_tag);
}

void ProjectManager::add_new_tag(const String &p_tag) {
	if (!tag_set.has(p_tag)) {
		tag_set.insert(p_tag);
		ProjectTag *tag_control = memnew(ProjectTag(p_tag));
		all_tags->add_child(tag_control);
		all_tags->move_child(tag_control, -2);
		tag_control->connect_button_to(callable_mp(this, &ProjectManager::_add_project_tag).bind(p_tag));
	}
}

// Project converter/migration tool.

#ifndef DISABLE_DEPRECATED
void ProjectManager::_minor_project_migrate() {
	const ProjectList::Item migrated_project = project_list->get_selected_projects()[0];

	if (version_convert_feature.begins_with("4.3")) {
		// Migrate layout after scale changes.
		const float edscale = EDSCALE;
		if (edscale != 1.0) {
			Ref<ConfigFile> layout_file;
			layout_file.instantiate();

			const String layout_path = migrated_project.path.path_join(".jundot/editor/editor_layout.cfg");
			Error err = layout_file->load(layout_path);
			if (err == OK) {
				for (int i = 0; i < 4; i++) {
					const String key = "dock_hsplit_" + itos(i + 1);
					int old_value = layout_file->get_value("docks", key, 0);
					if (old_value != 0) {
						layout_file->set_value("docks", key, old_value / edscale);
					}
				}
				layout_file->save(layout_path);
			}
		}
	}
}
#endif

void ProjectManager::_full_convert_button_pressed() {
	ask_update_settings->hide();

	if (ask_update_backup->is_pressed()) {
		ask_update_backup->set_pressed(false);

		_duplicate_project_with_action(POST_DUPLICATE_ACTION_FULL_CONVERSION);
		return;
	}

	ask_full_convert_dialog->popup_centered(Size2i(600.0 * EDSCALE, 0));
	ask_full_convert_dialog->get_cancel_button()->grab_focus();
}

void ProjectManager::_migration_guide_button_pressed() {
	const String url = vformat("%s/tutorials/migrating/index.html", JUNDOT_VERSION_DOCS_URL);
	OS::get_singleton()->shell_open(url);
}

void ProjectManager::_perform_full_project_conversion() {
	Vector<ProjectList::Item> selected_list = project_list->get_selected_projects();
	if (selected_list.is_empty()) {
		return;
	}

	const String &path = selected_list[0].path;

	print_line("Converting project: " + path);
	List<String> args;
	args.push_back("--path");
	args.push_back(path);
	args.push_back("--convert-3to4");
	args.push_back("--rendering-driver");
	args.push_back(OS::get_singleton()->get_current_rendering_driver_name());

	Error err = OS::get_singleton()->create_instance(args);
	ERR_FAIL_COND(err);

	project_list->set_project_version(path, JUNDOT4_CONFIG_VERSION);
}

// Input and I/O.

void ProjectManager::shortcut_input(const Ref<InputEvent> &p_ev) {
	ERR_FAIL_COND(p_ev.is_null());

	Ref<InputEventKey> k = p_ev;

	if (k.is_valid()) {
		if (!k->is_pressed()) {
			return;
		}

		// Pressing Command + Q quits the Project Manager
		// This is handled by the platform implementation on macOS,
		// so only define the shortcut on other platforms
#ifndef MACOS_ENABLED
		if (k->get_keycode_with_modifiers() == (KeyModifierMask::META | Key::Q)) {
			_dim_window();
			get_tree()->quit();
		}
#endif

		if (current_main_view != MAIN_VIEW_PROJECTS) {
			return;
		}

		bool keycode_handled = true;

		switch (k->get_keycode()) {
			case Key::ENTER: {
				_open_selected_projects_check_recovery_mode();
			} break;
			case Key::HOME: {
				if (project_list->get_project_count() > 0) {
					project_list->ensure_project_visible(0);
				}

			} break;
			case Key::END: {
				if (project_list->get_project_count() > 0) {
					project_list->ensure_project_visible(project_list->get_project_count() - 1);
				}

			} break;
			case Key::F: {
				if (k->is_command_or_control_pressed()) {
					search_box->grab_focus();
				} else {
					keycode_handled = false;
				}
			} break;
			case Key::A: {
				if (k->is_command_or_control_pressed()) {
					if (k->is_shift_pressed()) {
						project_list->deselect_all_visible_projects();
					} else {
						project_list->select_all_visible_projects();
					}
					_update_project_buttons();
				}
			} break;
			default: {
				keycode_handled = false;
			} break;
		}

		if (keycode_handled) {
			accept_event();
		}
	}
}

void ProjectManager::_files_dropped(PackedStringArray p_files) {
	// TODO: Support installing multiple ZIPs at the same time?
	if (p_files.size() == 1 && p_files[0].ends_with(".zip")) {
		const String &file = p_files[0];
		_install_project(file, file.get_file().get_basename().capitalize());
		return;
	}

	HashSet<String> folders_set;
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	for (int i = 0; i < p_files.size(); i++) {
		const String &file = p_files[i];
		folders_set.insert(da->dir_exists(file) ? file : file.get_base_dir());
	}
	ERR_FAIL_COND(folders_set.is_empty()); // This can't really happen, we consume every dropped file path above.

	PackedStringArray folders;
	for (const String &E : folders_set) {
		folders.push_back(E);
	}
	project_list->find_projects_multiple(folders);
}

void ProjectManager::_titlebar_resized() {
	DisplayServer::get_singleton()->window_set_window_buttons_offset(Vector2i(title_bar->get_global_position().y + title_bar->get_size().y / 2, title_bar->get_global_position().y + title_bar->get_size().y / 2), DisplayServerEnums::MAIN_WINDOW_ID);
	const Vector3i &margin = DisplayServer::get_singleton()->window_get_safe_title_margins(DisplayServerEnums::MAIN_WINDOW_ID);
	if (left_menu_spacer) {
		int w = (root_container->is_layout_rtl()) ? margin.y : margin.x;
		left_menu_spacer->set_custom_minimum_size(Size2(w, 0));
	}
	if (right_menu_spacer) {
		int w = (root_container->is_layout_rtl()) ? margin.x : margin.y;
		right_menu_spacer->set_custom_minimum_size(Size2(w, 0));
	}
	if (title_bar) {
		title_bar->set_custom_minimum_size(Size2(0, margin.z - title_bar->get_global_position().y));
	}
}

void ProjectManager::_open_donate_page() {
	OS::get_singleton()->shell_open("https://fund.godotengine.org/?ref=project_manager");
}

// Object methods.

ProjectManager::ProjectManager() {
	singleton = this;

	// Turn off some servers we aren't going to be using in the Project Manager.
	NavigationServer3D::get_singleton()->set_active(false);
	PhysicsServer3D::get_singleton()->set_active(false);
	PhysicsServer2D::get_singleton()->set_active(false);

	// Initialize settings.
	{
		if (!EditorSettings::get_singleton()) {
			EditorSettings::create();
		}
		EditorSettings::get_singleton()->set_optimize_save(false); // Just write settings as they come.

		{
			bool agile_input_event_flushing = EDITOR_GET("input/buffering/agile_event_flushing");
			bool use_accumulated_input = EDITOR_GET("input/buffering/use_accumulated_input");

			Input::get_singleton()->set_agile_input_event_flushing(agile_input_event_flushing);
			Input::get_singleton()->set_use_accumulated_input(use_accumulated_input);
		}

		int display_scale = EDITOR_GET("interface/editor/appearance/display_scale");

		switch (display_scale) {
			case 0:
				// Try applying a suitable display scale automatically.
				EditorScale::set_scale(EditorSettings::get_auto_display_scale());
				break;
			case 1:
				EditorScale::set_scale(0.75);
				break;
			case 2:
				EditorScale::set_scale(1.0);
				break;
			case 3:
				EditorScale::set_scale(1.25);
				break;
			case 4:
				EditorScale::set_scale(1.5);
				break;
			case 5:
				EditorScale::set_scale(1.75);
				break;
			case 6:
				EditorScale::set_scale(2.0);
				break;
			default:
				EditorScale::set_scale(EDITOR_GET("interface/editor/appearance/custom_display_scale"));
				break;
		}
		FileDialog::set_get_icon_callback(callable_mp_static(ProjectManager::_file_dialog_get_icon));
		FileDialog::set_get_thumbnail_callback(callable_mp_static(ProjectManager::_file_dialog_get_thumbnail));

		FileDialog::set_default_show_hidden_files(EDITOR_GET("filesystem/file_dialog/show_hidden_files"));
		FileDialog::set_default_display_mode((FileDialog::DisplayMode)EDITOR_GET("filesystem/file_dialog/display_mode").operator int());

		int swap_cancel_ok = EDITOR_GET("interface/editor/appearance/accept_dialog_cancel_ok_buttons");
		if (swap_cancel_ok != 0) { // 0 is auto, set in register_scene based on DisplayServer.
			// Swap on means OK first.
			AcceptDialog::set_swap_cancel_ok(swap_cancel_ok == 2);
		}

		OS::get_singleton()->set_low_processor_usage_mode(true);
	}

#if defined(MODULE_GDSCRIPT_ENABLED) || defined(MODULE_MONO_ENABLED)
	EditorHelpHighlighter::create_singleton();
#endif

	SceneTree::get_singleton()->get_root()->connect("files_dropped", callable_mp(this, &ProjectManager::_files_dropped));

	// Initialize UI.
	{
		int pm_root_dir = EDITOR_GET("interface/editor/localization/ui_layout_direction");
		Control::set_root_layout_direction(pm_root_dir);
		Window::set_root_layout_direction(pm_root_dir);

		EditorThemeManager::initialize();
		theme = EditorThemeManager::generate_theme();
		DisplayServer::set_early_window_clear_color_override(true, theme->get_color(SNAME("background"), EditorStringName(Editor)));

		set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);

		_build_icon_type_cache(theme);
	}

	// Project manager layout.

	background_panel = memnew(Panel);
	add_child(background_panel);
	background_panel->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);

	root_container = memnew(MarginContainer);
	add_child(root_container);
	root_container->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);

	main_vbox = memnew(VBoxContainer);
	root_container->add_child(main_vbox);

	// Title bar.
	bool can_expand = bool(EDITOR_GET("interface/editor/appearance/expand_to_title")) && DisplayServer::get_singleton()->has_feature(DisplayServerEnums::FEATURE_EXTEND_TO_TITLE);

	{
		title_bar = memnew(EditorTitleBar);
		main_vbox->add_child(title_bar);

		if (can_expand) {
			// Add spacer to avoid other controls under window minimize/maximize/close buttons (left side).
			left_menu_spacer = memnew(Control);
			left_menu_spacer->set_mouse_filter(Control::MOUSE_FILTER_PASS);
			title_bar->add_child(left_menu_spacer);
		}

		HBoxContainer *left_hbox = memnew(HBoxContainer);
		left_hbox->set_alignment(BoxContainer::ALIGNMENT_BEGIN);
		left_hbox->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		left_hbox->set_stretch_ratio(1.0);
		title_bar->add_child(left_hbox);

		title_bar_logo = memnew(Button);
		title_bar_logo->set_flat(true);
		title_bar_logo->set_tooltip_text(TTR("About Jundot"));
		left_hbox->add_child(title_bar_logo);
		title_bar_logo->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_show_about));

		bool global_menu = !bool(EDITOR_GET("interface/editor/appearance/use_embedded_menu")) && NativeMenu::get_singleton()->has_feature(NativeMenu::FEATURE_GLOBAL_MENU);
		if (global_menu) {
			MenuBar *main_menu_bar = memnew(MenuBar);
			main_menu_bar->set_start_index(0); // Main menu, add to the start of global menu.
			main_menu_bar->set_prefer_global_menu(true);
			left_hbox->add_child(main_menu_bar);

			if (NativeMenu::get_singleton()->has_system_menu(NativeMenu::WINDOW_MENU_ID)) {
				PopupMenu *window_menu = memnew(PopupMenu);
				window_menu->set_system_menu(NativeMenu::WINDOW_MENU_ID);
				window_menu->set_name(TTRC("Window"));
				main_menu_bar->add_child(window_menu);
			}
			if (NativeMenu::get_singleton()->has_system_menu(NativeMenu::HELP_MENU_ID)) {
				PopupMenu *help_menu = memnew(PopupMenu);
				help_menu->set_system_menu(NativeMenu::HELP_MENU_ID);
				help_menu->set_name(TTRC("Help"));
				main_menu_bar->add_child(help_menu);
			}
		}
		if (can_expand) {
			// Spacer to center main toggles.
			left_spacer = memnew(Control);
			left_spacer->set_mouse_filter(Control::MOUSE_FILTER_PASS);
			title_bar->add_child(left_spacer);
		}

		main_view_toggles = memnew(HBoxContainer);
		main_view_toggles->set_alignment(BoxContainer::ALIGNMENT_CENTER);
		main_view_toggles->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		main_view_toggles->set_stretch_ratio(2.0);
		title_bar->add_child(main_view_toggles);
		title_bar->set_center_control(main_view_toggles);

		if (can_expand) {
			// Spacer to center main toggles.
			right_spacer = memnew(Control);
			right_spacer->set_mouse_filter(Control::MOUSE_FILTER_PASS);
			title_bar->add_child(right_spacer);
		}

		main_view_toggles_group.instantiate();

		HBoxContainer *right_hbox = memnew(HBoxContainer);
		right_hbox->set_alignment(BoxContainer::ALIGNMENT_END);
		right_hbox->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		right_hbox->set_stretch_ratio(1.0);
		title_bar->add_child(right_hbox);

		quick_settings_button = memnew(Button);
		quick_settings_button->set_flat(true);
		quick_settings_button->set_text(TTRC("Settings"));
		right_hbox->add_child(quick_settings_button);
		quick_settings_button->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_show_quick_settings));

		if (can_expand) {
			// Add spacer to avoid other controls under the window minimize/maximize/close buttons (right side).
			right_menu_spacer = memnew(Control);
			right_menu_spacer->set_mouse_filter(Control::MOUSE_FILTER_PASS);
			title_bar->add_child(right_menu_spacer);
		}
	}

	main_view_container = memnew(PanelContainer);
	main_view_container->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	main_vbox->add_child(main_view_container);

	// Project list view.
	{
		local_projects_vb = memnew(VBoxContainer);
		local_projects_vb->set_name("LocalProjectsTab");
		_add_main_view(MAIN_VIEW_PROJECTS, TTRC("Projects"), Ref<Texture2D>(), local_projects_vb);

		// Project list's top bar.
		{
			HBoxContainer *hb = memnew(HBoxContainer);
			hb->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			local_projects_vb->add_child(hb);

			create_btn = memnew(Button);
			create_btn->set_text(TTRC("Create"));
			create_btn->set_shortcut(ED_SHORTCUT("project_manager/new_project", TTRC("New Project"), KeyModifierMask::CMD_OR_CTRL | Key::N));
			create_btn->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_new_project));
			hb->add_child(create_btn);

			import_btn = memnew(Button);
			import_btn->set_text(TTRC("Import"));
			import_btn->set_shortcut(ED_SHORTCUT("project_manager/import_project", TTRC("Import Project"), KeyModifierMask::CMD_OR_CTRL | Key::I));
			import_btn->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_import_project));
			hb->add_child(import_btn);

			scan_btn = memnew(Button);
			scan_btn->set_text(TTRC("Scan"));
			scan_btn->set_shortcut(ED_SHORTCUT("project_manager/scan_projects", TTRC("Scan Projects"), KeyModifierMask::CMD_OR_CTRL | Key::S));
			scan_btn->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_scan_projects));
			hb->add_child(scan_btn);

			loading_label = memnew(Label(TTRC("Loading, please wait...")));
			loading_label->set_accessibility_live(AccessibilityServerEnums::AccessibilityLiveMode::LIVE_ASSERTIVE);
			loading_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			loading_label->hide();
			hb->add_child(loading_label);

			search_box = memnew(LineEdit);
			search_box->set_placeholder(TTRC("Filter Projects"));
			search_box->set_accessibility_name(TTRC("Filter Projects"));
			search_box->set_tooltip_text(TTRC("This field filters projects by name and last path component.\nTo filter projects by name and full path, the query must contain at least one `/` character."));
			search_box->set_clear_button_enabled(true);
			search_box->connect(SceneStringName(text_changed), callable_mp(this, &ProjectManager::_on_search_term_changed));
			search_box->connect(SceneStringName(text_submitted), callable_mp(this, &ProjectManager::_on_search_term_submitted));
			search_box->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			hb->add_child(search_box);

			sort_label = memnew(Label);
			sort_label->set_text(TTRC("Sort:"));
			hb->add_child(sort_label);

			filter_option = memnew(OptionButton);
			filter_option->set_clip_text(true);
			filter_option->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			filter_option->set_stretch_ratio(0.3);
			filter_option->set_accessibility_name(TTRC("Sort:"));
			filter_option->connect(SceneStringName(item_selected), callable_mp(this, &ProjectManager::_on_order_option_changed));
			hb->add_child(filter_option);

			filter_option->add_item(TTRC("Last Edited"));
			filter_option->add_item(TTRC("Name"));
			filter_option->add_item(TTRC("Path"));
			filter_option->add_item(TTRC("Tags"));
		}

		// Project list and its sidebar.
		{
			HBoxContainer *project_list_hbox = memnew(HBoxContainer);
			local_projects_vb->add_child(project_list_hbox);
			project_list_hbox->set_v_size_flags(Control::SIZE_EXPAND_FILL);

			project_list_panel = memnew(PanelContainer);
			project_list_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			project_list_hbox->add_child(project_list_panel);

			project_list = memnew(ProjectList);
			project_list->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
			project_list_panel->add_child(project_list);
			project_list->connect(ProjectList::SIGNAL_LIST_CHANGED, callable_mp(this, &ProjectManager::_update_project_buttons));
			project_list->connect(ProjectList::SIGNAL_LIST_CHANGED, callable_mp(this, &ProjectManager::_update_list_placeholder));
			project_list->connect(ProjectList::SIGNAL_SELECTION_CHANGED, callable_mp(this, &ProjectManager::_update_project_buttons));
			project_list->connect(ProjectList::SIGNAL_PROJECT_ASK_OPEN, callable_mp(this, &ProjectManager::_open_selected_projects_check_recovery_mode));
			project_list->connect(ProjectList::SIGNAL_MENU_OPTION_SELECTED, callable_mp(this, &ProjectManager::_project_list_menu_option));

			// Empty project list placeholder.
			{
				empty_list_placeholder = memnew(VBoxContainer);
				empty_list_placeholder->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
				empty_list_placeholder->add_theme_constant_override("separation", 16 * EDSCALE);
				empty_list_placeholder->hide();
				project_list_panel->add_child(empty_list_placeholder);

				empty_list_message = memnew(RichTextLabel);
				empty_list_message->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
				empty_list_message->set_use_bbcode(true);
				empty_list_message->set_fit_content(true);
				empty_list_message->set_h_size_flags(SIZE_EXPAND_FILL);
				empty_list_message->add_theme_style_override(CoreStringName(normal), memnew(StyleBoxEmpty));

				empty_list_placeholder->add_child(empty_list_message);

				FlowContainer *empty_list_actions = memnew(FlowContainer);
				empty_list_actions->set_alignment(FlowContainer::ALIGNMENT_CENTER);
				empty_list_placeholder->add_child(empty_list_actions);

				empty_list_create_project = memnew(Button);
				empty_list_create_project->set_text(TTRC("Create New Project"));
				empty_list_create_project->set_theme_type_variation("PanelBackgroundButton");
				empty_list_actions->add_child(empty_list_create_project);
				empty_list_create_project->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_new_project));

				empty_list_import_project = memnew(Button);
				empty_list_import_project->set_text(TTRC("Import Existing Project"));
				empty_list_import_project->set_theme_type_variation("PanelBackgroundButton");
				empty_list_actions->add_child(empty_list_import_project);
				empty_list_import_project->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_import_project));

				empty_list_open_assetlib = memnew(Button);
				empty_list_open_assetlib->set_text(TTRC("Open Asset Store"));
				empty_list_open_assetlib->set_theme_type_variation("PanelBackgroundButton");
				empty_list_actions->add_child(empty_list_open_assetlib);
				empty_list_open_assetlib->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_open_asset_library_confirmed));

				empty_list_online_warning = memnew(Label);
				empty_list_online_warning->set_focus_mode(FOCUS_ACCESSIBILITY);
				empty_list_online_warning->set_horizontal_alignment(HorizontalAlignment::HORIZONTAL_ALIGNMENT_CENTER);
				empty_list_online_warning->set_custom_minimum_size(Size2(220, 0) * EDSCALE);
				empty_list_online_warning->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
				empty_list_online_warning->set_h_size_flags(Control::SIZE_EXPAND_FILL);
				empty_list_online_warning->set_text(TTRC("Note: The Asset Store requires an online connection and involves sending data over the internet."));
				empty_list_placeholder->add_child(empty_list_online_warning);
			}

			ai_project_chat_panel = memnew(PanelContainer);
			ai_project_chat_panel->set_custom_minimum_size(Size2(320, 120) * EDSCALE);
			ai_project_chat_panel->set_h_size_flags(Control::SIZE_FILL);
			project_list_hbox->add_child(ai_project_chat_panel);

			MarginContainer *ai_project_chat_margin = memnew(MarginContainer);
			ai_project_chat_margin->add_theme_constant_override("margin_left", 10 * EDSCALE);
			ai_project_chat_margin->add_theme_constant_override("margin_top", 10 * EDSCALE);
			ai_project_chat_margin->add_theme_constant_override("margin_right", 10 * EDSCALE);
			ai_project_chat_margin->add_theme_constant_override("margin_bottom", 10 * EDSCALE);
			ai_project_chat_panel->add_child(ai_project_chat_margin);

			VBoxContainer *ai_project_chat_vbox = memnew(VBoxContainer);
			ai_project_chat_vbox->add_theme_constant_override("separation", 8 * EDSCALE);
			ai_project_chat_margin->add_child(ai_project_chat_vbox);

			HBoxContainer *ai_project_chat_header = memnew(HBoxContainer);
			ai_project_chat_vbox->add_child(ai_project_chat_header);

			Label *ai_project_chat_title = memnew(Label(TTRC("Jundot AI")));
			ai_project_chat_title->add_theme_font_size_override("font_size", 15 * EDSCALE);
			ai_project_chat_title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			ai_project_chat_header->add_child(ai_project_chat_title);

			ai_project_settings_btn = memnew(Button);
			ai_project_settings_btn->set_text(TTRC("AI Settings"));
			ai_project_settings_btn->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_show_ai_config_dialog));
			ai_project_chat_header->add_child(ai_project_settings_btn);

			ai_project_clear_btn = memnew(Button);
			ai_project_clear_btn->set_text(TTRC("Clear"));
			ai_project_clear_btn->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_ai_project_clear_chat));
			ai_project_chat_header->add_child(ai_project_clear_btn);

			ai_project_summary_label = memnew(Label(TTRC("Tell me what you want to create.")));
			ai_project_summary_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
			ai_project_chat_vbox->add_child(ai_project_summary_label);

			ai_project_chat_log = memnew(RichTextLabel);
			ai_project_chat_log->set_fit_content(false);
			ai_project_chat_log->set_scroll_active(true);
			ai_project_chat_log->set_selection_enabled(true);
			ai_project_chat_log->set_use_bbcode(true);
			ai_project_chat_log->set_v_size_flags(Control::SIZE_EXPAND_FILL);
			ai_project_chat_vbox->add_child(ai_project_chat_log);

			ai_project_questions_panel = memnew(PanelContainer);
			ai_project_questions_panel->hide();
			ai_project_chat_vbox->add_child(ai_project_questions_panel);

			MarginContainer *ai_project_questions_margin = memnew(MarginContainer);
			ai_project_questions_margin->add_theme_constant_override("margin_left", 8 * EDSCALE);
			ai_project_questions_margin->add_theme_constant_override("margin_top", 8 * EDSCALE);
			ai_project_questions_margin->add_theme_constant_override("margin_right", 8 * EDSCALE);
			ai_project_questions_margin->add_theme_constant_override("margin_bottom", 8 * EDSCALE);
			ai_project_questions_panel->add_child(ai_project_questions_margin);

			VBoxContainer *ai_project_questions_vbox = memnew(VBoxContainer);
			ai_project_questions_vbox->add_theme_constant_override("separation", 6 * EDSCALE);
			ai_project_questions_margin->add_child(ai_project_questions_vbox);

			HBoxContainer *ai_project_question_nav = memnew(HBoxContainer);
			ai_project_questions_vbox->add_child(ai_project_question_nav);

			ai_project_question_prev_btn = memnew(Button);
			ai_project_question_prev_btn->set_text(TTRC("Previous"));
			ai_project_question_prev_btn->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_ai_project_question_prev));
			ai_project_question_nav->add_child(ai_project_question_prev_btn);

			ai_project_question_title = memnew(Label);
			ai_project_question_title->set_horizontal_alignment(HorizontalAlignment::HORIZONTAL_ALIGNMENT_CENTER);
			ai_project_question_title->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			ai_project_question_nav->add_child(ai_project_question_title);

			ai_project_question_next_btn = memnew(Button);
			ai_project_question_next_btn->set_text(TTRC("Next"));
			ai_project_question_next_btn->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_ai_project_question_next));
			ai_project_question_nav->add_child(ai_project_question_next_btn);

			ai_project_question_text = memnew(Label);
			ai_project_question_text->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
			ai_project_questions_vbox->add_child(ai_project_question_text);

			ai_project_question_options = memnew(VBoxContainer);
			ai_project_question_options->add_theme_constant_override("separation", 2 * EDSCALE);
			ai_project_questions_vbox->add_child(ai_project_question_options);

			ai_project_question_custom = memnew(LineEdit);
			ai_project_question_custom->set_placeholder(TTRC("Custom answer"));
			ai_project_question_custom->connect(SceneStringName(text_changed), callable_mp(this, &ProjectManager::_ai_project_question_custom_changed));
			ai_project_questions_vbox->add_child(ai_project_question_custom);

			ai_project_question_submit_btn = memnew(Button);
			ai_project_question_submit_btn->set_text(TTRC("Submit Answers"));
			ai_project_question_submit_btn->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_ai_project_question_submit));
			ai_project_questions_vbox->add_child(ai_project_question_submit_btn);

			HBoxContainer *ai_project_chat_input_hbox = memnew(HBoxContainer);
			ai_project_chat_vbox->add_child(ai_project_chat_input_hbox);

			ai_project_chat_input = memnew(LineEdit);
			ai_project_chat_input->set_placeholder(TTRC("Project idea"));
			ai_project_chat_input->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			ai_project_chat_input->connect(SceneStringName(text_submitted), callable_mp(this, &ProjectManager::_ai_project_chat_send).unbind(1));
			ai_project_chat_input_hbox->add_child(ai_project_chat_input);

			ai_project_chat_send_btn = memnew(Button);
			ai_project_chat_send_btn->set_text(TTRC("Send"));
			ai_project_chat_send_btn->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_ai_project_chat_send));
			ai_project_chat_input_hbox->add_child(ai_project_chat_send_btn);

			ai_project_create_btn = memnew(Button);
			ai_project_create_btn->set_text(TTRC("Create Project"));
			ai_project_create_btn->set_disabled(true);
			ai_project_create_btn->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_ai_project_create_from_summary));
			ai_project_chat_vbox->add_child(ai_project_create_btn);

			ai_project_chat_service = memnew(AIChatService);
			ai_project_chat_service->set_name("ProjectManagerAIChatService");
			add_child(ai_project_chat_service);
			ai_project_chat_service->connect("chat_completed", callable_mp(this, &ProjectManager::_ai_project_chat_completed));
			ai_project_chat_service->connect("chat_stream_data", callable_mp(this, &ProjectManager::_ai_project_chat_stream_data));

			_ai_project_chat_append(TTR("Jundot AI"), TTR("Tell me the type, theme, and core content of the project. I will summarize it and propose an English project name."));

			// The side bar with the edit, run, rename, etc. buttons.
			VBoxContainer *project_list_sidebar = memnew(VBoxContainer);
			project_list_sidebar->set_custom_minimum_size(Size2(120, 120));
			project_list_hbox->add_child(project_list_sidebar);

			project_list_sidebar->add_child(memnew(HSeparator));

			ScrollContainer *sidebar_scroll_containter = memnew(ScrollContainer);
			sidebar_scroll_containter->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
			sidebar_scroll_containter->set_v_size_flags(Control::SIZE_EXPAND_FILL);
			project_list_sidebar->add_child(sidebar_scroll_containter);
			VBoxContainer *sidebar_buttons_containter = memnew(VBoxContainer);
			sidebar_scroll_containter->add_child(sidebar_buttons_containter);

			open_btn_container = memnew(HBoxContainer);
			open_btn_container->set_anchors_preset(Control::PRESET_FULL_RECT);
			sidebar_buttons_containter->add_child(open_btn_container);

			open_btn = memnew(Button);
			open_btn->set_text(TTRC("Edit"));
			open_btn->set_shortcut(ED_SHORTCUT("project_manager/edit_project", TTRC("Edit Project"), KeyModifierMask::CMD_OR_CTRL | Key::E));
			open_btn->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_open_selected_projects_check_recovery_mode));
			open_btn->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			open_btn_container->add_child(open_btn);

			open_btn_container->add_child(memnew(VSeparator));

			open_options_btn = memnew(Button);
			open_options_btn->set_accessibility_name(TTRC("Options"));
			open_options_btn->set_icon_alignment(HorizontalAlignment::HORIZONTAL_ALIGNMENT_CENTER);
			open_options_btn->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_open_options_popup));
			open_btn_container->add_child(open_options_btn);

			open_options_popup = memnew(PopupMenu);
			open_options_popup->add_item(TTRC("Edit in verbose mode"));
			open_options_popup->add_item(TTRC("Edit in recovery mode"));
			open_options_popup->connect(SceneStringName(id_pressed), callable_mp(this, &ProjectManager::_on_open_options_selected));
			open_options_btn->add_child(open_options_popup);

			open_btn_container->set_custom_minimum_size(Size2(120, open_btn->get_combined_minimum_size().y));

			run_btn = memnew(Button);
			run_btn->set_text(TTRC("Run"));
			run_btn->set_shortcut(ED_SHORTCUT("project_manager/run_project", TTRC("Run Project"), KeyModifierMask::CMD_OR_CTRL | Key::R));
			run_btn->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_run_project));
			sidebar_buttons_containter->add_child(run_btn);

			rename_btn = memnew(Button);
			rename_btn->set_text(TTRC("Rename"));
			// The F2 shortcut isn't overridden with Enter on macOS as Enter is already used to edit a project.
			rename_btn->set_shortcut(ED_SHORTCUT("project_manager/rename_project", TTRC("Rename Project"), Key::F2));
			rename_btn->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_rename_project));
			sidebar_buttons_containter->add_child(rename_btn);

			duplicate_btn = memnew(Button);
			duplicate_btn->set_text(TTRC("Duplicate"));
			duplicate_btn->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_duplicate_project));
			sidebar_buttons_containter->add_child(duplicate_btn);

			manage_tags_btn = memnew(Button);
			manage_tags_btn->set_text(TTRC("Manage Tags"));
			manage_tags_btn->set_shortcut(ED_SHORTCUT("project_manager/project_tags", TTRC("Manage Tags"), KeyModifierMask::CMD_OR_CTRL | Key::T));
			sidebar_buttons_containter->add_child(manage_tags_btn);

			erase_btn = memnew(Button);
			erase_btn->set_text(TTRC("Remove"));
			erase_btn->set_shortcut(ED_SHORTCUT("project_manager/remove_project", TTRC("Remove Project"), Key::KEY_DELETE));
			erase_btn->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_erase_project));
			sidebar_buttons_containter->add_child(erase_btn);

			erase_missing_btn = memnew(Button);
			erase_missing_btn->set_text(TTRC("Remove Missing"));
			erase_missing_btn->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_erase_missing_projects));
			sidebar_buttons_containter->add_child(erase_missing_btn);

			donate_btn = memnew(Button);
			donate_btn->set_text(TTRC("Donate"));
			donate_btn->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_open_donate_page));
			project_list_sidebar->add_child(donate_btn);
		}
	}

	// Asset store view.
	if (AssetLibraryEditorPlugin::is_available()) {
		asset_library = memnew(EditorAssetLibrary(true));
		asset_library->set_name("AssetLibraryTab");
		_add_main_view(MAIN_VIEW_ASSETLIB, TTRC("Asset Store"), Ref<Texture2D>(), asset_library);
		asset_library->connect("install_asset", callable_mp(this, &ProjectManager::_install_project));
	} else {
		VBoxContainer *asset_library_filler = memnew(VBoxContainer);
		asset_library_filler->set_name("AssetLibraryTab");
		Button *asset_library_toggle = _add_main_view(MAIN_VIEW_ASSETLIB, TTRC("Asset Store"), Ref<Texture2D>(), asset_library_filler);
		asset_library_toggle->set_disabled(true);
		asset_library_toggle->set_tooltip_text(TTRC("Asset Store not available (due to using Web editor, or because SSL support disabled)."));
	}

	// Footer bar.
	{
		HBoxContainer *footer_bar = memnew(HBoxContainer);
		footer_bar->set_alignment(BoxContainer::ALIGNMENT_END);
		footer_bar->add_theme_constant_override("separation", 20 * EDSCALE);
		main_vbox->add_child(footer_bar);

#ifdef ENGINE_UPDATE_CHECK_ENABLED
		engine_update_label = memnew(EngineUpdateLabel);
		footer_bar->add_child(engine_update_label);
		engine_update_label->connect("offline_clicked", callable_mp(this, &ProjectManager::_show_quick_settings));
		engine_update_label->connect("update_download_requested", callable_mp(this, &ProjectManager::_on_update_download_requested));
#endif

		EditorVersionButton *version_btn = memnew(EditorVersionButton(EditorVersionButton::FORMAT_WITH_BUILD));
		// Fade the version label to be less prominent, but still readable.
		version_btn->set_self_modulate(Color(1, 1, 1, 0.6));
		footer_bar->add_child(version_btn);
	}

	// Dialogs.
	{
		quick_settings_dialog = memnew(QuickSettingsDialog);
		add_child(quick_settings_dialog);
		quick_settings_dialog->connect("restart_required", callable_mp(this, &ProjectManager::_restart_confirmed));
		quick_settings_dialog->connect("ai_source_manager_requested", callable_mp(this, &ProjectManager::_show_ai_source_manager));

		// Hot-update system.
		update_dialog = memnew(UpdateDialog);
		add_child(update_dialog);
		update_dialog->connect("update_now_requested", callable_mp(this, &ProjectManager::_on_update_now_requested));
		update_dialog->connect("skip_version_requested", callable_mp(this, &ProjectManager::_on_skip_version_requested));

		update_manager = memnew(UpdateManager);
		add_child(update_manager);
		update_manager->connect("update_launcher_started", callable_mp(this, &ProjectManager::_on_update_launcher_started));
		update_manager->connect("update_launcher_finished", callable_mp(this, &ProjectManager::_on_update_launcher_finished));

		scan_dir = memnew(EditorFileDialog);
		scan_dir->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
		scan_dir->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_DIR);
		scan_dir->set_title(TTRC("Select a Folder to Scan")); // Must be after mode or it's overridden.
		scan_dir->set_current_dir(EDITOR_GET("filesystem/directories/default_project_path"));
		add_child(scan_dir);
		scan_dir->connect("dir_selected", callable_mp(project_list, &ProjectList::find_projects));

		erase_missing_ask = memnew(ConfirmationDialog);
		erase_missing_ask->set_ok_button_text(TTRC("Remove All"));
		erase_missing_ask->get_ok_button()->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_erase_missing_projects_confirm));
		add_child(erase_missing_ask);

		erase_ask = memnew(ConfirmationDialog);
		erase_ask->set_ok_button_text(TTRC("Remove"));
		erase_ask->get_ok_button()->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_erase_project_confirm));
		add_child(erase_ask);

		VBoxContainer *erase_ask_vb = memnew(VBoxContainer);
		erase_ask->add_child(erase_ask_vb);

		erase_ask_label = memnew(Label);
		erase_ask_label->set_focus_mode(FOCUS_ACCESSIBILITY);
		erase_ask_vb->add_child(erase_ask_label);

		// Comment out for now until we have a better warning system to
		// ensure users delete their project only.
		//delete_project_contents = memnew(CheckBox);
		//delete_project_contents->set_text(TTRC("Also delete project contents (no undo!)"));
		//erase_ask_vb->add_child(delete_project_contents);

		multi_open_ask = memnew(ConfirmationDialog);
		multi_open_ask->set_ok_button_text(TTRC("Edit"));
		multi_open_ask->get_ok_button()->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_open_selected_projects));
		add_child(multi_open_ask);

		multi_run_ask = memnew(ConfirmationDialog);
		multi_run_ask->set_ok_button_text(TTRC("Run"));
		multi_run_ask->get_ok_button()->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_run_project_confirm));
		add_child(multi_run_ask);

		open_recovery_mode_ask = memnew(ConfirmationDialog);
		open_recovery_mode_ask->set_min_size(Size2(550, 70) * EDSCALE);
		open_recovery_mode_ask->set_autowrap(true);
		open_recovery_mode_ask->add_button(TTRC("Edit normally"))->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_on_recovery_mode_popup_open_normal));
		open_recovery_mode_ask->set_ok_button_text(TTRC("Edit in Recovery Mode"));
		open_recovery_mode_ask->get_ok_button()->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_on_recovery_mode_popup_open_recovery));
		add_child(open_recovery_mode_ask);

		ask_update_settings = memnew(ConfirmationDialog);
		add_child(ask_update_settings);
		ask_update_vb = memnew(VBoxContainer);
		ask_update_settings->add_child(ask_update_vb);
		ask_update_label = memnew(Label);
		ask_update_label->set_focus_mode(FOCUS_ACCESSIBILITY);
		ask_update_label->set_custom_minimum_size(Size2(300 * EDSCALE, 1));
		ask_update_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD);
		ask_update_label->set_v_size_flags(SIZE_EXPAND_FILL);
		ask_update_vb->add_child(ask_update_label);
		ask_update_backup = memnew(CheckBox);
		ask_update_backup->set_text(TTRC("Backup project first"));
		ask_update_backup->set_h_size_flags(SIZE_SHRINK_CENTER);
		ask_update_vb->add_child(ask_update_backup);
		ask_update_settings->get_ok_button()->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_open_selected_projects_with_migration));
		int ed_swap_cancel_ok = EDITOR_GET("interface/editor/appearance/accept_dialog_cancel_ok_buttons");
		if (ed_swap_cancel_ok == 0) {
			ed_swap_cancel_ok = DisplayServer::get_singleton()->get_swap_cancel_ok() ? 2 : 1;
		}
		full_convert_button = ask_update_settings->add_button(TTRC("Convert Full Project"), ed_swap_cancel_ok != 2);
		full_convert_button->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_full_convert_button_pressed));
		migration_guide_button = ask_update_settings->add_button(TTRC("See Migration Guide"), ed_swap_cancel_ok != 2);
		migration_guide_button->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_migration_guide_button_pressed));

		ask_full_convert_dialog = memnew(ConfirmationDialog);
		ask_full_convert_dialog->set_autowrap(true);
		ask_full_convert_dialog->set_text(TTRC("This option will perform full project conversion, updating scenes, resources and scripts from Jundot 3 to work in Jundot 4.\n\nNote that this is a best-effort conversion, i.e. it makes upgrading the project easier, but it will not open out-of-the-box and will still require manual adjustments.\n\nIMPORTANT: Make sure to backup your project before converting, as this operation makes it impossible to open it in older versions of Jundot."));
		ask_full_convert_dialog->connect(SceneStringName(confirmed), callable_mp(this, &ProjectManager::_perform_full_project_conversion));
		add_child(ask_full_convert_dialog);

		project_dialog = memnew(ProjectDialog);
		project_dialog->connect("projects_updated", callable_mp(this, &ProjectManager::_on_projects_updated));
		project_dialog->connect("project_created", callable_mp(this, &ProjectManager::_on_project_created));
		project_dialog->connect("project_duplicated", callable_mp(this, &ProjectManager::_on_project_duplicated));
		add_child(project_dialog);

		ai_config_dialog = memnew(AcceptDialog);
		ai_config_dialog->set_title(TTRC("AI Settings"));
		ai_config_dialog->set_min_size(Size2(640, 420) * EDSCALE);
		add_child(ai_config_dialog);

		ai_config_panel = memnew(AIConfigPanel);
		ai_config_panel->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		ai_config_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		ai_config_dialog->add_child(ai_config_panel);

		ai_usage_agreement_dialog = memnew(AIUsageAgreementDialog);
		ai_usage_agreement_dialog->connect(SNAME("agreement_accepted"), callable_mp(this, &ProjectManager::_ai_usage_agreement_accepted));
		ai_usage_agreement_dialog->connect(SNAME("agreement_rejected"), callable_mp(this, &ProjectManager::_ai_usage_agreement_rejected));
		add_child(ai_usage_agreement_dialog);

		error_dialog = memnew(AcceptDialog);
		error_dialog->set_title(TTRC("Error"));
		add_child(error_dialog);

		ai_source_prompt_dialog = memnew(ConfirmationDialog);
		ai_source_prompt_dialog->set_title(TTRC("AI Engine Source Cache"));
		ai_source_prompt_dialog->set_ok_button_text(TTRC("Manage Cache"));
		add_child(ai_source_prompt_dialog);

		VBoxContainer *ai_source_prompt_vb = memnew(VBoxContainer);
		ai_source_prompt_vb->add_theme_constant_override("separation", 8 * EDSCALE);
		ai_source_prompt_dialog->add_child(ai_source_prompt_vb);

		Label *ai_source_prompt_label = memnew(Label);
		ai_source_prompt_label->set_text(TTRC("JunDot AI can read and modify engine source code when a local Git source cache is available. The published editor does not include engine source code. Use the source manager to clone or update the fixed JunDot engine repository for AI use. The cache will be encrypted automatically after Git updates."));
		ai_source_prompt_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		ai_source_prompt_label->set_custom_minimum_size(Size2(460, 0) * EDSCALE);
		ai_source_prompt_vb->add_child(ai_source_prompt_label);

		ai_source_prompt_dialog->connect(SceneStringName(confirmed), callable_mp(this, &ProjectManager::_open_ai_source_manager_from_prompt));

		ai_source_manager_dialog = memnew(AISourceManager);
		add_child(ai_source_manager_dialog);

		about_dialog = memnew(EditorAbout);
		add_child(about_dialog);
	}

	// Tag management.
	{
		tag_manage_dialog = memnew(ConfirmationDialog);
		add_child(tag_manage_dialog);
		tag_manage_dialog->set_title(TTRC("Manage Project Tags"));
		tag_manage_dialog->get_ok_button()->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_apply_project_tags));
		manage_tags_btn->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_manage_project_tags));

		VBoxContainer *tag_vb = memnew(VBoxContainer);
		tag_manage_dialog->add_child(tag_vb);

		Label *label = memnew(Label(TTRC("Project Tags")));
		tag_vb->add_child(label);
		label->set_theme_type_variation("HeaderMedium");
		label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);

		label = memnew(Label(TTRC("Click tag to remove it from the project.")));
		tag_vb->add_child(label);
		label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);

		project_tags = memnew(HFlowContainer);
		tag_vb->add_child(project_tags);
		project_tags->set_custom_minimum_size(Vector2(0, 100) * EDSCALE);

		tag_vb->add_child(memnew(HSeparator));

		label = memnew(Label(TTRC("All Tags")));
		tag_vb->add_child(label);
		label->set_theme_type_variation("HeaderMedium");
		label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);

		label = memnew(Label(TTRC("Click tag to add it to the project.")));
		tag_vb->add_child(label);
		label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);

		all_tags = memnew(HFlowContainer);
		tag_vb->add_child(all_tags);
		all_tags->set_custom_minimum_size(Vector2(0, 100) * EDSCALE);

		tag_edit_error = memnew(Label);
		tag_vb->add_child(tag_edit_error);
		tag_edit_error->set_autowrap_mode(TextServer::AUTOWRAP_WORD);

		create_tag_dialog = memnew(ConfirmationDialog);
		tag_manage_dialog->add_child(create_tag_dialog);
		create_tag_dialog->set_title(TTRC("Create New Tag"));
		create_tag_dialog->get_ok_button()->connect(SceneStringName(pressed), callable_mp(this, &ProjectManager::_create_new_tag));

		tag_vb = memnew(VBoxContainer);
		create_tag_dialog->add_child(tag_vb);

		Label *info = memnew(Label(TTRC("Tags are capitalized automatically when displayed.")));
		tag_vb->add_child(info);

		new_tag_name = memnew(LineEdit);
		tag_vb->add_child(new_tag_name);
		new_tag_name->set_accessibility_name(TTRC("New Tag Name"));
		new_tag_name->set_placeholder(TTRC("example_tag (will display as Example Tag)"));
		new_tag_name->connect(SceneStringName(text_changed), callable_mp(this, &ProjectManager::_set_new_tag_name));
		new_tag_name->connect(SceneStringName(text_submitted), callable_mp(this, &ProjectManager::_create_new_tag).unbind(1));
		create_tag_dialog->connect("about_to_popup", callable_mp(new_tag_name, &LineEdit::clear));
		create_tag_dialog->connect("about_to_popup", callable_mp((Control *)new_tag_name, &Control::grab_focus).bind(false), CONNECT_DEFERRED);

		tag_error = memnew(Label);
		tag_error->set_focus_mode(FOCUS_ACCESSIBILITY);
		tag_vb->add_child(tag_error);

		create_tag_btn = memnew(Button);
		create_tag_btn->set_accessibility_name(TTRC("Create Tag"));
		all_tags->add_child(create_tag_btn);
		create_tag_btn->connect(SceneStringName(pressed), callable_mp((Window *)create_tag_dialog, &Window::popup_centered).bind(Vector2i(500, 0) * EDSCALE));

		_set_new_tag_name("");
	}

	// Initialize project list.
	{
		project_list->load_project_list();

		Ref<DirAccess> dir_access = DirAccess::create(DirAccess::AccessType::ACCESS_FILESYSTEM);

		String default_project_path = EDITOR_GET("filesystem/directories/default_project_path");
		if (!default_project_path.is_empty() && !dir_access->dir_exists(default_project_path)) {
			Error error = dir_access->make_dir_recursive(default_project_path);
			if (error != OK) {
				ERR_PRINT("Could not create default project directory at: " + default_project_path);
			}
		}

		String autoscan_path = EDITOR_GET("filesystem/directories/autoscan_project_path");
		if (!autoscan_path.is_empty()) {
			if (dir_access->dir_exists(autoscan_path)) {
				project_list->find_projects(autoscan_path);
			} else {
				Error error = dir_access->make_dir_recursive(autoscan_path);
				if (error != OK) {
					ERR_PRINT("Could not create project autoscan directory at: " + autoscan_path);
				}
			}
		}
		project_list->update_project_list();
		initialized = true;
	}

	// Extend menu bar to window title.
	if (can_expand) {
		DisplayServer::get_singleton()->process_events();
		DisplayServer::get_singleton()->window_set_flag(DisplayServerEnums::WINDOW_FLAG_EXTEND_TO_TITLE, true, DisplayServerEnums::MAIN_WINDOW_ID);
		title_bar->set_can_move_window(true);
		title_bar->connect(SceneStringName(item_rect_changed), callable_mp(this, &ProjectManager::_titlebar_resized));
	}

	_update_size_limits();
}

ProjectManager::~ProjectManager() {
	singleton = nullptr;
	EditorInspector::cleanup_plugins();

#if defined(MODULE_GDSCRIPT_ENABLED) || defined(MODULE_MONO_ENABLED)
	EditorHelpHighlighter::free_singleton();
#endif

	if (EditorSettings::get_singleton()) {
		EditorSettings::destroy();
	}

	EditorThemeManager::finalize();
}
