/**************************************************************************/
/*  ai_config_panel.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
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

#include "ai_config_panel.h"

#include "ai_chat_service.h"
#include "ai_settings.h"

#include "core/object/callable_mp.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
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
	system_prompt_label->set_text(TTR("System Prompt"));
	save_button->set_text(TTR("Save"));
	reset_button->set_text(TTR("Reset"));
	test_button->set_text(TTR("Test Connection"));
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
	system_prompt_edit->set_text(settings.system_prompt);
	status_label->set_text(TTR("AI settings loaded."));
}

void AIConfigPanel::_save_settings() {
	AISettingsData settings;
	settings.base_url = base_url_edit->get_text().strip_edges();
	settings.model = model_edit->get_text().strip_edges();
	settings.api_key = api_key_edit->get_text();
	settings.temperature = temperature_spin->get_value();
	settings.max_tokens = max_tokens_spin->get_value();
	settings.system_prompt = system_prompt_edit->get_text();
	AISettings::save(settings);
	status_label->set_text(TTR("AI settings saved."));
}

void AIConfigPanel::_reset_settings() {
	AISettings::reset_to_defaults();
	_load_settings();
	status_label->set_text(TTR("AI settings reset to defaults."));
}

void AIConfigPanel::_test_connection() {
	AISettingsData settings;
	settings.base_url = base_url_edit->get_text().strip_edges();
	settings.model = model_edit->get_text().strip_edges();
	settings.api_key = api_key_edit->get_text();
	settings.temperature = temperature_spin->get_value();
	settings.max_tokens = MIN<int>(max_tokens_spin->get_value(), 64);
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

void AIConfigPanel::_test_connection_completed(int p_result, int p_response_code, const String &p_content, const Dictionary &p_json, const String &p_raw_body) {
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

	status_label = memnew(Label);
	status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	status_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	root->add_child(status_label);

	test_service = memnew(AIChatService);
	test_service->connect(SNAME("chat_completed"), callable_mp(this, &AIConfigPanel::_test_connection_completed));
	add_child(test_service, false, INTERNAL_MODE_BACK);

	_update_translations();
	_load_settings();
}
