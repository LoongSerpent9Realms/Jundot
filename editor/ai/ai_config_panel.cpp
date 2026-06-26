/*  ai_config_panel.cpp                                                   */
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

#include "ai_config_panel.h"

#include "editor/ai/ai_source_update_service.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "editor/ai/ai_chat_service.h"
#include "editor/ai/ai_develop_flow.h"
#include "editor/ai/ai_mcp_manager.h"
#include "editor/ai/ai_settings.h"
#include "editor/ai/ai_usage_agreement_dialog.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/file_dialog.h"
#include "scene/gui/flow_container.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/text_edit.h"
#include "scene/main/http_request.h"

#ifdef WINDOWS_ENABLED
#include <windows.h>
#undef FAILED
#endif

static constexpr int OUTPUT_LANGUAGE_AUTO = 0;
static constexpr int OUTPUT_LANGUAGE_ENGLISH = 1;
static constexpr int OUTPUT_LANGUAGE_SIMPLIFIED_CHINESE = 2;
static constexpr int OUTPUT_LANGUAGE_TRADITIONAL_CHINESE = 3;
static constexpr int OUTPUT_LANGUAGE_JAPANESE = 4;
static constexpr int OUTPUT_LANGUAGE_KOREAN = 5;
static constexpr int OUTPUT_LANGUAGE_SPANISH = 6;
static constexpr int OUTPUT_LANGUAGE_FRENCH = 7;
static constexpr int OUTPUT_LANGUAGE_GERMAN = 8;
static constexpr int BACKEND_TYPE_JUNDOT_PLUGIN = 0;
static constexpr int BACKEND_TYPE_LEGACY_OPENAI = 1;

struct ExternalMCPAppTarget {
	String name;
	String path;
};

static void _append_external_mcp_target(Vector<ExternalMCPAppTarget> &r_targets, const String &p_name, const String &p_path) {
	ExternalMCPAppTarget target;
	target.name = p_name;
	target.path = p_path;
	r_targets.push_back(target);
}

static bool _is_engine_source_root(const String &p_path) {
	return !p_path.is_empty() && FileAccess::exists(p_path.path_join("SConstruct"));
}

static String _find_engine_source_root_in_cache(const String &p_cache_path) {
	if (_is_engine_source_root(p_cache_path)) {
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
			if (_is_engine_source_root(candidate)) {
				dir->list_dir_end();
				return candidate;
			}
		}
		name = dir->get_next();
	}
	dir->list_dir_end();
	return String();
}

static Error _collect_engine_source_cache_entries(const String &p_path, List<String> &r_dirs, List<String> &r_files) {
	Error open_error = OK;
	Ref<DirAccess> dir = DirAccess::open(p_path, &open_error);
	if (dir.is_null()) {
		if (FileAccess::exists(p_path)) {
			r_files.push_back(p_path);
			return OK;
		}
		return open_error != OK ? open_error : ERR_FILE_NOT_FOUND;
	}

	r_dirs.push_back(p_path);

	dir->list_dir_begin();
	String name = dir->get_next();
	while (!name.is_empty()) {
		if (name != "." && name != "..") {
			const String child_path = p_path.path_join(name);
			if (dir->current_is_dir() && !dir->is_link(name)) {
				const Error err = _collect_engine_source_cache_entries(child_path, r_dirs, r_files);
				if (err != OK) {
					dir->list_dir_end();
					return err;
				}
			} else {
				r_files.push_back(child_path);
			}
		}
		name = dir->get_next();
	}
	dir->list_dir_end();
	return OK;
}

static Error _encrypt_engine_source_cache(const String &p_cache_path, String &r_error) {
#ifdef WINDOWS_ENABLED
	if (p_cache_path.is_empty()) {
		r_error = TTR("Engine source cache path is empty.");
		return ERR_INVALID_PARAMETER;
	}

	List<String> dirs;
	List<String> files;
	Error err = _collect_engine_source_cache_entries(p_cache_path, dirs, files);
	if (err != OK) {
		r_error = vformat(TTR("Could not scan source cache before encryption: error %d"), err);
		return err;
	}

	auto encrypt_path = [&](const String &p_path) -> Error {
		const Char16String path_utf16 = p_path.utf16();
		DWORD encryption_status = 0;
		if (FileEncryptionStatusW((LPCWSTR)path_utf16.get_data(), &encryption_status) && encryption_status == FILE_IS_ENCRYPTED) {
			return OK;
		}

		FileAccess::set_read_only_attribute(p_path, false);
		if (!EncryptFileW((LPCWSTR)path_utf16.get_data())) {
			const DWORD windows_error = GetLastError();
			r_error = vformat(TTR("Could not encrypt path: %s (Windows error %d)"), p_path, (int)windows_error);
			return FAILED;
		}
		return OK;
	};

	for (const String &E : dirs) {
		err = encrypt_path(E);
		if (err != OK) {
			return err;
		}
	}
	for (const String &E : files) {
		err = encrypt_path(E);
		if (err != OK) {
			return err;
		}
	}

	return OK;
#else
	r_error = "Automatic encrypted source cache is only implemented on Windows.";
	return ERR_UNAVAILABLE;
#endif
}

static String _output_language_from_id(int p_id) {
	switch (p_id) {
		case OUTPUT_LANGUAGE_ENGLISH:
			return "English";
		case OUTPUT_LANGUAGE_SIMPLIFIED_CHINESE:
			return "Simplified Chinese";
		case OUTPUT_LANGUAGE_TRADITIONAL_CHINESE:
			return "Traditional Chinese";
		case OUTPUT_LANGUAGE_JAPANESE:
			return "Japanese";
		case OUTPUT_LANGUAGE_KOREAN:
			return "Korean";
		case OUTPUT_LANGUAGE_SPANISH:
			return "Spanish";
		case OUTPUT_LANGUAGE_FRENCH:
			return "French";
		case OUTPUT_LANGUAGE_GERMAN:
			return "German";
		case OUTPUT_LANGUAGE_AUTO:
		default:
			return "auto";
	}
}

static int _output_language_to_id(const String &p_language) {
	const String language = p_language.strip_edges();
	if (language == "English") {
		return OUTPUT_LANGUAGE_ENGLISH;
	}
	if (language == "Simplified Chinese") {
		return OUTPUT_LANGUAGE_SIMPLIFIED_CHINESE;
	}
	if (language == "Traditional Chinese") {
		return OUTPUT_LANGUAGE_TRADITIONAL_CHINESE;
	}
	if (language == "Japanese") {
		return OUTPUT_LANGUAGE_JAPANESE;
	}
	if (language == "Korean") {
		return OUTPUT_LANGUAGE_KOREAN;
	}
	if (language == "Spanish") {
		return OUTPUT_LANGUAGE_SPANISH;
	}
	if (language == "French") {
		return OUTPUT_LANGUAGE_FRENCH;
	}
	if (language == "German") {
		return OUTPUT_LANGUAGE_GERMAN;
	}
	return OUTPUT_LANGUAGE_AUTO;
}

static AIBackendType _backend_type_from_id(int p_id) {
	return p_id == BACKEND_TYPE_LEGACY_OPENAI ? AIBackendType::LEGACY_OPENAI : AIBackendType::JUNDOT_PLUGIN;
}

static int _backend_type_to_id(AIBackendType p_backend_type) {
	return p_backend_type == AIBackendType::LEGACY_OPENAI ? BACKEND_TYPE_LEGACY_OPENAI : BACKEND_TYPE_JUNDOT_PLUGIN;
}

