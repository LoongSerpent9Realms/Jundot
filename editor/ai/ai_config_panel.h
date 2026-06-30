/*  ai_config_panel.h                                                     */
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

#include "scene/gui/margin_container.h"

class Button;
class CheckBox;
class AcceptDialog;
class FileDialog;
class GridContainer;
class HFlowContainer;
class HTTPRequest;
class ProgressBar;
class ScrollContainer;
class AIChatService;
class AIUsageAgreementDialog;
class Label;
class LineEdit;
class OptionButton;
class SpinBox;
class TextEdit;
class VBoxContainer;
struct AISettingsData;

class AIConfigPanel : public MarginContainer {
	GDCLASS(AIConfigPanel, MarginContainer)

	LineEdit *base_url_edit = nullptr;
	OptionButton *backend_type_option = nullptr;
	LineEdit *jundot_plugin_id_edit = nullptr;
	LineEdit *jundot_plugin_url_edit = nullptr;
	OptionButton *model_option = nullptr;
	Button *model_refresh_button = nullptr;
	HTTPRequest *model_list_request = nullptr;
	LineEdit *api_key_edit = nullptr;
	SpinBox *temperature_spin = nullptr;
	SpinBox *max_tokens_spin = nullptr;
	SpinBox *context_char_budget_spin = nullptr;
	SpinBox *history_budget_spin = nullptr;
	SpinBox *max_tool_iterations_spin = nullptr;
	SpinBox *feature_universality_threshold_spin = nullptr;
	SpinBox *feature_necessity_threshold_spin = nullptr;
	OptionButton *output_language_option = nullptr;
	CheckBox *include_project_memories_check = nullptr;
	CheckBox *include_tool_context_check = nullptr;
	CheckBox *low_token_mode_check = nullptr;
	CheckBox *tools_enabled_check = nullptr;
	Button *builtin_tools_button = nullptr;
	AcceptDialog *builtin_tools_dialog = nullptr;
	Label *builtin_tools_label = nullptr;
	VBoxContainer *builtin_tools_box = nullptr;
	Button *builtin_tools_enable_all_button = nullptr;
	Button *builtin_tools_disable_all_button = nullptr;
	Vector<CheckBox *> builtin_tool_checkboxes;
	CheckBox *develop_mode_check = nullptr;
	CheckBox *mcp_tools_enabled_check = nullptr;
	CheckBox *auto_suggest_entries_check = nullptr;
	CheckBox *auto_audit_enabled_check = nullptr;
	CheckBox *html_min_project_prototype_check = nullptr;
	CheckBox *feature_design_philosophy_check = nullptr;
	TextEdit *system_prompt_edit = nullptr;
	TextEdit *user_extra_instructions_edit = nullptr;
	Label *title_label = nullptr;
	Label *backend_type_label = nullptr;
	Label *jundot_plugin_id_label = nullptr;
	Label *jundot_plugin_url_label = nullptr;
	Label *base_url_label = nullptr;
	Label *model_label = nullptr;
	Label *api_key_label = nullptr;
	Label *temperature_label = nullptr;
	Label *max_tokens_label = nullptr;
	Label *context_char_budget_label = nullptr;
	Label *history_budget_label = nullptr;
	Label *max_tool_iterations_label = nullptr;
	Label *feature_universality_threshold_label = nullptr;
	Label *feature_necessity_threshold_label = nullptr;
	Label *output_language_label = nullptr;
	Label *system_prompt_label = nullptr;
	Label *user_extra_instructions_label = nullptr;
	Label *usage_notice_label = nullptr;
	Label *status_label = nullptr;
	Label *external_api_port_label = nullptr;
	Label *external_api_bind_address_label = nullptr;
	Label *external_mcp_config_label = nullptr;
	Label *mimocode_download_spacer = nullptr;
	AIChatService *test_service = nullptr;
	AIUsageAgreementDialog *usage_agreement_dialog = nullptr;
	CheckBox *external_api_enabled_check = nullptr;
	SpinBox *external_api_port_spin = nullptr;
	LineEdit *external_api_bind_address_edit = nullptr;
	TextEdit *external_mcp_config_edit = nullptr;
	Button *save_button = nullptr;
	Button *reset_button = nullptr;
	Button *test_button = nullptr;
	Button *view_agreement_button = nullptr;
	Button *reset_agreement_button = nullptr;
	Button *export_button = nullptr;
	Button *import_button = nullptr;
	Button *auto_configure_mcp_button = nullptr;
	Button *mimocode_download_button = nullptr;
	HTTPRequest *mimocode_download_request = nullptr;
	ProgressBar *mimocode_download_progress = nullptr;
	String mimocode_download_zip_path;
	Button *engine_source_download_button = nullptr;
	Button *engine_source_delete_button = nullptr;
	Button *engine_source_browse_button = nullptr;
	Button *engine_source_update_button = nullptr;
	Label *engine_source_status_label = nullptr;
	LineEdit *engine_source_cache_path_edit = nullptr;
	ProgressBar *engine_source_download_progress = nullptr;
	HTTPRequest *engine_source_download_request = nullptr;
	String engine_source_download_zip_path;
	String engine_source_download_url;
	bool engine_source_download_mirror_retry_available = false;

