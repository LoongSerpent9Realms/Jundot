/*  ai_chat_service.h                                                     */
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

#include "core/io/http_client.h"
#include "core/typedefs.h"
#include "core/variant/dictionary.h"
#include "editor/ai/ai_settings.h"
#include "scene/main/node.h"

class AIJundotPluginBackend;
class HTTPRequest;

class AIChatService : public Node {
	GDCLASS(AIChatService, Node)

	HTTPRequest *http_request = nullptr;
	AIJundotPluginBackend *jundot_plugin_backend = nullptr;
	AISettingsData settings;
	double timeout = 300.0;
	bool use_threads = true;
	uint64_t request_start_usec = 0;

	bool streaming = false;
	String stream_buffer;
	int stream_prompt_tokens = 0;
	int stream_completion_tokens = 0;
	Array stream_tool_calls;

	String _build_chat_url() const;
	void _ensure_http_request();
	void _ensure_jundot_plugin_backend();
	bool _should_use_jundot_plugin_backend() const;
	void _request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	void _jundot_plugin_chat_completed(int p_result, int p_response_code, const String &p_content, const Dictionary &p_json, const String &p_raw_body, double p_elapsed_seconds, const String &p_think_content, int p_prompt_tokens, int p_completion_tokens);
	void _jundot_plugin_stream_data(const String &p_delta, const String &p_full_content, int p_completion_tokens);
	String _extract_text_from_response(const Variant &p_data) const;
	void _extract_usage_from_response(const Variant &p_data, int &r_prompt_tokens, int &r_completion_tokens) const;
	void _extract_think_from_content(String &r_content, String &r_think) const;
	void _process_stream_chunk(const String &p_chunk);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void configure(const AISettingsData &p_settings);
	Error send_chat(const String &p_message);
	Error send_messages(const Array &p_messages);
	Error send_messages(const Array &p_messages, const Array &p_tools);
	void cancel_request();
	bool is_requesting() const;

	bool is_streaming() const;
};
