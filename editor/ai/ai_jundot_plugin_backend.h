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

class AIJundotPluginBackend : public Node {
	GDCLASS(AIJundotPluginBackend, Node)

	HTTPRequest *http_request = nullptr;
	AISettingsData settings;
	uint64_t request_start_usec = 0;
	bool requesting = false;

	void _ensure_http_request();
	String _build_plugin_url() const;
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
