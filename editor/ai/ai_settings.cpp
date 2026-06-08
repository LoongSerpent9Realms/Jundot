/**************************************************************************/
/*  ai_settings.cpp                                                        */
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

#include "ai_settings.h"

#include "editor/settings/editor_settings.h"

String AISettings::get_default_base_url() {
	return "https://api.openai.com/v1";
}

String AISettings::get_default_model() {
	return "gpt-4.1";
}

String AISettings::get_default_system_prompt() {
	return TTR("You are an AI assistant inside the Jundot editor. Help analyze project issues, evaluate feature necessity, and propose confirmed next steps.");
}

void AISettings::ensure_defaults() {
	EDITOR_DEF(BASE_URL_KEY, get_default_base_url());
	EDITOR_DEF(MODEL_KEY, get_default_model());
	EDITOR_DEF(API_KEY_KEY, "");
	EDITOR_DEF(TEMPERATURE_KEY, 0.7);
	EDITOR_DEF(MAX_TOKENS_KEY, 1024);
	EDITOR_DEF(SYSTEM_PROMPT_KEY, get_default_system_prompt());
}

AISettingsData AISettings::load() {
	ensure_defaults();

	AISettingsData settings;
	settings.base_url = EDITOR_GET(BASE_URL_KEY);
	settings.model = EDITOR_GET(MODEL_KEY);
	settings.api_key = EDITOR_GET(API_KEY_KEY);
	settings.temperature = EDITOR_GET(TEMPERATURE_KEY);
	settings.max_tokens = EDITOR_GET(MAX_TOKENS_KEY);
	settings.system_prompt = EDITOR_GET(SYSTEM_PROMPT_KEY);
	return settings;
}

void AISettings::save(const AISettingsData &p_settings) {
	EditorSettings *editor_settings = EditorSettings::get_singleton();
	ERR_FAIL_NULL(editor_settings);

	editor_settings->set(BASE_URL_KEY, p_settings.base_url);
	editor_settings->set(MODEL_KEY, p_settings.model);
	editor_settings->set(API_KEY_KEY, p_settings.api_key);
	editor_settings->set(TEMPERATURE_KEY, p_settings.temperature);
	editor_settings->set(MAX_TOKENS_KEY, p_settings.max_tokens);
	editor_settings->set(SYSTEM_PROMPT_KEY, p_settings.system_prompt);
}

void AISettings::reset_to_defaults() {
	AISettingsData settings;
	settings.base_url = get_default_base_url();
	settings.model = get_default_model();
	settings.api_key = "";
	settings.temperature = 0.7;
	settings.max_tokens = 1024;
	settings.system_prompt = get_default_system_prompt();
	save(settings);
}
