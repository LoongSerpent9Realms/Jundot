/*  ai_jundot_plugin_backend.cpp                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#include "ai_jundot_plugin_backend.h"

#include "ai_http_response_text.h"

#include "core/io/http_client.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "editor/settings/editor_settings.h"
#include "scene/main/http_request.h"
#include "scene/main/timer.h"

static constexpr uint64_t JUNDOT_PLUGIN_MIN_REQUEST_INTERVAL_USEC = 2500000;
static constexpr uint64_t JUNDOT_PLUGIN_RATE_LIMIT_BACKOFF_USEC = 30000000;

void AIJundotPluginBackend::_ensure_http_request() {
	if (http_request) {
		return;
	}

	http_request = memnew(HTTPRequest);
	http_request->set_name("AIJundotPluginHTTPRequest");
	http_request->set_use_threads(true);
	http_request->set_timeout(300.0);

	if (EditorSettings::get_singleton()) {
		const String proxy_host = EDITOR_GET("network/http_proxy/host");
		const int proxy_port = EDITOR_GET("network/http_proxy/port");
		http_request->set_http_proxy(proxy_host, proxy_port);
		http_request->set_https_proxy(proxy_host, proxy_port);
	}

	http_request->connect(SNAME("request_completed"), callable_mp(this, &AIJundotPluginBackend::_request_completed));
	add_child(http_request, false, INTERNAL_MODE_BACK);
}

void AIJundotPluginBackend::_ensure_cooldown_timer() {
	if (cooldown_timer) {
		return;
	}

	cooldown_timer = memnew(Timer);
	cooldown_timer->set_name("AIJundotPluginCooldownTimer");
	cooldown_timer->set_one_shot(true);
	cooldown_timer->connect("timeout", callable_mp(this, &AIJundotPluginBackend::_send_pending_request));
	add_child(cooldown_timer, false, INTERNAL_MODE_BACK);
}

String AIJundotPluginBackend::_build_plugin_url() const {
	String url = settings.jundot_ai_plugin_url.strip_edges();
	while (url.ends_with("/")) {
		url = url.substr(0, url.length() - 1);
	}
	if (!url.ends_with("/ai/chat")) {
		url += "/ai/chat";
	}
	return url;
}

void AIJundotPluginBackend::_emit_completed(int p_result, int p_response_code, const String &p_content, const Dictionary &p_json, const String &p_raw_body, double p_elapsed_seconds) {
	requesting = false;
	last_request_end_usec = OS::get_singleton()->get_ticks_usec();
	if (p_response_code == HTTPClient::RESPONSE_TOO_MANY_REQUESTS || p_content.to_lower().contains("too many requests")) {
		rate_limit_backoff_until_usec = last_request_end_usec + JUNDOT_PLUGIN_RATE_LIMIT_BACKOFF_USEC;
	}
	emit_signal(SNAME("chat_completed"), p_result, p_response_code, p_content, p_json, p_raw_body, p_elapsed_seconds, String(), 0, 0);
}

void AIJundotPluginBackend::_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	double elapsed = 0.0;
	if (request_start_usec > 0) {
		elapsed = (OS::get_singleton()->get_ticks_usec() - request_start_usec) / 1000000.0;
		request_start_usec = 0;
	}

	String body_text;
	if (!p_body.is_empty()) {
		body_text = ai_decode_http_response_text(p_body, p_headers);
	}

	if (p_result != HTTPRequest::RESULT_SUCCESS) {
		String error_text;
		if (p_result == HTTPRequest::RESULT_CANT_CONNECT) {
			error_text = vformat("MiMoCode jundot plugin is not running at %s. Download the packaged MiMoCode plugin from the AI settings panel, install/start it, then retry.", settings.jundot_ai_plugin_url);
		} else {
			error_text = vformat("MiMoCode jundot plugin request failed (result %d).", p_result);
		}
		_emit_completed(p_result, p_response_code, error_text, Dictionary(), body_text, elapsed);
		return;
	}

	Variant parsed = JSON::parse_string(body_text);
	if (parsed.get_type() != Variant::DICTIONARY) {
		String preview = body_text.strip_edges();
		if (preview.length() > 200) {
			preview = preview.substr(0, 200) + "...";
		}
		if (preview.is_empty()) {
			preview = "(empty body)";
		}
		
		String hint = "";
		if (preview.begins_with("<!doctype html>") || preview.begins_with("<html")) {
			hint = "\n\nHint: The plugin returned an HTML page instead of JSON. This usually means the /ai/chat endpoint does not exist on the MiMoCode server. Please ensure you have the latest version of the MiMoCode Jundot plugin installed.";
		}
		
		String error_text = vformat("MiMoCode jundot plugin returned a non-JSON response (HTTP %d): %s%s", p_response_code, preview, hint);
		_emit_completed(HTTPRequest::RESULT_BODY_DECOMPRESS_FAILED, p_response_code, error_text, Dictionary(), body_text, elapsed);
		return;
	}

	Dictionary root = parsed;
	String content = root.get("content", String());
	Dictionary openai_compatible = root.get("openai_compatible", Dictionary());

	if (openai_compatible.is_empty()) {
		if (root.has("choices") && root["choices"].get_type() == Variant::ARRAY) {
			openai_compatible = root;
			Array choices = root["choices"];
			if (!choices.is_empty() && choices[0].get_type() == Variant::DICTIONARY) {
				Dictionary first = choices[0];
				if (first.has("message") && first["message"].get_type() == Variant::DICTIONARY) {
					Dictionary msg = first["message"];
					content = msg.get("content", content);
				}
			}
		} else {
			Dictionary message;
			message["role"] = "assistant";
			message["content"] = content;

			Dictionary choice;
			choice["message"] = message;
			choice["finish_reason"] = root.get("finish_reason", String("stop"));

			Array choices;
			choices.push_back(choice);
			openai_compatible["choices"] = choices;
		}
	}

	_emit_completed(HTTPRequest::RESULT_SUCCESS, p_response_code, content, openai_compatible, body_text, elapsed);
}

void AIJundotPluginBackend::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		_ensure_http_request();
	}
}

void AIJundotPluginBackend::_bind_methods() {
	ClassDB::bind_method(D_METHOD("cancel_request"), &AIJundotPluginBackend::cancel_request);
	ClassDB::bind_method(D_METHOD("is_requesting"), &AIJundotPluginBackend::is_requesting);

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

	ADD_SIGNAL(MethodInfo("chat_stream_data",
			PropertyInfo(Variant::STRING, "delta"),
			PropertyInfo(Variant::STRING, "full_content"),
			PropertyInfo(Variant::INT, "completion_tokens")));
}

void AIJundotPluginBackend::configure(const AISettingsData &p_settings) {
	settings = p_settings;
}

Error AIJundotPluginBackend::send_messages(const Array &p_messages, const Array &p_tools) {
	ERR_FAIL_COND_V_MSG(!is_inside_tree(), ERR_UNCONFIGURED, "AI jundot plugin backend must be inside the scene tree before sending a request.");
	ERR_FAIL_COND_V_MSG(settings.jundot_ai_plugin_id.strip_edges().is_empty(), ERR_UNCONFIGURED, "AI jundot plugin id is empty.");
	ERR_FAIL_COND_V_MSG(settings.jundot_ai_plugin_url.strip_edges().is_empty(), ERR_UNCONFIGURED, "AI jundot plugin URL is empty.");

	_ensure_http_request();
	ERR_FAIL_COND_V_MSG(is_requesting(), ERR_BUSY, "AI jundot plugin backend is already handling a request.");

	const uint64_t now_usec = OS::get_singleton()->get_ticks_usec();
	if (rate_limit_backoff_until_usec > now_usec) {
		_ensure_cooldown_timer();
		pending_messages = p_messages.duplicate(true);
		pending_tools = p_tools.duplicate(true);
		pending_request = true;
		requesting = true;
		const double wait_seconds = double(rate_limit_backoff_until_usec - now_usec) / 1000000.0;
		cooldown_timer->start(wait_seconds);
		return OK;
	}

	if (last_request_end_usec > 0 && now_usec > last_request_end_usec) {
		const uint64_t elapsed_usec = now_usec - last_request_end_usec;
		if (elapsed_usec < JUNDOT_PLUGIN_MIN_REQUEST_INTERVAL_USEC) {
			_ensure_cooldown_timer();
			pending_messages = p_messages.duplicate(true);
			pending_tools = p_tools.duplicate(true);
			pending_request = true;
			requesting = true;
			const double wait_seconds = double(JUNDOT_PLUGIN_MIN_REQUEST_INTERVAL_USEC - elapsed_usec) / 1000000.0;
			cooldown_timer->start(wait_seconds);
			return OK;
		}
	}

	return _send_messages_now(p_messages, p_tools);
}

Error AIJundotPluginBackend::_send_messages_now(const Array &p_messages, const Array &p_tools) {
	Dictionary payload;
	payload["plugin_id"] = settings.jundot_ai_plugin_id;
	payload["action"] = "send_message";
	payload["messages"] = p_messages;
	payload["tools"] = p_tools;
	payload["context_mode"] = (settings.context_mode == AIContextMode::ENGINE) ? "engine" : "project";
	payload["output_language"] = settings.output_language;

	Vector<String> headers;
	headers.push_back("Content-Type: application/json; charset=utf-8");
	headers.push_back("Accept: application/json");
	headers.push_back("Accept-Charset: utf-8");

	requesting = true;
	request_start_usec = OS::get_singleton()->get_ticks_usec();
	const Error err = http_request->request(_build_plugin_url(), headers, HTTPClient::METHOD_POST, JSON::stringify(payload));
	if (err != OK) {
		requesting = false;
		request_start_usec = 0;
	}
	return err;
}

void AIJundotPluginBackend::_send_pending_request() {
	if (!pending_request) {
		return;
	}

	Array messages = pending_messages;
	Array tools = pending_tools;
	pending_messages.clear();
	pending_tools.clear();
	pending_request = false;

	const Error err = _send_messages_now(messages, tools);
	if (err != OK) {
		_emit_completed(HTTPRequest::RESULT_CANT_CONNECT, 0, "MiMoCode jundot plugin request could not start after waiting for the local request cooldown.", Dictionary(), String(), 0.0);
	}
}

void AIJundotPluginBackend::cancel_request() {
	pending_request = false;
	pending_messages.clear();
	pending_tools.clear();
	if (cooldown_timer) {
		cooldown_timer->stop();
	}
	requesting = false;
	if (http_request) {
		http_request->cancel_request();
	}
}

bool AIJundotPluginBackend::is_requesting() const {
	return requesting || (http_request && http_request->get_http_client_status() != HTTPClient::STATUS_DISCONNECTED);
}