static String _get_external_mcp_base_url(const String &p_bind_address, int p_port) {
	const String host = p_bind_address.strip_edges().is_empty() ? String("127.0.0.1") : p_bind_address.strip_edges();
	return vformat("http://%s:%d/api/mcp", host, p_port);
}

static Dictionary _make_external_mcp_config_root(const String &p_base_url, bool p_enabled) {
	Dictionary mcp;
	mcp["http"] = p_base_url;
	mcp["health"] = p_base_url + "/health";
	mcp["tools"] = p_base_url + "/tools";
	mcp["servers"] = p_base_url + "/servers";
	mcp["call"] = p_base_url + "/call";

	Dictionary jundot_server;
	jundot_server["type"] = "http";
	jundot_server["url"] = p_base_url;
	jundot_server["enabled"] = p_enabled;

	Dictionary mcp_servers;
	mcp_servers["jundot"] = jundot_server;

	Dictionary root;
	root["mcp"] = mcp;
	root["mcpServers"] = mcp_servers;
	root["enabled"] = p_enabled;
	return root;
}

static Error _write_external_ai_mcp_config(const String &p_path, const String &p_base_url) {
	Dictionary root;
	if (FileAccess::exists(p_path)) {
		Error read_err = OK;
		const String content = FileAccess::get_file_as_string(p_path, &read_err);
		if (read_err == OK && !content.is_empty()) {
			JSON json;
			if (json.parse(content) == OK && json.get_data().get_type() == Variant::DICTIONARY) {
				root = json.get_data();
			}
		}
	}

	Variant existing_servers = root.get("mcpServers", Dictionary());
	Dictionary mcp_servers;
	if (existing_servers.get_type() == Variant::DICTIONARY) {
		mcp_servers = existing_servers;
	}

	Dictionary jundot_server;
	jundot_server["type"] = "http";
	jundot_server["url"] = p_base_url;
	jundot_server["enabled"] = true;
	mcp_servers["jundot"] = jundot_server;
	root["mcpServers"] = mcp_servers;

	Error err = DirAccess::make_dir_recursive_absolute(p_path.get_base_dir());
	ERR_FAIL_COND_V(err != OK, err);

	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);
	ERR_FAIL_COND_V(err != OK || file.is_null(), err != OK ? err : ERR_CANT_OPEN);

	file->store_string(JSON::stringify(root, "\t"));
	return OK;
}

void AIConfigPanel::_bind_methods() {
}

void AIConfigPanel::_notification(int p_what) {
	if (p_what == NOTIFICATION_TRANSLATION_CHANGED) {
		_update_translations();
	} else if (p_what == NOTIFICATION_READY || p_what == NOTIFICATION_RESIZED) {
		_update_adaptive_layout();
	}
}

void AIConfigPanel::_update_adaptive_layout() {
	if (!settings_grid || !engine_source_path_grid || !external_mcp_config_edit || !system_prompt_edit || !user_extra_instructions_edit) {
		return;
	}

	const bool use_compact_layout = get_size().x < 560.0f * EDSCALE;
	if (compact_layout != use_compact_layout) {
		compact_layout = use_compact_layout;
		settings_grid->set_columns(compact_layout ? 1 : 2);
		engine_source_path_grid->set_columns(compact_layout ? 1 : 2);
	}

	const float available_height = MAX(get_size().y, 320.0f * EDSCALE);
	external_mcp_config_edit->set_custom_minimum_size(Size2(0, CLAMP(available_height * 0.18f, 90.0f * EDSCALE, 150.0f * EDSCALE)));
	system_prompt_edit->set_custom_minimum_size(Size2(0, CLAMP(available_height * 0.16f, 90.0f * EDSCALE, 130.0f * EDSCALE)));
	user_extra_instructions_edit->set_custom_minimum_size(Size2(0, CLAMP(available_height * 0.14f, 80.0f * EDSCALE, 110.0f * EDSCALE)));
}

LineEdit *AIConfigPanel::_add_line_edit_row(GridContainer *p_grid, Label **r_label, const String &p_label, const String &p_placeholder, bool p_secret) {
	Label *label = memnew(Label);
	label->set_text(p_label);
	label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	p_grid->add_child(label);
	*r_label = label;

	LineEdit *edit = memnew(LineEdit);
	edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	edit->set_placeholder(p_placeholder);
	edit->set_secret(p_secret);
	p_grid->add_child(edit);
	return edit;
}

SpinBox *AIConfigPanel::_add_spin_box_row(GridContainer *p_grid, Label **r_label, const String &p_label, double p_min, double p_max, double p_step) {
	Label *label = memnew(Label);
	label->set_text(p_label);
	label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	p_grid->add_child(label);
	*r_label = label;

	SpinBox *spin = memnew(SpinBox);
	spin->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	spin->set_min(p_min);
	spin->set_max(p_max);
	spin->set_step(p_step);
	p_grid->add_child(spin);
	return spin;
}

OptionButton *AIConfigPanel::_add_backend_type_row(GridContainer *p_grid, Label **r_label, const String &p_label) {
	Label *label = memnew(Label);
	label->set_text(p_label);
	label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	p_grid->add_child(label);
	*r_label = label;

	OptionButton *option = memnew(OptionButton);
	option->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	option->add_item(TTR("jundot Plugin (MiMoCode)"), BACKEND_TYPE_JUNDOT_PLUGIN);
	option->add_item(TTR("Legacy OpenAI-Compatible"), BACKEND_TYPE_LEGACY_OPENAI);
	p_grid->add_child(option);
	return option;
}

OptionButton *AIConfigPanel::_add_output_language_row(GridContainer *p_grid, Label **r_label, const String &p_label) {
	Label *label = memnew(Label);
	label->set_text(p_label);
	label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	p_grid->add_child(label);
	*r_label = label;

	OptionButton *option = memnew(OptionButton);
	option->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	option->add_item(TTR("System Language (Auto)"), OUTPUT_LANGUAGE_AUTO);
	option->add_item(TTR("English"), OUTPUT_LANGUAGE_ENGLISH);
	option->add_item(TTR("Chinese (Simplified)"), OUTPUT_LANGUAGE_SIMPLIFIED_CHINESE);
	option->add_item(TTR("Chinese (Traditional)"), OUTPUT_LANGUAGE_TRADITIONAL_CHINESE);
	option->add_item(TTR("Japanese"), OUTPUT_LANGUAGE_JAPANESE);
	option->add_item(TTR("Korean"), OUTPUT_LANGUAGE_KOREAN);
	option->add_item(TTR("Spanish"), OUTPUT_LANGUAGE_SPANISH);
	option->add_item(TTR("French"), OUTPUT_LANGUAGE_FRENCH);
	option->add_item(TTR("German"), OUTPUT_LANGUAGE_GERMAN);
	p_grid->add_child(option);
	return option;
}

