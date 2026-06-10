/*  ai_chat_service.cpp                                                    */
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

#include "ai_chat_service.h"

#include "ai_settings.h"

#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "editor/settings/editor_settings.h"
#include "scene/main/http_request.h"

static String _get_json_error(const String &p_text) {
	// Try to extract a human-readable error from a non-JSON response.
	if (p_text.is_empty()) {
		return "Empty response from server.";
	}
	// If it looks like HTML, extract a summary.
	if (p_text.begins_with("<")) {
		int title_start = p_text.find("<title>");
		if (title_start >= 0) {
			int title_end = p_text.find("</title>", title_start);
			if (title_end > title_start) {
				return "Server returned HTML: " + p_text.substr(title_start + 7, title_end - title_start - 7);
			}
		}
		return "Server returned an HTML page instead of JSON (response length: " + itos(p_text.length()) + " chars).";
	}
	// Truncate very long responses.
	String summary = p_text.substr(0, 300);
	if (p_text.length() > 300) {
		summary += "...";
	}
	return "Failed to parse JSON response. Body preview: " + summary;
}

String AIChatService::_build_chat_url() const {
	String url = settings.base_url.strip_edges();
	while (url.ends_with("/")) {
		url = url.substr(0, url.length() - 1);
	}
	if (!url.ends_with("/chat/completions")) {
		url += "/chat/completions";
	}
	return url;
}

void AIChatService::_ensure_http_request() {
	if (http_request) {
		return;
	}

	http_request = memnew(HTTPRequest);
	http_request->set_name("AIChatHTTPRequest");
	http_request->set_use_threads(use_threads);
	http_request->set_timeout(timeout);

	const String proxy_host = EDITOR_GET("network/http_proxy/host");
	const int proxy_port = EDITOR_GET("network/http_proxy/port");
	http_request->set_http_proxy(proxy_host, proxy_port);
	http_request->set_https_proxy(proxy_host, proxy_port);

	http_request->connect(SNAME("request_completed"), callable_mp(this, &AIChatService::_request_completed));
	add_child(http_request, false, INTERNAL_MODE_BACK);
}

String AIChatService::_extract_text_from_response(const Variant &p_data) const {
	if (p_data.get_type() != Variant::DICTIONARY) {
		return String();
	}

	Dictionary root = p_data;
	if (!root.has("choices")) {
		if (root.has("error") && root["error"].get_type() == Variant::DICTIONARY) {
			Dictionary error = root["error"];
			return error.get("message", String());
		}
		return String();
	}

	Array choices = root["choices"];
	if (choices.is_empty() || choices[0].get_type() != Variant::DICTIONARY) {
		return String();
	}

	Dictionary first_choice = choices[0];
	if (first_choice.has("message") && first_choice["message"].get_type() == Variant::DICTIONARY) {
		Dictionary message = first_choice["message"];
		return message.get("content", String());
	}
	return first_choice.get("text", String());
}

void AIChatService::_extract_usage_from_response(const Variant &p_data, int &r_prompt_tokens, int &r_completion_tokens) const {
	r_prompt_tokens = 0;
	r_completion_tokens = 0;
	if (p_data.get_type() != Variant::DICTIONARY) {
		return;
	}
	Dictionary root = p_data;
	if (root.has("usage") && root["usage"].get_type() == Variant::DICTIONARY) {
		Dictionary usage = root["usage"];
		r_prompt_tokens = usage.get("prompt_tokens", 0);
		r_completion_tokens = usage.get("completion_tokens", 0);
	}
}

void AIChatService::_extract_think_from_content(String &r_content, String &r_think) const {
	r_think = String();
	// Extract content from <think> ... </think> tags.
	// Some models (like DeepSeek R1) wrap their reasoning in these tags.
	const String think_start = "<think>";
	const String think_end = "</think>";
	int start = r_content.find(think_start);
	if (start >= 0) {
		int end = r_content.find(think_end, start + think_start.length());
		if (end > start) {
			r_think = r_content.substr(start + think_start.length(), end - start - think_start.length()).strip_edges();
			// Remove the think block from the main content.
			String before = r_content.substr(0, start);
			String after = r_content.substr(end + think_end.length());
			r_content = (before + after).strip_edges();
		}
	}
}

static String _http_result_to_string(int p_result) {
	switch (p_result) {
		case 0: return "Success";
		case 1: return "Chunked body size mismatch";
		case 2: return "Cannot connect to server";
		case 3: return "Cannot resolve hostname";
		case 4: return "Connection error";
		case 5: return "SSL handshake failed";
		case 6: return "No response from server";
		case 7: return "Body decompression failed";
		case 8: return "Request failed (possibly timed out)";
		case 9: return "Cannot open download file";
		case 10: return "Download file write error";
		case 11: return "Redirect limit reached";
		default: return "Unknown error (code: " + itos(p_result) + ")";
	}
}

