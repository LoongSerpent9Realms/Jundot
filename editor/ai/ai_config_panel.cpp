/*  ai_config_panel.cpp                                                    */
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

#include "ai_chat_service.h"
#include "ai_settings.h"
#include "ai_usage_agreement_dialog.h"

#include "core/object/callable_mp.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/text_edit.h"
#include "scene/main/http_request.h"

void AIConfigPanel::_bind_methods() {
}

void AIConfigPanel::_notification(int p_what) {
	if (p_what == NOTIFICATION_TRANSLATION_CHANGED) {
		_update_translations();
	}
}

LineEdit *AIConfigPanel::_add_line_edit_row(GridContainer *p_grid, Label **r_label, const String &p_label, const String &p_placeholder, bool p_secret) {
	Label *label = memnew(Label);
	label->set_text(p_label);
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

void AIConfigPanel::_update_translations() {
	set_name(TTRC("Config"));
	title_label->set_text(TTR("AI Configuration"));
	base_url_label->set_text(TTR("Base URL"));
	model_label->set_text(TTR("Model"));
	api_key_label->set_text(TTR("API Key"));
	temperature_label->set_text(TTR("Temperature"));
	max_tokens_label->set_text(TTR("Max Tokens"));
	context_char_budget_label->set_text(TTR("Context Budget"));
	feature_universality_threshold_label->set_text(TTR("Feature Universality Threshold (%)"));
	feature_necessity_threshold_label->set_text(TTR("Feature Necessity Threshold"));
	system_prompt_label->set_text(TTR("System Prompt"));
	include_project_memories_check->set_text(TTR("Include project memories"));
	include_tool_context_check->set_text(TTR("Include skill and MCP context"));
	auto_suggest_entries_check->set_text(TTR("Allow AI to suggest Skill/MCP/Memory entries"));
	feature_design_philosophy_check->set_text(TTR("Require Jundot design philosophy check for feature expansion"));
	usage_notice_label->set_text(TTR("AI requests can consume additional API tokens when project context, attachments, logs, or repair analysis are included."));
	save_button->set_text(TTR("Save"));
	reset_button->set_text(TTR("Reset"));
	test_button->set_text(TTR("Test Connection"));
	view_agreement_button->set_text(TTR("View AI Usage Agreement"));
	reset_agreement_button->set_text(TTR("Reset Agreement Consent"));
	base_url_edit->set_placeholder(AISettings::get_default_base_url());
	model_edit->set_placeholder(AISettings::get_default_model());
}

void AIConfigPanel::_load_settings() {
	const AISettingsData settings = AISettings::load();
	base_url_edit->set_text(settings.base_url);
	model_edit->set_text(settings.model);
	api_key_edit->set_text(settings.api_key);
	temperature_spin->set_value(settings.temperature);
	max_tokens_spin->set_value(settings.max_tokens);
	context_char_budget_spin->set_value(settings.context_char_budget);
	feature_universality_threshold_spin->set_value(settings.feature_universality_threshold);
	feature_necessity_threshold_spin->set_value(settings.feature_necessity_threshold);
	include_project_memories_check->set_pressed(settings.include_project_memories);
	include_tool_context_check->set_pressed(settings.include_tool_context);
	auto_suggest_entries_check->set_pressed(settings.auto_suggest_entries);
	feature_design_philosophy_check->set_pressed(settings.feature_design_philosophy_check);
	system_prompt_edit->set_text(settings.system_prompt);
	status_label->set_text(TTR("AI settings loaded."));
}

void AIConfigPanel::_save_settings() {
	AISettingsData settings = AISettings::load();
	settings.base_url = base_url_edit->get_text().strip_edges();
	settings.model = model_edit->get_text().strip_edges();
	settings.api_key = api_key_edit->get_text();
	settings.temperature = temperature_spin->get_value();
	settings.max_tokens = max_tokens_spin->get_value();
	settings.context_char_budget = context_char_budget_spin->get_value();
	settings.feature_universality_threshold = feature_universality_threshold_spin->get_value();
	settings.feature_necessity_threshold = feature_necessity_threshold_spin->get_value();
	settings.include_project_memories = include_project_memories_check->is_pressed();
	settings.include_tool_context = include_tool_context_check->is_pressed();
	settings.auto_suggest_entries = auto_suggest_entries_check->is_pressed();
	settings.feature_design_philosophy_check = feature_design_philosophy_check->is_pressed();
	settings.system_prompt = system_prompt_edit->get_text();
	const Error err = AISettings::save(settings);
	if (err != OK) {
		status_label->set_text(TTR("AI settings could not be saved."));
		return;
	}
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
	settings.base_url = base_url_edit->get_text().strip_edges();
	settings.model = model_edit->get_text().strip_edges();
	settings.api_key = api_key_edit->get_text();
	settings.temperature = temperature_spin->get_value();
	settings.max_tokens = MIN<int>(max_tokens_spin->get_value(), 64);
	settings.context_char_budget = context_char_budget_spin->get_value();
	settings.feature_universality_threshold = feature_universality_threshold_spin->get_value();
	settings.feature_necessity_threshold = feature_necessity_threshold_spin->get_value();
	settings.include_project_memories = include_project_memories_check->is_pressed();
	settings.include_tool_context = include_tool_context_check->is_pressed();
	settings.auto_suggest_entries = auto_suggest_entries_check->is_pressed();
	settings.feature_design_philosophy_check = feature_design_philosophy_check->is_pressed();
	settings.system_prompt = system_prompt_edit->get_text();

	if (settings.base_url.is_empty() || settings.model.is_empty() || settings.api_key.is_empty()) {
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
		status_label->set_text(TTR("Connection test succeeded."));
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

AIConfigPanel::AIConfigPanel() {
	set_name(TTRC("Config"));
	add_theme_constant_override("margin_left", 8 * EDSCALE);
	add_theme_constant_override("margin_top", 8 * EDSCALE);
	add_theme_constant_override("margin_right", 8 * EDSCALE);
	add_theme_constant_override("margin_bottom", 8 * EDSCALE);

	VBoxContainer *root = memnew(VBoxContainer);
	root->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	root->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	root->add_theme_constant_override("separation", 8 * EDSCALE);
	add_child(root);

	title_label = memnew(Label);
	title_label->set_theme_type_variation("HeaderSmall");
	root->add_child(title_label);

	GridContainer *grid = memnew(GridContainer);
	grid->set_columns(2);
	grid->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	root->add_child(grid);

	base_url_edit = _add_line_edit_row(grid, &base_url_label, TTR("Base URL"), AISettings::get_default_base_url());
	model_edit = _add_line_edit_row(grid, &model_label, TTR("Model"), AISettings::get_default_model());
	api_key_edit = _add_line_edit_row(grid, &api_key_label, TTR("API Key"), String(), true);
	temperature_spin = _add_spin_box_row(grid, &temperature_label, TTR("Temperature"), 0.0, 2.0, 0.05);
	max_tokens_spin = _add_spin_box_row(grid, &max_tokens_label, TTR("Max Tokens"), 1, 262144, 1);
	context_char_budget_spin = _add_spin_box_row(grid, &context_char_budget_label, TTR("Context Budget"), 0, 262144, 256);
	feature_universality_threshold_spin = _add_spin_box_row(grid, &feature_universality_threshold_label, TTR("Feature Universality Threshold (%)"), 0, 100, 1);
	feature_necessity_threshold_spin = _add_spin_box_row(grid, &feature_necessity_threshold_label, TTR("Feature Necessity Threshold"), 0, 1, 0.05);

	include_project_memories_check = memnew(CheckBox);
	root->add_child(include_project_memories_check);

	include_tool_context_check = memnew(CheckBox);
	root->add_child(include_tool_context_check);

	auto_suggest_entries_check = memnew(CheckBox);
	root->add_child(auto_suggest_entries_check);

	feature_design_philosophy_check = memnew(CheckBox);
	root->add_child(feature_design_philosophy_check);

	usage_notice_label = memnew(Label);
	usage_notice_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	root->add_child(usage_notice_label);

	system_prompt_label = memnew(Label);
	root->add_child(system_prompt_label);

	system_prompt_edit = memnew(TextEdit);
	system_prompt_edit->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	system_prompt_edit->set_custom_minimum_size(Size2(0, 120) * EDSCALE);
	root->add_child(system_prompt_edit);

	HBoxContainer *actions = memnew(HBoxContainer);
	actions->add_theme_constant_override("separation", 6 * EDSCALE);
	root->add_child(actions);

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