void AIConfigPanel::_update_translations() {
	set_name(TTRC("Config"));
	title_label->set_text(TTR("AI Configuration"));
	backend_type_label->set_text(TTR("AI Backend"));
	jundot_plugin_id_label->set_text(TTR("jundot AI Plugin ID"));
	jundot_plugin_url_label->set_text(TTR("jundot AI Plugin URL"));
	base_url_label->set_text(TTR("Base URL"));
	model_label->set_text(TTR("Model"));
	api_key_label->set_text(TTR("API Key"));
	temperature_label->set_text(TTR("Temperature"));
	max_tokens_label->set_text(TTR("Max Tokens"));
	context_char_budget_label->set_text(TTR("Context Budget"));
	history_budget_label->set_text(TTR("Compressed Context Size"));
	max_tool_iterations_label->set_text(TTR("Max Tool Call Iterations"));
	feature_universality_threshold_label->set_text(TTR("Feature Universality Threshold (%)"));
	feature_necessity_threshold_label->set_text(TTR("Feature Necessity Threshold"));
	output_language_label->set_text(TTR("AI Output Language"));
	user_extra_instructions_label->set_text(TTR("Extra Instructions (appended to system prompt)"));
	include_project_memories_check->set_text(TTR("Include project memories"));
	include_tool_context_check->set_text(TTR("Include skill and MCP context"));
	tools_enabled_check->set_text(TTR("Enable Function Calling tools (read/write files, build, etc.)"));
	develop_mode_check->set_text(TTR("Develop Mode (run local workflow, never commit or push)"));
	develop_mode_check->set_tooltip_text(TTR("Demonstrates modify, build, restart, user verification, AI verification, and upload validation. Git commit and push are always skipped."));
	mcp_tools_enabled_check->set_text(TTR("Enable MCP server tools (external services)"));
	auto_suggest_entries_check->set_text(TTR("Allow AI to suggest Skill/MCP/Memory entries"));
	feature_design_philosophy_check->set_text(TTR("Require Jundot design philosophy check for feature expansion"));
	external_api_enabled_check->set_text(TTR("Enable External API Server (for remote MCP tool calls)"));
	external_mcp_config_label->set_text(TTR("External AI MCP Config"));
	usage_notice_label->set_text(TTR("AI requests can consume additional API tokens when project context, attachments, logs, or repair analysis are included."));
	save_button->set_text(TTR("Save"));
	reset_button->set_text(TTR("Reset"));
	test_button->set_text(TTR("Test Connection"));
	view_agreement_button->set_text(TTR("View AI Usage Agreement"));
	reset_agreement_button->set_text(TTR("Reset Agreement Consent"));
	export_button->set_text(TTR("Export Config"));
	import_button->set_text(TTR("Import Config"));
	auto_configure_mcp_button->set_text(TTR("Auto Configure External MCP Apps"));
	mimocode_download_button->set_text(TTR("Download MiMoCode"));
	mimocode_download_button->set_tooltip_text(TTR("Open the packaged MiMoCode jundot plugin download page."));
	base_url_edit->set_placeholder(AISettings::get_default_base_url());
	jundot_plugin_id_edit->set_placeholder(JUNDOT_MIMOCODE_PLUGIN_ID);
	jundot_plugin_url_edit->set_placeholder("http://127.0.0.1:4096");
	model_edit->set_placeholder(AISettings::get_default_model());
	backend_type_option->set_item_text(backend_type_option->get_item_index(BACKEND_TYPE_JUNDOT_PLUGIN), TTR("jundot Plugin (MiMoCode)"));
	backend_type_option->set_item_text(backend_type_option->get_item_index(BACKEND_TYPE_LEGACY_OPENAI), TTR("Legacy OpenAI-Compatible"));
	output_language_option->set_item_text(output_language_option->get_item_index(OUTPUT_LANGUAGE_AUTO), TTR("System Language (Auto)"));
	output_language_option->set_item_text(output_language_option->get_item_index(OUTPUT_LANGUAGE_ENGLISH), TTR("English"));
	output_language_option->set_item_text(output_language_option->get_item_index(OUTPUT_LANGUAGE_SIMPLIFIED_CHINESE), TTR("Chinese (Simplified)"));
	output_language_option->set_item_text(output_language_option->get_item_index(OUTPUT_LANGUAGE_TRADITIONAL_CHINESE), TTR("Chinese (Traditional)"));
	output_language_option->set_item_text(output_language_option->get_item_index(OUTPUT_LANGUAGE_JAPANESE), TTR("Japanese"));
	output_language_option->set_item_text(output_language_option->get_item_index(OUTPUT_LANGUAGE_KOREAN), TTR("Korean"));
	output_language_option->set_item_text(output_language_option->get_item_index(OUTPUT_LANGUAGE_SPANISH), TTR("Spanish"));
	output_language_option->set_item_text(output_language_option->get_item_index(OUTPUT_LANGUAGE_FRENCH), TTR("French"));
	output_language_option->set_item_text(output_language_option->get_item_index(OUTPUT_LANGUAGE_GERMAN), TTR("German"));
	_update_external_mcp_config();
	_update_backend_controls();
}

void AIConfigPanel::_update_external_mcp_config() {
	if (!external_mcp_config_edit) {
		return;
	}

	const String bind_address = external_api_bind_address_edit ? external_api_bind_address_edit->get_text().strip_edges() : String("127.0.0.1");
	const int port = external_api_port_spin ? int(external_api_port_spin->get_value()) : 8080;
	const bool enabled = external_api_enabled_check && external_api_enabled_check->is_pressed();
	const String base_url = _get_external_mcp_base_url(bind_address, port);
	external_mcp_config_edit->set_text(JSON::stringify(_make_external_mcp_config_root(base_url, enabled), "\t"));
}

void AIConfigPanel::_update_backend_controls() {
	const bool use_mimocode = backend_type_option && _backend_type_from_id(backend_type_option->get_selected_id()) == AIBackendType::JUNDOT_PLUGIN;

	if (jundot_plugin_id_label) {
		jundot_plugin_id_label->set_visible(use_mimocode);
	}
	if (jundot_plugin_id_edit) {
		jundot_plugin_id_edit->set_visible(use_mimocode);
	}
	if (jundot_plugin_url_label) {
		jundot_plugin_url_label->set_visible(use_mimocode);
	}
	if (jundot_plugin_url_edit) {
		jundot_plugin_url_edit->set_visible(use_mimocode);
	}
	if (mimocode_download_button) {
		mimocode_download_button->set_visible(use_mimocode);
	}
	if (mimocode_download_spacer) {
		mimocode_download_spacer->set_visible(use_mimocode);
	}

	if (base_url_label) {
		base_url_label->set_visible(!use_mimocode);
	}
	if (base_url_edit) {
		base_url_edit->set_visible(!use_mimocode);
	}
	if (model_label) {
		model_label->set_visible(!use_mimocode);
	}
	if (model_edit) {
		model_edit->set_visible(!use_mimocode);
	}
	if (api_key_label) {
		api_key_label->set_visible(!use_mimocode);
	}
	if (api_key_edit) {
		api_key_edit->set_visible(!use_mimocode);
	}
}

void AIConfigPanel::_on_backend_type_selected(int p_index) {
	(void)p_index;
	_update_backend_controls();
}

void AIConfigPanel::_on_mimocode_download_button_pressed() {
	const Error err = OS::get_singleton()->shell_open(JUNDOT_MIMOCODE_RELEASES_URL);
	if (err != OK) {
		status_label->set_text(vformat(TTR("Could not open MiMoCode download page: %s"), JUNDOT_MIMOCODE_RELEASES_URL));
		return;
	}
	status_label->set_text(TTR("Opened the MiMoCode packaged plugin download page."));
}

