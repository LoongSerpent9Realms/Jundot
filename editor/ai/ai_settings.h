/**************************************************************************/
/*  ai_settings.h                                                          */
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

#pragma once

#include "core/string/ustring.h"

struct AISettingsData {
	String base_url;
	String model;
	String api_key;
	double temperature = 0.7;
	int max_tokens = 1024;
	String system_prompt;
};

class AISettings {
public:
	static constexpr const char *BASE_URL_KEY = "ai_assistant/base_url";
	static constexpr const char *MODEL_KEY = "ai_assistant/model";
	static constexpr const char *API_KEY_KEY = "ai_assistant/api_key";
	static constexpr const char *TEMPERATURE_KEY = "ai_assistant/temperature";
	static constexpr const char *MAX_TOKENS_KEY = "ai_assistant/max_tokens";
	static constexpr const char *SYSTEM_PROMPT_KEY = "ai_assistant/system_prompt";

	static String get_default_base_url();
	static String get_default_model();
	static String get_default_system_prompt();

	static void ensure_defaults();
	static AISettingsData load();
	static void save(const AISettingsData &p_settings);
	static void reset_to_defaults();
};