void AIChatService::_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	// Calculate elapsed time.
	double elapsed = 0.0;
	if (request_start_usec > 0) {
		elapsed = (OS::get_singleton()->get_ticks_usec() - request_start_usec) / 1000000.0;
		request_start_usec = 0;
	}

	String body_text;
	if (!p_body.is_empty()) {
		body_text = String::utf8((const char *)p_body.ptr(), p_body.size());
	}

	Variant parsed;
	String parse_error;
	String content;

	if (p_result != 0) {
		// HTTP request itself failed (not a server-side HTTP error).
		String err = _http_result_to_string(p_result);
		parse_error = "Request failed: " + err;
		if (p_result == 8) {
			parse_error += vformat(" (timeout is %.0f seconds).", timeout);
		}
		ERR_PRINT("AIChatService: " + parse_error);
		content = "Error: " + parse_error;
	} else if (p_response_code >= 400) {
		// Server returned an HTTP error status.
		parse_error = vformat("Server returned HTTP %d.", p_response_code);
		ERR_PRINT("AIChatService: " + parse_error);
		if (!body_text.is_empty()) {
			ERR_PRINT("AIChatService: Response body:\n" + body_text.substr(0, 2000));
		}
		content = "Error: " + parse_error;
	} else if (body_text.is_empty()) {
		parse_error = "Empty response body (HTTP " + itos(p_response_code) + ")";
		ERR_PRINT("AIChatService: " + parse_error);
		content = "Error: " + parse_error;
	} else {
		parsed = JSON::parse_string(body_text);
		if (parsed.get_type() == Variant::NIL) {
			parse_error = _get_json_error(body_text);
			ERR_PRINT("AIChatService: JSON parse failed. " + parse_error);
			ERR_PRINT("AIChatService: Raw response body:\n" + body_text.substr(0, 2000));
			content = "Error: " + parse_error;
		} else {
			content = _extract_text_from_response(parsed);
		}
	}

	String think_content;
	_extract_think_from_content(content, think_content);

	int prompt_tokens = 0;
	int completion_tokens = 0;
	_extract_usage_from_response(parsed, prompt_tokens, completion_tokens);

	Dictionary json;
	if (parsed.get_type() == Variant::DICTIONARY) {
		json = parsed;
	}

	emit_signal(SNAME("chat_completed"), p_result, p_response_code, content, json, body_text, elapsed, think_content, prompt_tokens, completion_tokens);
}

void AIChatService::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		_ensure_http_request();
	}
}

void AIChatService::_bind_methods() {
	ClassDB::bind_method(D_METHOD("send_chat", "message"), &AIChatService::send_chat);
	ClassDB::bind_method(D_METHOD("cancel_request"), &AIChatService::cancel_request);
	ClassDB::bind_method(D_METHOD("is_requesting"), &AIChatService::is_requesting);

	ADD_SIGNAL(MethodInfo("chat_completed",
			PropertyInfo(Variant::INT, "result"),
			PropertyInfo(Variant::INT, "response_code"),
			PropertyInfo(Variant::STRING, "content"),
			PropertyInfo(Variant::DICTIONARY, "json"),
			PropertyInfo(Variant::STRING, "raw_body"),
			PropertyInfo(Variant::FLOAT, "elapsed_seconds"),
			PropertyInfo(Variant::STRING, "think_content"),
			PropertyInfo(Variant::INT, "prompt_tokens"),
			PropertyInfo(Variant::INT, "completion_tokens")));
}

void AIChatService::configure(const AISettingsData &p_settings) {
	settings = p_settings;
}

Error AIChatService::send_chat(const String &p_message) {
	Array messages;
	String effective_prompt = AISettings::get_effective_system_prompt(settings);
	if (!effective_prompt.is_empty()) {
		Dictionary system_message;
		system_message["role"] = "system";
		system_message["content"] = effective_prompt;
		messages.push_back(system_message);
	}

	Dictionary user_message;
	user_message["role"] = "user";
	user_message["content"] = p_message;
	messages.push_back(user_message);

	return send_messages(messages);
}

Error AIChatService::send_messages(const Array &p_messages) {
	return send_messages(p_messages, Array());
}

Error AIChatService::send_messages(const Array &p_messages, const Array &p_tools) {
	ERR_FAIL_COND_V_MSG(!is_inside_tree(), ERR_UNCONFIGURED, "AIChatService must be inside the scene tree before sending a request.");
	ERR_FAIL_COND_V_MSG(settings.base_url.strip_edges().is_empty(), ERR_UNCONFIGURED, "AI base URL is empty.");
	ERR_FAIL_COND_V_MSG(settings.model.strip_edges().is_empty(), ERR_UNCONFIGURED, "AI model is empty.");
	ERR_FAIL_COND_V_MSG(settings.api_key.is_empty(), ERR_UNCONFIGURED, "AI API key is empty.");

	_ensure_http_request();

	request_start_usec = OS::get_singleton()->get_ticks_usec();

	Dictionary payload;
	payload["model"] = settings.model;
	payload["messages"] = p_messages;
	payload["temperature"] = settings.temperature;
	payload["max_tokens"] = settings.max_tokens;

	if (!p_tools.is_empty()) {
		payload["tools"] = p_tools;
		payload["tool_choice"] = "auto";
	}

	Vector<String> headers;
	headers.push_back("Content-Type: application/json");
	headers.push_back("Authorization: Bearer " + settings.api_key);

	return http_request->request(_build_chat_url(), headers, HTTPClient::METHOD_POST, JSON::stringify(payload));
}

void AIChatService::cancel_request() {
	if (http_request) {
		http_request->cancel_request();
	}
}

bool AIChatService::is_requesting() const {
	return http_request && http_request->get_http_client_status() != HTTPClient::STATUS_DISCONNECTED;
}