	Label *github_account_label = nullptr;
	Label *github_status_label = nullptr;
	LineEdit *github_client_id_edit = nullptr;
	Label *github_client_id_label = nullptr;
	LineEdit *github_client_secret_edit = nullptr;
	Label *github_client_secret_label = nullptr;
	Button *github_login_button = nullptr;
	Button *github_logout_button = nullptr;
	uint32_t _github_auth_start_time = 0;
	bool github_auth_polling = false;

	Label *gitee_account_label = nullptr;
	Label *gitee_status_label = nullptr;
	LineEdit *gitee_client_id_edit = nullptr;
	Label *gitee_client_id_label = nullptr;
	LineEdit *gitee_client_secret_edit = nullptr;
	Label *gitee_client_secret_label = nullptr;
	Button *gitee_login_button = nullptr;
	Button *gitee_logout_button = nullptr;
	uint32_t _gitee_auth_start_time = 0;
	bool gitee_auth_polling = false;

	FileDialog *export_dialog = nullptr;
	FileDialog *import_dialog = nullptr;
	ScrollContainer *settings_scroll = nullptr;
	GridContainer *settings_grid = nullptr;
	GridContainer *engine_source_path_grid = nullptr;
	HFlowContainer *engine_source_actions = nullptr;
	HFlowContainer *config_actions = nullptr;
	bool compact_layout = false;

	void _load_settings();
	void _save_settings();
	void _reset_settings();
	void _test_connection();
	void _view_usage_agreement();
	void _reset_usage_agreement();
	void _export_config();
	void _import_config();
	void _auto_configure_external_mcp_apps();
	void _export_config_confirmed(const String &p_path);
	void _import_config_confirmed(const String &p_path);
	void _test_connection_completed(int p_result, int p_response_code, const String &p_content, const Dictionary &p_json, const String &p_raw_body, double p_elapsed_seconds, const String &p_think_content, int p_prompt_tokens, int p_completion_tokens);
	void _update_translations();
	void _on_low_token_mode_toggled(bool p_pressed);
	void _update_external_mcp_config();
	void _update_backend_controls();
	void _rebuild_builtin_tool_checks(const AISettingsData &p_settings);
	Vector<String> _collect_disabled_builtin_tools() const;
	void _show_builtin_tools_dialog();
	void _enable_all_builtin_tools();
	void _disable_all_builtin_tools();
	void _update_mimocode_button();
	void _on_backend_type_selected(int p_index);
	void _on_model_refresh_pressed();
	void _on_model_list_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	void _fetch_available_models();
	void _on_mimocode_download_button_pressed();
	void _on_mimocode_download_progress(int p_amount_downloaded, int p_amount_total);
	void _on_mimocode_download_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	Error _start_mimocode(const String &p_executable_path);
	void _update_engine_source_status();
	void _on_engine_source_download_button_pressed();
	void _on_engine_source_download_progress(int p_amount_downloaded, int p_amount_total);
	void _on_engine_source_download_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	void _on_engine_source_delete_button_pressed();
	void _on_engine_source_browse_button_pressed();
	void _on_engine_source_update_button_pressed();
	void _on_engine_source_cache_path_selected(const String &p_path);
	void _update_adaptive_layout();

	void _on_github_login_pressed();
	void _on_github_logout_pressed();
	void _update_github_status();
	void _github_poll_auth();

	void _on_gitee_login_pressed();
	void _on_gitee_logout_pressed();
	void _update_gitee_status();
	void _gitee_poll_auth();

	LineEdit *_add_line_edit_row(GridContainer *p_grid, Label **r_label, const String &p_label, const String &p_placeholder = String(), bool p_secret = false);
	SpinBox *_add_spin_box_row(GridContainer *p_grid, Label **r_label, const String &p_label, double p_min, double p_max, double p_step);
	OptionButton *_add_backend_type_row(GridContainer *p_grid, Label **r_label, const String &p_label);
	OptionButton *_add_output_language_row(GridContainer *p_grid, Label **r_label, const String &p_label);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	static void stop_managed_mimocode();

	AIConfigPanel();
};