void AIConfigPanel::_update_engine_source_status() {
	AISettingsData settings = AISettings::load();
	String cache_path = settings.engine_source_cache_root.strip_edges();
	if (cache_path.is_empty()) {
		cache_path = OS::get_singleton()->get_user_data_dir().path_join("engine_source");
	}

	String source_root = AISourceUpdateService::resolve_source_root();
	if (source_root.is_empty()) {
		source_root = _find_engine_source_root_in_cache(cache_path);
	}

	if (!source_root.is_empty()) {
		if (settings.engine_source_root != source_root || settings.engine_source_cache_root != cache_path || !settings.encrypt_engine_source_cache) {
			settings.engine_source_root = source_root;
			settings.engine_source_cache_root = cache_path;
			settings.encrypt_engine_source_cache = true;
			AISettings::save(settings);
		}

		String encryption_error;
		const Error encryption_err = _encrypt_engine_source_cache(cache_path, encryption_error);
		if (encryption_err == OK) {
			const AISourceUpdateStatus update_status = AISourceUpdateService::get_cached_status();
			const String update_text = update_status.source_root == source_root && !update_status.message.is_empty() ? update_status.message : TTR("Update: Not checked");
			engine_source_status_label->set_text(vformat(TTR("Status: Downloaded\nSource Root: %s\nEncryption: Enabled\n%s"), source_root, update_text));
		} else {
			const AISourceUpdateStatus update_status = AISourceUpdateService::get_cached_status();
			const String update_text = update_status.source_root == source_root && !update_status.message.is_empty() ? update_status.message : TTR("Update: Not checked");
			engine_source_status_label->set_text(vformat(TTR("Status: Downloaded\nSource Root: %s\nEncryption: Failed (%s)\n%s"), source_root, encryption_error, update_text));
		}
		engine_source_download_button->set_text(TTR("Re-download"));
		engine_source_delete_button->set_disabled(false);
		engine_source_update_button->set_disabled(false);
		const AISourceUpdateStatus update_status = AISourceUpdateService::get_cached_status();
		engine_source_update_button->set_text(update_status.source_root == source_root && update_status.state == AISourceUpdateStatus::UPDATE_AVAILABLE ? TTR("Update Now") : TTR("Check Updates"));
	} else {
		engine_source_status_label->set_text(TTR("Status: Not Downloaded"));
		engine_source_download_button->set_text(TTR("Download"));
		engine_source_delete_button->set_disabled(true);
		engine_source_update_button->set_disabled(true);
		engine_source_update_button->set_text(TTR("Check Updates"));
	}

	engine_source_cache_path_edit->set_text(cache_path);
}

void AIConfigPanel::_on_engine_source_update_button_pressed() {
	engine_source_update_button->set_disabled(true);
	const String source_root = AISourceUpdateService::resolve_source_root();
	const AISourceUpdateStatus cached_status = AISourceUpdateService::get_cached_status();
	if (cached_status.source_root == source_root && cached_status.state == AISourceUpdateStatus::UPDATE_AVAILABLE) {
		engine_source_status_label->set_text(TTR("Updating engine source and preserving local changes..."));
		AISourceUpdateStatus update_status = cached_status;
		AISourceUpdateService::update_source(update_status);
	} else {
		engine_source_status_label->set_text(TTR("Checking the engine source repository for updates..."));
		AISourceUpdateService::check_for_updates(true);
	}
	_update_engine_source_status();
}

void AIConfigPanel::_on_engine_source_browse_button_pressed() {
	String cache_path = engine_source_cache_path_edit->get_text().strip_edges();
	if (cache_path.is_empty()) {
		cache_path = OS::get_singleton()->get_user_data_dir().path_join("engine_source");
		engine_source_cache_path_edit->set_text(cache_path);
	}

	const Error mkdir_err = DirAccess::make_dir_recursive_absolute(cache_path);
	if (mkdir_err != OK) {
		status_label->set_text(vformat(TTR("Could not create cache directory: error %d"), mkdir_err));
		return;
	}

	AISettingsData settings = AISettings::load();
	settings.engine_source_cache_root = cache_path;
	AISettings::save(settings);
	_update_engine_source_status();

	const Error open_err = OS::get_singleton()->shell_open(cache_path);
	if (open_err != OK) {
		status_label->set_text(vformat(TTR("Could not open cache directory: %s"), cache_path));
	}
}

void AIConfigPanel::_on_engine_source_download_button_pressed() {
	String cache_path = engine_source_cache_path_edit->get_text().strip_edges();
	if (cache_path.is_empty()) {
		cache_path = OS::get_singleton()->get_user_data_dir().path_join("engine_source");
	}

	Error err = DirAccess::make_dir_recursive_absolute(cache_path);
	if (err != OK) {
		status_label->set_text(TTR("Could not create cache directory."));
		return;
	}

	AISettingsData settings = AISettings::load();
	settings.engine_source_cache_root = cache_path;
	AISettings::save(settings);

	// Launch external browser to download or show instructions.
	status_label->set_text(vformat(TTR("Please download engine source from:\nhttps://github.com/LoongSerpent9Realms/Jundot\n\nCache path set to: %s"), cache_path));
}

void AIConfigPanel::_on_engine_source_delete_button_pressed() {
	AISettingsData settings = AISettings::load();
	String cache_path = settings.engine_source_cache_root.strip_edges();
	if (cache_path.is_empty()) {
		cache_path = OS::get_singleton()->get_user_data_dir().path_join("engine_source");
	}

	String source_root = _find_engine_source_root_in_cache(cache_path);

	if (source_root.is_empty()) {
		status_label->set_text(TTR("No source cache to delete."));
		return;
	}

	Error err = DirAccess::remove_absolute(source_root);
	if (err != OK) {
		status_label->set_text(vformat(TTR("Failed to delete cache: error %d"), err));
		return;
	}

	settings.engine_source_root = "";
	AISettings::save(settings);

	status_label->set_text(TTR("Engine source cache deleted."));
	_update_engine_source_status();
}

void AIConfigPanel::_on_engine_source_cache_path_selected(const String &p_path) {
	engine_source_cache_path_edit->set_text(p_path);
	AISettingsData settings = AISettings::load();
	settings.engine_source_cache_root = p_path;
	AISettings::save(settings);
	_update_engine_source_status();
}

void AIConfigPanel::_load_settings() {
	const AISettingsData settings = AISettings::load();
	backend_type_option->select(backend_type_option->get_item_index(_backend_type_to_id(settings.backend_type)));
	jundot_plugin_id_edit->set_text(settings.jundot_ai_plugin_id);
	jundot_plugin_url_edit->set_text(settings.jundot_ai_plugin_url);
	base_url_edit->set_text(settings.base_url);
	model_edit->set_text(settings.model);
	api_key_edit->set_text(settings.api_key);
	temperature_spin->set_value(settings.temperature);
	max_tokens_spin->set_value(settings.max_tokens);
	context_char_budget_spin->set_value(settings.context_char_budget);
	history_budget_spin->set_value(settings.history_char_budget);
	max_tool_iterations_spin->set_value(settings.max_tool_iterations);
	feature_universality_threshold_spin->set_value(settings.feature_universality_threshold);
	feature_necessity_threshold_spin->set_value(settings.feature_necessity_threshold);
	output_language_option->select(output_language_option->get_item_index(_output_language_to_id(settings.output_language)));
	include_project_memories_check->set_pressed(settings.include_project_memories);
	include_tool_context_check->set_pressed(settings.include_tool_context);
	tools_enabled_check->set_pressed(settings.tools_enabled);
	develop_mode_check->set_pressed(settings.develop_mode);
	mcp_tools_enabled_check->set_pressed(settings.mcp_tools_enabled);
	auto_suggest_entries_check->set_pressed(settings.auto_suggest_entries);
	feature_design_philosophy_check->set_pressed(settings.feature_design_philosophy_check);
	external_api_enabled_check->set_pressed(settings.external_api_enabled);
	external_api_port_spin->set_value(settings.external_api_port);
	external_api_bind_address_edit->set_text(settings.external_api_bind_address);
	_update_external_mcp_config();
	_update_backend_controls();
	user_extra_instructions_edit->set_text(settings.user_extra_instructions);
	status_label->set_text(TTR("AI settings loaded."));
	_update_engine_source_status();
}

