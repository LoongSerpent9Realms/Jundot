/**************************************************************************/
/*  ai_interface.h                                                        */
/**************************************************************************/

#pragma once

#include "core/io/http_client.h"
#include "core/variant/dictionary.h"
#include "scene/main/node.h"

class HTTPRequest;

class AIInterface : public Node {
	GDCLASS(AIInterface, Node);

	String api_base_url = "https://api.openai.com/v1";
	String api_key;
	String model = "gpt-4o-mini";
	String system_prompt;
	double temperature = 0.7;
	int max_tokens = 1024;
	double timeout = 60.0;
	bool use_threads = true;

	HTTPRequest *http_request = nullptr;

	String _build_chat_url() const;
	void _ensure_http_request();
	void _request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	String _extract_text_from_response(const Variant &p_data) const;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_api_base_url(const String &p_url);
	String get_api_base_url() const;

	void set_api_key(const String &p_key);
	String get_api_key() const;

	void set_model(const String &p_model);
	String get_model() const;

	void set_system_prompt(const String &p_prompt);
	String get_system_prompt() const;

	void set_temperature(double p_temperature);
	double get_temperature() const;

	void set_max_tokens(int p_max_tokens);
	int get_max_tokens() const;

	void set_timeout(double p_timeout);
	double get_timeout() const;

	void set_use_threads(bool p_use_threads);
	bool is_using_threads() const;

	Error send_chat(const String &p_message);
	Error send_messages(const Array &p_messages);
	void cancel_request();
	bool is_requesting() const;
};
