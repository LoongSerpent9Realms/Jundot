/*  ai_jundot_plugin_backend.h                                            */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#pragma once

#include "core/typedefs.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "scene/main/node.h"

#include "ai_settings.h"

class HTTPRequest;
class Timer;

class AIJundotPluginBackend : public Node {
	GDCLASS(AIJundotPluginBackend, Node)

	HTTPRequest *http_request = nullptr;
	Timer *cooldown_timer = nullptr;
	AISettingsData settings;
	uint64_t request_start_usec = 0;
	uint64_t last_request_end_usec = 0;
	uint64_t rate_limit_backoff_until_usec = 0;
	int rate_limit_retry_count = 0;
	bool requesting = false;
	bool pending_request = false;
	Array pending_messages;
	Array pending_tools;
	Array active_messages;
	Array active_tools;

	void _ensure_http_request();
	void _ensure_cooldown_timer();
	String _build_plugin_url() const;
	double _get_rate_limit_wait_seconds(const PackedStringArray &p_headers) const;
	bool _schedule_rate_limit_retry(const PackedStringArray &p_headers);
	Error _send_messages_now(const Array &p_messages, const Array &p_tools);
	void _send_pending_request();
	void _request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	void _emit_completed(int p_result, int p_response_code, const String &p_content, const Dictionary &p_json, const String &p_raw_body, double p_elapsed_seconds);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void configure(const AISettingsData &p_settings);
	Error send_messages(const Array &p_messages, const Array &p_tools);
	void cancel_request();
	bool is_requesting() const;
};
