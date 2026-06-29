/**************************************************************************/
/*  ai_cline_integration.cpp                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#include "ai_cline_integration.h"

#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/main/http_request.h"
#include "scene/main/timer.h"

AIClineIntegration *AIClineIntegration::singleton = nullptr;

AIClineIntegration *AIClineIntegration::get_singleton() {
	return singleton;
}

void AIClineIntegration::cleanup() {
	if (singleton) {
		memdelete(singleton);
		singleton = nullptr;
	}
}

void AIClineIntegration::_bind_methods() {
	ClassDB::bind_method(D_METHOD("connect_to_cline", "server_url"), &AIClineIntegration::connect_to_cline, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("disconnect_from_cline"), &AIClineIntegration::disconnect_from_cline);
	ClassDB::bind_method(D_METHOD("is_connected"), &AIClineIntegration::is_connected);
	ClassDB::bind_method(D_METHOD("get_state"), &AIClineIntegration::get_state);
	ClassDB::bind_method(D_METHOD("login", "token"), &AIClineIntegration::login, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("logout"), &AIClineIntegration::logout);
	ClassDB::bind_method(D_METHOD("is_authenticated"), &AIClineIntegration::is_authenticated);
	ClassDB::bind_method(D_METHOD("send_message", "content", "context"), &AIClineIntegration::send_message, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("send_tool_result", "tool_call_id", "result", "is_error"), &AIClineIntegration::send_tool_result, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("register_tool", "name", "description", "parameters"), &AIClineIntegration::register_tool);
	ClassDB::bind_method(D_METHOD("unregister_tool", "name"), &AIClineIntegration::unregister_tool);
	ClassDB::bind_method(D_METHOD("set_callback", "callback"), &AIClineIntegration::set_callback);
	ClassDB::bind_method(D_METHOD("set_auto_connect", "enabled"), &AIClineIntegration::set_auto_connect);
	ClassDB::bind_method(D_METHOD("set_server_url", "url"), &AIClineIntegration::set_server_url);
	ClassDB::bind_method(D_METHOD("get_server_url"), &AIClineIntegration::get_server_url);
	ClassDB::bind_method(D_METHOD("get_session_info"), &AIClineIntegration::get_session_info);
	ClassDB::bind_method(D_METHOD("get_session_id"), &AIClineIntegration::get_session_id);
}

AIClineIntegration::AIClineIntegration() {
	singleton = this;
	
	http_request = memnew(HTTPRequest);
	http_request->connect("request_completed", callable_mp(this, &AIClineIntegration::_on_http_request_completed));
	
	poll_timer = memnew(Timer);
	poll_timer->set_wait_time(poll_interval / 1000.0);
	poll_timer->connect("timeout", callable_mp(this, &AIClineIntegration::_on_poll_timeout));
}

AIClineIntegration::~AIClineIntegration() {
	disconnect_from_cline();
	if (http_request) {
		memdelete(http_request);
	}
	if (poll_timer) {
		memdelete(poll_timer);
	}
}

Error AIClineIntegration::connect_to_cline(const String &p_server_url) {
	if (!p_server_url.is_empty()) {
		cline_server_url = p_server_url;
	}
	
	_update_state(AIClineState::CONNECTING);
	
	// Check health endpoint
	String health_url = cline_server_url + "/api/mcp/health";
	Error err = http_request->request(health_url, PackedStringArray(), HTTPClient::METHOD_GET);
	if (err != OK) {
		_update_state(AIClineState::ERROR);
		return err;
	}
	
	return OK;
}

void AIClineIntegration::disconnect_from_cline() {
	poll_timer->stop();
	_update_state(AIClineState::DISCONNECTED);
	session_id = "";
	auth_token = "";
}

bool AIClineIntegration::is_connected() const {
	return state == AIClineState::CONNECTED || state == AIClineState::AUTHENTICATED;
}

int AIClineIntegration::get_state() const {
	return static_cast<int>(state);
}

Error AIClineIntegration::login(const String &p_token) {
	if (p_token.is_empty()) {
		// Generate a session-based token
		auth_token = "session_" + String::num_int64(OS::get_singleton()->get_ticks_usec());
	} else {
		auth_token = p_token;
	}
	
	_update_state(AIClineState::AUTHENTICATED);
	
	// Start polling for messages
	poll_timer->start();
	
	if (callback.is_valid()) {
		Dictionary cb_dict;
		cb_dict["type"] = "login_success";
		cb_dict["session_id"] = session_id;
		cb_dict["auth_token"] = auth_token;
		callback.call(cb_dict);
	}
	
	return OK;
}

void AIClineIntegration::logout() {
	poll_timer->stop();
	auth_token = "";
	_update_state(AIClineState::CONNECTED);
	
	if (callback.is_valid()) {
		Dictionary cb_dict;
		cb_dict["type"] = "session_ended";
		callback.call(cb_dict);
	}
}

bool AIClineIntegration::is_authenticated() const {
	return state == AIClineState::AUTHENTICATED && !auth_token.is_empty();
}

Error AIClineIntegration::send_message(const String &p_content, const Dictionary &p_context) {
	if (!is_connected()) {
		return ERR_UNAVAILABLE;
	}
	
	Dictionary payload;
	payload["type"] = "message";
	payload["content"] = p_content;
	payload["context"] = p_context;
	payload["session_id"] = session_id;
	
	String json = JSON::stringify(payload);
	PackedStringArray headers;
	headers.push_back("Content-Type: application/json");
	if (!auth_token.is_empty()) {
		headers.push_back("Authorization: Bearer " + auth_token);
	}
	
	return http_request->request(cline_server_url + "/api/cline/message", headers, HTTPClient::METHOD_POST, json);
}

Error AIClineIntegration::send_tool_result(const String &p_tool_call_id, const String &p_result, bool p_is_error) {
	if (!is_connected()) {
		return ERR_UNAVAILABLE;
	}
	
	Dictionary payload;
	payload["type"] = "tool_result";
	payload["tool_call_id"] = p_tool_call_id;
	payload["result"] = p_result;
	payload["is_error"] = p_is_error;
	payload["session_id"] = session_id;
	
	String json = JSON::stringify(payload);
	PackedStringArray headers;
	headers.push_back("Content-Type: application/json");
	if (!auth_token.is_empty()) {
		headers.push_back("Authorization: Bearer " + auth_token);
	}
	
	return http_request->request(cline_server_url + "/api/cline/tool_result", headers, HTTPClient::METHOD_POST, json);
}

Error AIClineIntegration::register_tool(const String &p_name, const String &p_description, const Dictionary &p_parameters) {
	if (!is_connected()) {
		return ERR_UNAVAILABLE;
	}
	
	Dictionary payload;
	payload["type"] = "register_tool";
	payload["name"] = p_name;
	payload["description"] = p_description;
	payload["parameters"] = p_parameters;
	payload["session_id"] = session_id;
	
	String json = JSON::stringify(payload);
	PackedStringArray headers;
	headers.push_back("Content-Type: application/json");
	if (!auth_token.is_empty()) {
		headers.push_back("Authorization: Bearer " + auth_token);
	}
	
	return http_request->request(cline_server_url + "/api/cline/register_tool", headers, HTTPClient::METHOD_POST, json);
}

Error AIClineIntegration::unregister_tool(const String &p_name) {
	if (!is_connected()) {
		return ERR_UNAVAILABLE;
	}
	
	Dictionary payload;
	payload["type"] = "unregister_tool";
	payload["name"] = p_name;
	payload["session_id"] = session_id;
	
	String json = JSON::stringify(payload);
	PackedStringArray headers;
	headers.push_back("Content-Type: application/json");
	if (!auth_token.is_empty()) {
		headers.push_back("Authorization: Bearer " + auth_token);
	}
	
	return http_request->request(cline_server_url + "/api/cline/unregister_tool", headers, HTTPClient::METHOD_POST, json);
}

void AIClineIntegration::set_callback(const Callable &p_callback) {
	callback = p_callback;
}

void AIClineIntegration::set_auto_connect(bool p_enabled) {
	auto_connect = p_enabled;
}

void AIClineIntegration::set_server_url(const String &p_url) {
	cline_server_url = p_url;
}

String AIClineIntegration::get_server_url() const {
	return cline_server_url;
}

Dictionary AIClineIntegration::get_session_info() const {
	Dictionary info;
	info["state"] = state;
	info["server_url"] = cline_server_url;
	info["session_id"] = session_id;
	info["is_authenticated"] = is_authenticated();
	info["auto_connect"] = auto_connect;
	return info;
}

String AIClineIntegration::get_session_id() const {
	return session_id;
}

void AIClineIntegration::_on_http_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	String response = String::utf8((const char *)p_body.ptr(), p_body.size());
	
	if (state == AIClineState::CONNECTING) {
		if (p_result == HTTPRequest::RESULT_SUCCESS && p_response_code == 200) {
			// Generate session ID
			session_id = "cline_" + String::num_int64(OS::get_singleton()->get_ticks_usec());
			_update_state(AIClineState::CONNECTED);
			
			// Auto-login if enabled
			if (auto_connect) {
				login();
			}
		} else {
			_update_state(AIClineState::ERROR);
		}
		return;
	}
	
	// Handle authenticated responses
	if (p_response_code == 200 && !response.is_empty()) {
		JSON json;
		if (json.parse(response) == OK) {
			Dictionary result = json.get_data();
			if (callback.is_valid()) {
				Dictionary cb_dict;
				cb_dict["type"] = "message_received";
				cb_dict["data"] = result;
				callback.call(cb_dict);
			}
		}
	}
}

void AIClineIntegration::_on_poll_timeout() {
	if (!is_connected()) {
		poll_timer->stop();
		return;
	}
	
	// Poll for new messages from Cline
	PackedStringArray headers;
	if (!auth_token.is_empty()) {
		headers.push_back("Authorization: Bearer " + auth_token);
	}
	headers.push_back("X-Session-ID: " + session_id);
	
	http_request->request(cline_server_url + "/api/cline/poll", headers, HTTPClient::METHOD_GET);
}

void AIClineIntegration::_update_state(AIClineState p_new_state) {
	AIClineState old_state = state;
	state = p_new_state;
	
	if (callback.is_valid() && old_state != p_new_state) {
		Dictionary cb_dict;
		switch (p_new_state) {
			case AIClineState::CONNECTED:
				cb_dict["type"] = "login_success";
				break;
			case AIClineState::ERROR:
				cb_dict["type"] = "login_failed";
				break;
			default:
				break;
		}
		if (cb_dict.has("type")) {
			callback.call(cb_dict);
		}
	}
}