/**************************************************************************/
/*  ai_interface.cpp                                                      */
/**************************************************************************/

#include "ai_interface.h"

#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/main/http_request.h"

String AIInterface::_build_chat_url() const {
	String url = api_base_url.strip_edges();
	while (url.ends_with("/")) {
		url = url.substr(0, url.length() - 1);
	}
	if (!url.ends_with("/chat/completions")) {
		url += "/chat/completions";
	}
	return url;
}

void AIInterface::_ensure_http_request() {
	if (http_request) {
		return;
	}

	http_request = memnew(HTTPRequest);
	http_request->set_name("AIHTTPRequest");
	http_request->set_use_threads(use_threads);
	http_request->set_timeout(timeout);
	http_request->connect(SNAME("request_completed"), callable_mp(this, &AIInterface::_request_completed));
	add_child(http_request, false, INTERNAL_MODE_BACK);
}

String AIInterface::_extract_text_from_response(const Variant &p_data) const {
	if (p_data.get_type() != Variant::DICTIONARY) {
		return String();
	}

	Dictionary root = p_data;
	if (!root.has("choices")) {
		if (root.has("error")) {
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

void AIInterface::_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	String body_text;
	if (!p_body.is_empty()) {
		body_text = String::utf8((const char *)p_body.ptr(), p_body.size());
	}

	Variant parsed = JSON::parse_string(body_text);
	String content = _extract_text_from_response(parsed);
	Dictionary json;
	if (parsed.get_type() == Variant::DICTIONARY) {
		json = parsed;
	}
	emit_signal(SNAME("chat_completed"), p_result, p_response_code, content, json, body_text);
}

void AIInterface::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		_ensure_http_request();
	}
}

void AIInterface::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_api_base_url", "url"), &AIInterface::set_api_base_url);
	ClassDB::bind_method(D_METHOD("get_api_base_url"), &AIInterface::get_api_base_url);
	ClassDB::bind_method(D_METHOD("set_api_key", "key"), &AIInterface::set_api_key);
	ClassDB::bind_method(D_METHOD("get_api_key"), &AIInterface::get_api_key);
	ClassDB::bind_method(D_METHOD("set_model", "model"), &AIInterface::set_model);
	ClassDB::bind_method(D_METHOD("get_model"), &AIInterface::get_model);
	ClassDB::bind_method(D_METHOD("set_system_prompt", "prompt"), &AIInterface::set_system_prompt);
	ClassDB::bind_method(D_METHOD("get_system_prompt"), &AIInterface::get_system_prompt);
	ClassDB::bind_method(D_METHOD("set_temperature", "temperature"), &AIInterface::set_temperature);
	ClassDB::bind_method(D_METHOD("get_temperature"), &AIInterface::get_temperature);
	ClassDB::bind_method(D_METHOD("set_max_tokens", "max_tokens"), &AIInterface::set_max_tokens);
	ClassDB::bind_method(D_METHOD("get_max_tokens"), &AIInterface::get_max_tokens);
	ClassDB::bind_method(D_METHOD("set_timeout", "timeout"), &AIInterface::set_timeout);
	ClassDB::bind_method(D_METHOD("get_timeout"), &AIInterface::get_timeout);
	ClassDB::bind_method(D_METHOD("set_use_threads", "enable"), &AIInterface::set_use_threads);
	ClassDB::bind_method(D_METHOD("is_using_threads"), &AIInterface::is_using_threads);

	ClassDB::bind_method(D_METHOD("send_chat", "message"), &AIInterface::send_chat);
	ClassDB::bind_method(D_METHOD("send_messages", "messages"), &AIInterface::send_messages);
	ClassDB::bind_method(D_METHOD("cancel_request"), &AIInterface::cancel_request);
	ClassDB::bind_method(D_METHOD("is_requesting"), &AIInterface::is_requesting);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "api_base_url"), "set_api_base_url", "get_api_base_url");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "api_key"), "set_api_key", "get_api_key");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "model"), "set_model", "get_model");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "system_prompt", PROPERTY_HINT_MULTILINE_TEXT), "set_system_prompt", "get_system_prompt");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "temperature", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_temperature", "get_temperature");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_tokens", PROPERTY_HINT_RANGE, "1,200000,1,or_greater"), "set_max_tokens", "get_max_tokens");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "timeout", PROPERTY_HINT_RANGE, "0,3600,0.1,suffix:s"), "set_timeout", "get_timeout");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_threads"), "set_use_threads", "is_using_threads");

	ADD_SIGNAL(MethodInfo("chat_completed",
			PropertyInfo(Variant::INT, "result"),
			PropertyInfo(Variant::INT, "response_code"),
			PropertyInfo(Variant::STRING, "content"),
			PropertyInfo(Variant::DICTIONARY, "json"),
			PropertyInfo(Variant::STRING, "raw_body")));
}