void AIConfigPanel::_save_settings() {
	AISettingsData settings = AISettings::load();
	const bool was_develop_mode = settings.develop_mode;
	settings.backend_type = _backend_type_from_id(backend_type_option->get_selected_id());
	settings.jundot_ai_plugin_id = jundot_plugin_id_edit->get_text().strip_edges();
	settings.jundot_ai_plugin_url = jundot_plugin_url_edit->get_text().strip_edges();
	settings.base_url = base_url_edit->get_text().strip_edges();
	settings.model = model_edit->get_text().strip_edges();
	settings.api_key = api_key_edit->get_text();
	settings.temperature = temperature_spin->get_value();
	settings.max_tokens = max_tokens_spin->get_value();
	settings.context_char_budget = context_char_budget_spin->get_value();
	settings.history_char_budget = history_budget_spin->get_value();
	settings.max_tool_iterations = max_tool_iterations_spin->get_value();
	settings.feature_universality_threshold = feature_universality_threshold_spin->get_value();
	settings.feature_necessity_threshold = feature_necessity_threshold_spin->get_value();
	settings.output_language = _output_language_from_id(output_language_option->get_selected_id());
	settings.include_project_memories = include_project_memories_check->is_pressed();
	settings.include_tool_context = include_tool_context_check->is_pressed();
	settings.tools_enabled = tools_enabled_check->is_pressed();
	settings.develop_mode = develop_mode_check->is_pressed();
	settings.mcp_tools_enabled = mcp_tools_enabled_check->is_pressed();
	settings.auto_suggest_entries = auto_suggest_entries_check->is_pressed();
	settings.feature_design_philosophy_check = feature_design_philosophy_check->is_pressed();
	settings.user_extra_instructions = user_extra_instructions_edit->get_text();
	settings.external_api_enabled = external_api_enabled_check->is_pressed();
	settings.external_api_port = external_api_port_spin->get_value();
	settings.external_api_bind_address = external_api_bind_address_edit->get_text().strip_edges();
	const Error err = AISettings::save(settings);
	if (err != OK) {
		status_label->set_text(TTR("AI settings could not be saved."));
		return;
	}
	if (!was_develop_mode && settings.develop_mode) {
		AIDevelopFlow::reset();
	}
	AIMCPManager::get_singleton()->update_settings(settings);
	_update_external_mcp_config();
	status_label->set_text(TTR("AI settings saved."));
}

void AIConfigPanel::_reset_settings() {
	const Error err = AISettings::reset_to_defaults();
	if (err != OK) {
		status_label->set_text(TTR("AI settings could not be reset."));
		return;
	}
	_load_settings();
	status_label->set_text(TTR("AI settings reset to defaults."));
}

void AIConfigPanel::_test_connection() {
	AISettingsData settings = AISettings::load();
	settings.backend_type = _backend_type_from_id(backend_type_option->get_selected_id());
	settings.jundot_ai_plugin_id = jundot_plugin_id_edit->get_text().strip_edges();
	settings.jundot_ai_plugin_url = jundot_plugin_url_edit->get_text().strip_edges();
	settings.base_url = base_url_edit->get_text().strip_edges();
	settings.model = model_edit->get_text().strip_edges();
	settings.api_key = api_key_edit->get_text();
	settings.temperature = temperature_spin->get_value();
	settings.max_tokens = MIN<int>(max_tokens_spin->get_value(), 64);
	settings.context_char_budget = context_char_budget_spin->get_value();
	settings.history_char_budget = history_budget_spin->get_value();
	settings.max_tool_iterations = max_tool_iterations_spin->get_value();
	settings.feature_universality_threshold = feature_universality_threshold_spin->get_value();
	settings.feature_necessity_threshold = feature_necessity_threshold_spin->get_value();
	settings.output_language = _output_language_from_id(output_language_option->get_selected_id());
	settings.include_project_memories = include_project_memories_check->is_pressed();
	settings.include_tool_context = include_tool_context_check->is_pressed();
	settings.tools_enabled = tools_enabled_check->is_pressed();
	settings.develop_mode = develop_mode_check->is_pressed();
	settings.mcp_tools_enabled = mcp_tools_enabled_check->is_pressed();
	settings.auto_suggest_entries = auto_suggest_entries_check->is_pressed();
	settings.feature_design_philosophy_check = feature_design_philosophy_check->is_pressed();
	settings.system_prompt = AISettings::get_default_system_prompt();

	if (settings.backend_type == AIBackendType::JUNDOT_PLUGIN && (settings.jundot_ai_plugin_id.is_empty() || settings.jundot_ai_plugin_url.is_empty())) {
		status_label->set_text(TTR("MiMoCode plugin ID and URL are required before testing the connection."));
		return;
	}

	if (settings.backend_type == AIBackendType::LEGACY_OPENAI && (settings.base_url.is_empty() || settings.model.is_empty() || settings.api_key.is_empty())) {
		status_label->set_text(TTR("Base URL, model, and API key are required before testing the connection."));
		return;
	}

	if (test_service->is_requesting()) {
		test_service->cancel_request();
	}

	test_service->configure(settings);
	const Error err = test_service->send_chat(TTR("Say hello from Jundot."));
	if (err != OK) {
		status_label->set_text(TTR("Connection test could not start."));
		return;
	}

	status_label->set_text(TTR("Testing AI connection..."));
}

void AIConfigPanel::_test_connection_completed(int p_result, int p_response_code, const String &p_content, const Dictionary &p_json, const String &p_raw_body, double p_elapsed_seconds, const String &p_think_content, int p_prompt_tokens, int p_completion_tokens) {
	if (p_result == HTTPRequest::RESULT_SUCCESS && p_response_code < HTTPClient::RESPONSE_BAD_REQUEST) {
		// Auto-save on successful test so users don't lose their config.
		_save_settings();
		status_label->set_text(TTR("Connection test succeeded. Settings saved."));
		return;
	}

	String error_text = p_content;
	if (error_text.is_empty()) {
		error_text = vformat(TTR("Connection test failed. HTTP %d."), p_response_code);
	}
	status_label->set_text(error_text);
}

void AIConfigPanel::_view_usage_agreement() {
	usage_agreement_dialog->popup_centered(Size2(420, 220) * EDSCALE);
}

void AIConfigPanel::_reset_usage_agreement() {
	const Error err = AISettings::reset_usage_agreement();
	status_label->set_text(err == OK ? TTR("AI usage agreement consent reset.") : TTR("AI usage agreement consent could not be reset."));
}

void AIConfigPanel::_export_config() {
	export_dialog->set_current_file("ai_config.json");
	export_dialog->popup_file_dialog();
}

