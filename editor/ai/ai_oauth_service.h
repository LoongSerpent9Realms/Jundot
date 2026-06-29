#pragma once

#include "core/crypto/crypto.h"
#include "core/io/http_client.h"
#include "core/io/tcp_server.h"
#include "core/object/class_db.h"
#include "core/object/object.h"
#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/os/thread_safe.h"
#include "core/variant/dictionary.h"
#include "editor/ai/ai_settings.h"

class AIOAuthService : public Object {
	GDCLASS(AIOAuthService, Object);

public:
	enum AuthState {
		AUTH_STATE_IDLE = 0,
		AUTH_STATE_AUTHORIZING = 1,
		AUTH_STATE_EXCHANGING = 2,
		AUTH_STATE_SUCCESS = 3,
		AUTH_STATE_FAILED = 4,
	};

protected:
	static void _bind_methods();

private:
	Ref<TCPServer> callback_server;
	Thread callback_thread;
	Mutex state_mutex;
	SafeFlag thread_quit;
	SafeFlag auth_completed;

	AuthState auth_state = AUTH_STATE_IDLE;
	String error_message;
	String auth_state_value;
	String received_code;
	int callback_port = 0;

	bool _start_callback_server(int p_preferred_port = 0);
	void _stop_callback_server();
	static void _callback_thread_poll(void *p_data);
	void _handle_callback_client(Ref<StreamPeerTCP> p_client);
	void _handle_callback_request(const String &p_path, const Dictionary &p_query_params);
	String _generate_state() const;
	Dictionary _parse_query_string(const String &p_query) const;
	void _send_http_response(Ref<StreamPeerTCP> p_client, int p_status_code, const String &p_content_type, const String &p_body);
	Error _connect_http_client(Ref<HTTPClient> p_http, const String &p_host, int p_port, Ref<TLSOptions> p_tls_options, String *r_error_message);

	virtual String _get_authorize_url() const = 0;
	virtual String _get_token_url() const = 0;
	virtual String _get_user_url() const = 0;
	virtual String _get_client_id() const = 0;
	virtual String _get_client_secret() const = 0;
	virtual String _get_callback_path() const = 0;
	virtual String _get_scope() const = 0;
	virtual bool _allow_missing_callback_state() const { return false; }
	virtual String _build_authorization_url(const String &p_client_id, const String &p_redirect_uri) const;
	virtual AIOAuthToken _parse_token_response(const Dictionary &p_response) const = 0;
	virtual AIOAuthUserInfo _parse_user_response(const Dictionary &p_response) const = 0;
	virtual void _save_token_and_user(const AIOAuthToken &p_token, const AIOAuthUserInfo &p_user) = 0;

	Error _exchange_code_for_token(const String &p_code, const String &p_redirect_uri, AIOAuthToken &r_token, String *r_error_message = nullptr);
	Error _fetch_user_info(const AIOAuthToken &p_token, AIOAuthUserInfo &r_user, String *r_error_message = nullptr);

public:
	Error start_login();
	void cancel_login();
	AuthState get_auth_state() const;
	String get_error_message() const;
	bool is_logged_in() const;
	void logout();

	AIOAuthService();
	~AIOAuthService();
};

VARIANT_ENUM_CAST(AIOAuthService::AuthState);