void AIInterface::set_api_base_url(const String &p_url) {
	api_base_url = p_url;
}

String AIInterface::get_api_base_url() const {
	return api_base_url;
}

void AIInterface::set_api_key(const String &p_key) {
	api_key = p_key;
}

String AIInterface::get_api_key() const {
	return api_key;
}

void AIInterface::set_model(const String &p_model) {
	model = p_model;
}

String AIInterface::get_model() const {
	return model;
}

void AIInterface::set_system_prompt(const String &p_prompt) {
	system_prompt = p_prompt;
}

String AIInterface::get_system_prompt() const {
	return system_prompt;
}

void AIInterface::set_temperature(double p_temperature) {
	temperature = p_temperature;
}

double AIInterface::get_temperature() const {
	return temperature;
}

void AIInterface::set_max_tokens(int p_max_tokens) {
	max_tokens = p_max_tokens;
}

int AIInterface::get_max_tokens() const {
	return max_tokens;
}

void AIInterface::set_timeout(double p_timeout) {
	timeout = p_timeout;
	if (http_request) {
		http_request->set_timeout(timeout);
	}
}

double AIInterface::get_timeout() const {
	return timeout;
}

void AIInterface::set_use_threads(bool p_use_threads) {
	use_threads = p_use_threads;
	if (http_request) {
		http_request->set_use_threads(use_threads);
	}
}

bool AIInterface::is_using_threads() const {
	return use_threads;
}

Error AIInterface::send_chat(const String &p_message) {
	Array messages;
	if (!system_prompt.is_empty()) {
		Dictionary system_message;
		system_message["role"] = "system";
		system_message["content"] = system_prompt;
		messages.push_back(system_message);
	}

	Dictionary user_message;
	user_message["role"] = "user";
	user_message["content"] = p_message;
	messages.push_back(user_message);

	return send_messages(messages);
}

Error AIInterface::send_messages(const Array &p_messages) {
	ERR_FAIL_COND_V_MSG(!is_inside_tree(), ERR_UNCONFIGURED, "AIInterface must be inside the scene tree before sending a request.");
	ERR_FAIL_COND_V_MSG(api_key.is_empty(), ERR_UNCONFIGURED, "AIInterface.api_key is empty.");
	_ensure_http_request();

	Dictionary payload;
	payload["model"] = model;
	payload["messages"] = p_messages;
	payload["temperature"] = temperature;
	payload["max_tokens"] = max_tokens;

	Vector<String> headers;
	headers.push_back("Content-Type: application/json");
	headers.push_back("Authorization: Bearer " + api_key);

	return http_request->request(_build_chat_url(), headers, HTTPClient::METHOD_POST, JSON::stringify(payload));
}

void AIInterface::cancel_request() {
	if (http_request) {
		http_request->cancel_request();
	}
}

bool AIInterface::is_requesting() const {
	return http_request && http_request->get_http_client_status() != HTTPClient::STATUS_DISCONNECTED;
}