void AIConfigPanel::_export_config_confirmed(const String &p_path) {
	AISettingsData settings = AISettings::load();
	settings.backend_type = _backend_type_from_id(backend_type_option->get_selected_id());
	settings.jundot_ai_plugin_id = jundot_plugin_id_edit->get_text().strip_edges();
	settings.jundot_ai_plugin_url = jundot_plugin_url_edit->get_text().strip_edges();
	settings.base_url = base_url_edit->get_text().strip_edges();
	settings.model = model_edit->get_text().strip_edges();
	settings.api_key = api_key_edit->get_text();
	settings.temperature = temperature_spin->get_value();
	settings.max_tokens = max_tokens_spin->get_value();
	settings.context_char_budget = context_char_budget_spin->get_value();
	settings.history_char_budget = history_budget_spin->get_value();
	settings.max_tool_iterations = max_tool_iterations_spin->get_value();
	settings.feature_universality_threshold = feature_universality_threshold_spin->get_value();
	settings.feature_necessity_threshold = feature_necessity_threshold_spin->get_value();
	settings.output_language = _output_language_from_id(output_language_option->get_selected_id());
	settings.include_project_memories = include_project_memories_check->is_pressed();
	settings.include_tool_context = include_tool_context_check->is_pressed();
	settings.tools_enabled = tools_enabled_check->is_pressed();
	settings.develop_mode = develop_mode_check->is_pressed();
	settings.mcp_tools_enabled = mcp_tools_enabled_check->is_pressed();
	settings.auto_suggest_entries = auto_suggest_entries_check->is_pressed();
	settings.feature_design_philosophy_check = feature_design_philosophy_check->is_pressed();
	settings.system_prompt = AISettings::get_default_system_prompt();

	Dictionary root;
	root["backend_type"] = settings.backend_type == AIBackendType::LEGACY_OPENAI ? "legacy_openai" : "jundot_plugin";
	root["jundot_ai_plugin_id"] = settings.jundot_ai_plugin_id;
	root["jundot_ai_plugin_url"] = settings.jundot_ai_plugin_url;
	root["allow_legacy_openai_backend"] = settings.allow_legacy_openai_backend;
	root["base_url"] = settings.base_url;
	root["model"] = settings.model;
	root["api_key"] = settings.api_key;
	root["temperature"] = settings.temperature;
	root["max_tokens"] = settings.max_tokens;
	root["system_prompt"] = AISettings::get_default_system_prompt();
	root["include_project_memories"] = settings.include_project_memories;
	root["include_tool_context"] = settings.include_tool_context;
	root["tools_enabled"] = settings.tools_enabled;
	root["develop_mode"] = settings.develop_mode;
	root["mcp_tools_enabled"] = settings.mcp_tools_enabled;
	root["context_char_budget"] = settings.context_char_budget;
	root["history_char_budget"] = settings.history_char_budget;
	root["max_tool_iterations"] = settings.max_tool_iterations;
	root["auto_suggest_entries"] = settings.auto_suggest_entries;
	root["output_language"] = settings.output_language;
	root["feature_universality_threshold"] = settings.feature_universality_threshold;
	root["feature_necessity_threshold"] = settings.feature_necessity_threshold;
	root["feature_design_philosophy_check"] = settings.feature_design_philosophy_check;
	root["engine_source_root"] = settings.engine_source_root;
	root["engine_source_cache_root"] = settings.engine_source_cache_root;
	root["engine_source_repository_url"] = settings.engine_source_repository_url;
	root["encrypt_engine_source_cache"] = settings.encrypt_engine_source_cache;
	root["external_api_enabled"] = settings.external_api_enabled;
	root["external_api_port"] = settings.external_api_port;
	root["external_api_bind_address"] = settings.external_api_bind_address;
	root["schema_version"] = 1;

	Error err;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);
	if (err != OK || file.is_null()) {
		status_label->set_text(vformat(TTR("Failed to export config: %s"), TTR(error_names[err])));
		return;
	}

	file->store_string(JSON::stringify(root, "\t"));
	status_label->set_text(vformat(TTR("Config exported to %s"), p_path));
}

void AIConfigPanel::_import_config() {
	import_dialog->popup_file_dialog();
}

void AIConfigPanel::_auto_configure_external_mcp_apps() {
	if (external_api_enabled_check) {
		external_api_enabled_check->set_pressed(true);
	}
	_update_external_mcp_config();
	_save_settings();

	const String bind_address = external_api_bind_address_edit ? external_api_bind_address_edit->get_text().strip_edges() : String("127.0.0.1");
	const int port = external_api_port_spin ? int(external_api_port_spin->get_value()) : 8080;
	const String base_url = _get_external_mcp_base_url(bind_address, port);

	Vector<ExternalMCPAppTarget> targets;
	const String appdata = OS::get_singleton()->has_environment("APPDATA") ? OS::get_singleton()->get_environment("APPDATA") : String();
	if (!appdata.is_empty()) {
		_append_external_mcp_target(targets, "Claude Desktop", appdata.path_join("Claude").path_join("claude_desktop_config.json"));
		_append_external_mcp_target(targets, "Cursor", appdata.path_join("Cursor").path_join("User").path_join("mcp.json"));
		_append_external_mcp_target(targets, "Visual Studio Code", appdata.path_join("Code").path_join("User").path_join("mcp.json"));
		_append_external_mcp_target(targets, "Windsurf", appdata.path_join("Windsurf").path_join("User").path_join("mcp.json"));
	}

	String configured;
	String failed;
	for (const ExternalMCPAppTarget &target : targets) {
		if (!DirAccess::dir_exists_absolute(target.path.get_base_dir())) {
			continue;
		}

		const Error err = _write_external_ai_mcp_config(target.path, base_url);
		if (err == OK) {
			if (!configured.is_empty()) {
				configured += ", ";
			}
			configured += target.name;
		} else {
			if (!failed.is_empty()) {
				failed += ", ";
			}
			failed += target.name;
		}
	}

	if (configured.is_empty() && failed.is_empty()) {
		status_label->set_text(TTR("No supported external AI app config directories were found."));
		return;
	}
	if (configured.is_empty()) {
		status_label->set_text(vformat(TTR("Failed to configure external MCP for %s."), failed));
		return;
	}
	if (!failed.is_empty()) {
		status_label->set_text(vformat(TTR("Configured external MCP for %s. Failed: %s."), configured, failed));
		return;
	}
	status_label->set_text(vformat(TTR("Configured external MCP for %s."), configured));
}

