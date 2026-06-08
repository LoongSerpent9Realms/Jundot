/**************************************************************************/
/*  ai_chat_service.cpp                                                    */
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

#include "ai_chat_service.h"

#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/settings/editor_settings.h"
#include "scene/main/http_request.h"

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

void AIChatService::_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	String body_text;
	if (!p_body.is_empty()) {
		body_text = String::utf8((const char *)p_body.ptr(), p_body.size());
	}

	const Variant parsed = JSON::parse_string(body_text);
	const String content = _extract_text_from_response(parsed);
	Dictionary json;
	if (parsed.get_type() == Variant::DICTIONARY) {
		json = parsed;
	}

	emit_signal(SNAME("chat_completed"), p_result, p_response_code, content, json, body_text);
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
			PropertyInfo(Variant::STRING, "raw_body")));
}

void AIChatService::configure(const AISettingsData &p_settings) {
	settings = p_settings;
}

Error AIChatService::send_chat(const String &p_message) {
	Array messages;
	if (!settings.system_prompt.is_empty()) {
		Dictionary system_message;
		system_message["role"] = "system";
		system_message["content"] = settings.system_prompt;
		messages.push_back(system_message);
	}

	Dictionary user_message;
	user_message["role"] = "user";
	user_message["content"] = p_message;
	messages.push_back(user_message);

	return send_messages(messages);
}

Error AIChatService::send_messages(const Array &p_messages) {
	ERR_FAIL_COND_V_MSG(!is_inside_tree(), ERR_UNCONFIGURED, "AIChatService must be inside the scene tree before sending a request.");
	ERR_FAIL_COND_V_MSG(settings.base_url.strip_edges().is_empty(), ERR_UNCONFIGURED, "AI base URL is empty.");
	ERR_FAIL_COND_V_MSG(settings.model.strip_edges().is_empty(), ERR_UNCONFIGURED, "AI model is empty.");
	ERR_FAIL_COND_V_MSG(settings.api_key.is_empty(), ERR_UNCONFIGURED, "AI API key is empty.");

	_ensure_http_request();

	Dictionary payload;
	payload["model"] = settings.model;
	payload["messages"] = p_messages;
	payload["temperature"] = settings.temperature;
	payload["max_tokens"] = settings.max_tokens;

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
