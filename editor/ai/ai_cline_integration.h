/**************************************************************************/
/*  ai_cline_integration.h                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#pragma once

#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"

class HTTPRequest;
class Timer;

// Cline integration states
enum class AIClineState {
	DISCONNECTED,     // Not connected to Cline
	CONNECTING,       // Attempting to connect
	CONNECTED,        // Connected and ready
	AUTHENTICATING,   // Waiting for user authentication
	AUTHENTICATED,    // Authenticated and ready
	ERROR             // Error state
};

// Cline callback data is passed as Dictionary with "type" key
// containing one of: "login_success", "login_failed",
// "message_received", "tool_call_requested", "session_ended"

class AIClineIntegration : public RefCounted {
	GDCLASS(AIClineIntegration, RefCounted);

private:
	static AIClineIntegration *singleton;
	
	AIClineState state = AIClineState::DISCONNECTED;
	String cline_server_url = "http://127.0.0.1:8080";
	String session_id;
	String auth_token;
	
	HTTPRequest *http_request = nullptr;
	Timer *poll_timer = nullptr;
	
	bool auto_connect = true;
	int connection_timeout = 30;
	int poll_interval = 1000; // ms
	
	Callable callback;
	
	void _on_http_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	void _on_poll_timeout();
	void _update_state(AIClineState p_new_state);
	
public:
	static AIClineIntegration *get_singleton();
	static void cleanup();
	
	AIClineIntegration();
	~AIClineIntegration();
	
	// Connection management
	Error connect_to_cline(const String &p_server_url = String());
	void disconnect_from_cline();
	bool is_cline_connected() const;
	int get_state() const;
	
	// Authentication
	Error login(const String &p_token = String());
	void logout();
	bool is_authenticated() const;
	
	// Message handling
	Error send_message(const String &p_content, const Dictionary &p_context = Dictionary());
	Error send_tool_result(const String &p_tool_call_id, const String &p_result, bool p_is_error = false);
	
	// Tool registration
	Error register_tool(const String &p_name, const String &p_description, const Dictionary &p_parameters);
	Error unregister_tool(const String &p_name);
	
	// Callbacks
	void set_callback(const Callable &p_callback);
	
	// Configuration
	void set_auto_connect(bool p_enabled);
	void set_server_url(const String &p_url);
	String get_server_url() const;
	
	// Session info
	Dictionary get_session_info() const;
	String get_session_id() const;
	
protected:
	static void _bind_methods();
};