void AIConfigPanel::_import_config_confirmed(const String &p_path) {
	if (!FileAccess::exists(p_path)) {
		status_label->set_text(TTR("Import failed: file not found."));
		return;
	}

	Error err = OK;
	const String content = FileAccess::get_file_as_string(p_path, &err);
	if (err != OK || content.is_empty()) {
		status_label->set_text(TTR("Import failed: could not read file."));
		return;
	}

	JSON json;
	err = json.parse(content);
	if (err != OK) {
		status_label->set_text(TTR("Import failed: invalid JSON."));
		return;
	}

	const Variant json_data = json.get_data();
	if (json_data.get_type() != Variant::DICTIONARY) {
		status_label->set_text(TTR("Import failed: unexpected JSON structure."));
		return;
	}

	const Dictionary root = json_data;
	if (root.has("backend_type")) {
		backend_type_option->select(backend_type_option->get_item_index(String(root["backend_type"]) == "legacy_openai" ? BACKEND_TYPE_LEGACY_OPENAI : BACKEND_TYPE_JUNDOT_PLUGIN));
	}
	if (root.has("jundot_ai_plugin_id")) {
		jundot_plugin_id_edit->set_text(root["jundot_ai_plugin_id"]);
	}
	if (root.has("jundot_ai_plugin_url")) {
		jundot_plugin_url_edit->set_text(root["jundot_ai_plugin_url"]);
	}
	if (root.has("base_url")) {
		base_url_edit->set_text(root["base_url"]);
	}
	if (root.has("model")) {
		model_edit->set_text(root["model"]);
	}
	if (root.has("api_key")) {
		api_key_edit->set_text(root["api_key"]);
	}
	if (root.has("temperature")) {
		temperature_spin->set_value(root["temperature"]);
	}
	if (root.has("max_tokens")) {
		max_tokens_spin->set_value(root["max_tokens"]);
	}
	if (root.has("context_char_budget")) {
		context_char_budget_spin->set_value(root["context_char_budget"]);
	}
	if (root.has("history_char_budget")) {
		history_budget_spin->set_value(root["history_char_budget"]);
	}
	if (root.has("max_tool_iterations")) {
		max_tool_iterations_spin->set_value(root["max_tool_iterations"]);
	}
	if (root.has("feature_universality_threshold")) {
		feature_universality_threshold_spin->set_value(root["feature_universality_threshold"]);
	}
	if (root.has("feature_necessity_threshold")) {
		feature_necessity_threshold_spin->set_value(root["feature_necessity_threshold"]);
	}
	if (root.has("output_language")) {
		output_language_option->select(output_language_option->get_item_index(_output_language_to_id(String(root["output_language"]))));
	}
	if (root.has("include_project_memories")) {
		include_project_memories_check->set_pressed(root["include_project_memories"]);
	}
	if (root.has("include_tool_context")) {
		include_tool_context_check->set_pressed(root["include_tool_context"]);
	}
	if (root.has("tools_enabled")) {
		tools_enabled_check->set_pressed(root["tools_enabled"]);
	}
	if (root.has("develop_mode")) {
		develop_mode_check->set_pressed(root["develop_mode"]);
	}
	if (root.has("mcp_tools_enabled")) {
		mcp_tools_enabled_check->set_pressed(root["mcp_tools_enabled"]);
	}
	if (root.has("auto_suggest_entries")) {
		auto_suggest_entries_check->set_pressed(root["auto_suggest_entries"]);
	}
	if (root.has("feature_design_philosophy_check")) {
		feature_design_philosophy_check->set_pressed(root["feature_design_philosophy_check"]);
	}
	_save_settings();
	AISettingsData imported_settings = AISettings::load();
	if (root.has("engine_source_root")) {
		imported_settings.engine_source_root = root["engine_source_root"];
	}
	if (root.has("engine_source_cache_root")) {
		imported_settings.engine_source_cache_root = root["engine_source_cache_root"];
	}
	if (root.has("external_api_enabled")) {
		imported_settings.external_api_enabled = root["external_api_enabled"];
	}
	if (root.has("external_api_port")) {
		imported_settings.external_api_port = root["external_api_port"];
	}
	if (root.has("external_api_bind_address")) {
		imported_settings.external_api_bind_address = root["external_api_bind_address"];
	}
	AISettings::save(imported_settings);
	status_label->set_text(vformat(TTR("Config imported from %s and saved."), p_path));
}

