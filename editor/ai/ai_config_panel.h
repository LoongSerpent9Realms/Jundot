/*  ai_config_panel.h                                                      */
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
class GridContainer;
class AIChatService;
class AIUsageAgreementDialog;
class Label;
class LineEdit;
class SpinBox;
class TextEdit;

class AIConfigPanel : public MarginContainer {
	GDCLASS(AIConfigPanel, MarginContainer)

	LineEdit *base_url_edit = nullptr;
	LineEdit *model_edit = nullptr;
	LineEdit *api_key_edit = nullptr;
	SpinBox *temperature_spin = nullptr;
	SpinBox *max_tokens_spin = nullptr;
	SpinBox *context_char_budget_spin = nullptr;
	SpinBox *feature_universality_threshold_spin = nullptr;
	SpinBox *feature_necessity_threshold_spin = nullptr;
	CheckBox *include_project_memories_check = nullptr;
	CheckBox *include_tool_context_check = nullptr;
	CheckBox *auto_suggest_entries_check = nullptr;
	CheckBox *feature_design_philosophy_check = nullptr;
	TextEdit *system_prompt_edit = nullptr;
	Label *title_label = nullptr;
	Label *base_url_label = nullptr;
	Label *model_label = nullptr;
	Label *api_key_label = nullptr;
	Label *temperature_label = nullptr;
	Label *max_tokens_label = nullptr;
	Label *context_char_budget_label = nullptr;
	Label *feature_universality_threshold_label = nullptr;
	Label *feature_necessity_threshold_label = nullptr;
	Label *system_prompt_label = nullptr;
	Label *usage_notice_label = nullptr;
	Label *status_label = nullptr;
	AIChatService *test_service = nullptr;
	AIUsageAgreementDialog *usage_agreement_dialog = nullptr;
	Button *save_button = nullptr;
	Button *reset_button = nullptr;
	Button *test_button = nullptr;
	Button *view_agreement_button = nullptr;
	Button *reset_agreement_button = nullptr;

	void _load_settings();
	void _save_settings();
	void _reset_settings();
	void _test_connection();
	void _view_usage_agreement();
	void _reset_usage_agreement();
	void _test_connection_completed(int p_result, int p_response_code, const String &p_content, const Dictionary &p_json, const String &p_raw_body, double p_elapsed_seconds, const String &p_think_content, int p_prompt_tokens, int p_completion_tokens);
	void _update_translations();

	LineEdit *_add_line_edit_row(GridContainer *p_grid, Label **r_label, const String &p_label, const String &p_placeholder = String(), bool p_secret = false);
	SpinBox *_add_spin_box_row(GridContainer *p_grid, Label **r_label, const String &p_label, double p_min, double p_max, double p_step);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	AIConfigPanel();
};