AIConfigPanel::AIConfigPanel() {
	set_name(TTRC("Config"));
	add_theme_constant_override("margin_left", 8 * EDSCALE);
	add_theme_constant_override("margin_top", 8 * EDSCALE);
	add_theme_constant_override("margin_right", 8 * EDSCALE);
	add_theme_constant_override("margin_bottom", 8 * EDSCALE);

	settings_scroll = memnew(ScrollContainer);
	settings_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	settings_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	settings_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	settings_scroll->set_vertical_scroll_mode(ScrollContainer::SCROLL_MODE_AUTO);
	add_child(settings_scroll);

	VBoxContainer *root = memnew(VBoxContainer);
	root->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	root->add_theme_constant_override("separation", 8 * EDSCALE);
	settings_scroll->add_child(root);

	title_label = memnew(Label);
	title_label->set_theme_type_variation("HeaderSmall");
	root->add_child(title_label);

	settings_grid = memnew(GridContainer);
	settings_grid->set_columns(2);
	settings_grid->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	settings_grid->add_theme_constant_override("h_separation", 12 * EDSCALE);
	settings_grid->add_theme_constant_override("v_separation", 6 * EDSCALE);
	root->add_child(settings_grid);
	GridContainer *grid = settings_grid;

	backend_type_option = _add_backend_type_row(grid, &backend_type_label, TTR("AI Backend"));
	backend_type_option->connect(SceneStringName(item_selected), callable_mp(this, &AIConfigPanel::_on_backend_type_selected));
	jundot_plugin_id_edit = _add_line_edit_row(grid, &jundot_plugin_id_label, TTR("jundot AI Plugin ID"), JUNDOT_MIMOCODE_PLUGIN_ID);
	jundot_plugin_url_edit = _add_line_edit_row(grid, &jundot_plugin_url_label, TTR("jundot AI Plugin URL"), "http://127.0.0.1:4096");

	mimocode_download_spacer = memnew(Label);
	grid->add_child(mimocode_download_spacer);

	mimocode_download_button = memnew(Button);
	mimocode_download_button->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
	mimocode_download_button->connect(SceneStringName(pressed), callable_mp(this, &AIConfigPanel::_on_mimocode_download_button_pressed));
	grid->add_child(mimocode_download_button);

	base_url_edit = _add_line_edit_row(grid, &base_url_label, TTR("Base URL"), AISettings::get_default_base_url());
	model_edit = _add_line_edit_row(grid, &model_label, TTR("Model"), AISettings::get_default_model());
	api_key_edit = _add_line_edit_row(grid, &api_key_label, TTR("API Key"), String(), true);
	temperature_spin = _add_spin_box_row(grid, &temperature_label, TTR("Temperature"), 0.0, 2.0, 0.05);
	max_tokens_spin = _add_spin_box_row(grid, &max_tokens_label, TTR("Max Tokens"), 1, 262144, 1);
	context_char_budget_spin = _add_spin_box_row(grid, &context_char_budget_label, TTR("Context Budget"), 0, 262144, 256);
	history_budget_spin = _add_spin_box_row(grid, &history_budget_label, TTR("Compressed Context Size"), 0, 262144, 256);
	max_tool_iterations_spin = _add_spin_box_row(grid, &max_tool_iterations_label, TTR("Max Tool Call Iterations"), 1, 1000, 1);
	feature_universality_threshold_spin = _add_spin_box_row(grid, &feature_universality_threshold_label, TTR("Feature Universality Threshold (%)"), 0, 100, 1);
	feature_necessity_threshold_spin = _add_spin_box_row(grid, &feature_necessity_threshold_label, TTR("Feature Necessity Threshold"), 0, 1, 0.05);
	output_language_option = _add_output_language_row(grid, &output_language_label, TTR("AI Output Language"));

	include_project_memories_check = memnew(CheckBox);
	root->add_child(include_project_memories_check);

	include_tool_context_check = memnew(CheckBox);
	root->add_child(include_tool_context_check);

	tools_enabled_check = memnew(CheckBox);
	root->add_child(tools_enabled_check);

	develop_mode_check = memnew(CheckBox);
	develop_mode_check->set_tooltip_text(TTR("Demonstrates the complete engine AI workflow without committing or pushing to GitHub."));
	root->add_child(develop_mode_check);

	mcp_tools_enabled_check = memnew(CheckBox);
	root->add_child(mcp_tools_enabled_check);

	auto_suggest_entries_check = memnew(CheckBox);
	root->add_child(auto_suggest_entries_check);

	feature_design_philosophy_check = memnew(CheckBox);
	root->add_child(feature_design_philosophy_check);

	external_api_enabled_check = memnew(CheckBox);
	external_api_enabled_check->connect(SceneStringName(toggled), callable_mp(this, &AIConfigPanel::_update_external_mcp_config).unbind(1));
	root->add_child(external_api_enabled_check);

	external_api_port_spin = _add_spin_box_row(grid, &external_api_port_label, TTR("External API Port"), 1, 65535, 1);
	external_api_port_spin->connect(SceneStringName(value_changed), callable_mp(this, &AIConfigPanel::_update_external_mcp_config).unbind(1));
	external_api_bind_address_edit = _add_line_edit_row(grid, &external_api_bind_address_label, TTR("Bind Address"), "127.0.0.1");
	external_api_bind_address_edit->connect(SceneStringName(text_changed), callable_mp(this, &AIConfigPanel::_update_external_mcp_config).unbind(1));

	external_mcp_config_label = memnew(Label);
	root->add_child(external_mcp_config_label);

	external_mcp_config_edit = memnew(TextEdit);
	external_mcp_config_edit->set_editable(false);
	external_mcp_config_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	external_mcp_config_edit->set_custom_minimum_size(Size2(0, 140) * EDSCALE);
	root->add_child(external_mcp_config_edit);

	auto_configure_mcp_button = memnew(Button);
	auto_configure_mcp_button->connect(SceneStringName(pressed), callable_mp(this, &AIConfigPanel::_auto_configure_external_mcp_apps));
	root->add_child(auto_configure_mcp_button);

	// Engine Source section.
	{
		Label *engine_source_header = memnew(Label);
		engine_source_header->set_theme_type_variation("HeaderMedium");
		engine_source_header->set_text(TTR("AI Engine Source Cache"));
		root->add_child(engine_source_header);

		engine_source_status_label = memnew(Label);
		engine_source_status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		root->add_child(engine_source_status_label);

		engine_source_path_grid = memnew(GridContainer);
		engine_source_path_grid->set_columns(2);
		engine_source_path_grid->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		engine_source_path_grid->add_theme_constant_override("h_separation", 6 * EDSCALE);
		engine_source_path_grid->add_theme_constant_override("v_separation", 6 * EDSCALE);
		root->add_child(engine_source_path_grid);

		engine_source_cache_path_edit = memnew(LineEdit);
		engine_source_cache_path_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		engine_source_path_grid->add_child(engine_source_cache_path_edit);

		engine_source_browse_button = memnew(Button);
		engine_source_browse_button->set_text(TTR("Browse"));
		engine_source_browse_button->connect(SceneStringName(pressed), callable_mp(this, &AIConfigPanel::_on_engine_source_browse_button_pressed));
		engine_source_path_grid->add_child(engine_source_browse_button);

		engine_source_actions = memnew(HFlowContainer);
		engine_source_actions->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		engine_source_actions->add_theme_constant_override("h_separation", 6 * EDSCALE);
		engine_source_actions->add_theme_constant_override("v_separation", 6 * EDSCALE);
		root->add_child(engine_source_actions);

		engine_source_download_button = memnew(Button);
		engine_source_download_button->connect(SceneStringName(pressed), callable_mp(this, &AIConfigPanel::_on_engine_source_download_button_pressed));
		engine_source_actions->add_child(engine_source_download_button);

		engine_source_update_button = memnew(Button);
		engine_source_update_button->set_text(TTR("Check Updates"));
		engine_source_update_button->connect(SceneStringName(pressed), callable_mp(this, &AIConfigPanel::_on_engine_source_update_button_pressed));
		engine_source_actions->add_child(engine_source_update_button);

		engine_source_delete_button = memnew(Button);
		engine_source_delete_button->set_text(TTR("Delete Cache"));
		engine_source_delete_button->connect(SceneStringName(pressed), callable_mp(this, &AIConfigPanel::_on_engine_source_delete_button_pressed));
		engine_source_actions->add_child(engine_source_delete_button);

		Label *engine_source_spacer = memnew(Label);
		engine_source_spacer->set_custom_minimum_size(Size2(0, 8) * EDSCALE);
		root->add_child(engine_source_spacer);
	}

	usage_notice_label = memnew(Label);
	usage_notice_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	root->add_child(usage_notice_label);

	system_prompt_label = memnew(Label);
	system_prompt_label->hide();
	root->add_child(system_prompt_label);

	system_prompt_edit = memnew(TextEdit);
	system_prompt_edit->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	system_prompt_edit->set_custom_minimum_size(Size2(0, 120) * EDSCALE);
	system_prompt_edit->hide();
	root->add_child(system_prompt_edit);

	// User extra instructions — a customizable prompt that appends to system message.
	user_extra_instructions_label = memnew(Label);
	root->add_child(user_extra_instructions_label);

	user_extra_instructions_edit = memnew(TextEdit);
	user_extra_instructions_edit->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	user_extra_instructions_edit->set_custom_minimum_size(Size2(0, 100) * EDSCALE);
	user_extra_instructions_edit->set_placeholder(TTR("Optional: enter extra instructions here. These will be appended to the system prompt."));
	root->add_child(user_extra_instructions_edit);

	config_actions = memnew(HFlowContainer);
	config_actions->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	config_actions->add_theme_constant_override("h_separation", 6 * EDSCALE);
	config_actions->add_theme_constant_override("v_separation", 6 * EDSCALE);
	root->add_child(config_actions);
	HFlowContainer *actions = config_actions;

	save_button = memnew(Button);
	save_button->connect(SceneStringName(pressed), callable_mp(this, &AIConfigPanel::_save_settings));
	actions->add_child(save_button);

	reset_button = memnew(Button);
	reset_button->connect(SceneStringName(pressed), callable_mp(this, &AIConfigPanel::_reset_settings));
	actions->add_child(reset_button);

	test_button = memnew(Button);
	test_button->connect(SceneStringName(pressed), callable_mp(this, &AIConfigPanel::_test_connection));
	actions->add_child(test_button);

	view_agreement_button = memnew(Button);
	view_agreement_button->connect(SceneStringName(pressed), callable_mp(this, &AIConfigPanel::_view_usage_agreement));
	actions->add_child(view_agreement_button);

	reset_agreement_button = memnew(Button);
	reset_agreement_button->connect(SceneStringName(pressed), callable_mp(this, &AIConfigPanel::_reset_usage_agreement));
	actions->add_child(reset_agreement_button);

	export_button = memnew(Button);
	export_button->connect(SceneStringName(pressed), callable_mp(this, &AIConfigPanel::_export_config));
	actions->add_child(export_button);

	import_button = memnew(Button);
	import_button->connect(SceneStringName(pressed), callable_mp(this, &AIConfigPanel::_import_config));
	actions->add_child(import_button);

	// Export dialog (save mode).
	export_dialog = memnew(FileDialog);
	export_dialog->set_file_mode(FileDialog::FILE_MODE_SAVE_FILE);
	export_dialog->set_access(FileDialog::ACCESS_FILESYSTEM);
	export_dialog->set_title(TTR("Export AI Config"));
	export_dialog->add_filter("*.json", TTR("JSON Files"));
	export_dialog->connect("file_selected", callable_mp(this, &AIConfigPanel::_export_config_confirmed));
	add_child(export_dialog, false, INTERNAL_MODE_FRONT);

	// Import dialog (open mode).
	import_dialog = memnew(FileDialog);
	import_dialog->set_file_mode(FileDialog::FILE_MODE_OPEN_FILE);
	import_dialog->set_access(FileDialog::ACCESS_FILESYSTEM);
	import_dialog->set_title(TTR("Import AI Config"));
	import_dialog->add_filter("*.json", TTR("JSON Files"));
	import_dialog->connect("file_selected", callable_mp(this, &AIConfigPanel::_import_config_confirmed));
	add_child(import_dialog, false, INTERNAL_MODE_FRONT);

	status_label = memnew(Label);
	status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	status_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	root->add_child(status_label);

	test_service = memnew(AIChatService);
	test_service->connect(SNAME("chat_completed"), callable_mp(this, &AIConfigPanel::_test_connection_completed));
	add_child(test_service, false, INTERNAL_MODE_BACK);

	usage_agreement_dialog = memnew(AIUsageAgreementDialog);
	add_child(usage_agreement_dialog);

	_update_translations();
	_load_settings();
}
