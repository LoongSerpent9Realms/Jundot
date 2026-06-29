#include "ai_oauth_service.h"

#include "core/crypto/crypto.h"
#include "core/io/http_client.h"
#include "core/io/json.h"
#include "core/io/stream_peer_tcp.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/string/print_string.h"
#include "core/templates/local_vector.h"

void AIOAuthService::_bind_methods() {
	BIND_ENUM_CONSTANT(AUTH_STATE_IDLE);
	BIND_ENUM_CONSTANT(AUTH_STATE_AUTHORIZING);
	BIND_ENUM_CONSTANT(AUTH_STATE_EXCHANGING);
	BIND_ENUM_CONSTANT(AUTH_STATE_SUCCESS);
	BIND_ENUM_CONSTANT(AUTH_STATE_FAILED);

	ClassDB::bind_method(D_METHOD("start_login"), &AIOAuthService::start_login);
	ClassDB::bind_method(D_METHOD("cancel_login"), &AIOAuthService::cancel_login);
	ClassDB::bind_method(D_METHOD("get_auth_state"), &AIOAuthService::get_auth_state);
	ClassDB::bind_method(D_METHOD("get_error_message"), &AIOAuthService::get_error_message);
	ClassDB::bind_method(D_METHOD("is_logged_in"), &AIOAuthService::is_logged_in);
	ClassDB::bind_method(D_METHOD("logout"), &AIOAuthService::logout);
}

String AIOAuthService::_generate_state() const {
	Ref<Crypto> crypto = Crypto::create();
	ERR_FAIL_COND_V(crypto.is_null(), String());

	PackedByteArray bytes = crypto->generate_random_bytes(32);
	String state;
	for (int i = 0; i < bytes.size(); i++) {
		state += String::hex_encode_buffer(&bytes[i], 1).to_lower();
	}
	return state;
}

Dictionary AIOAuthService::_parse_query_string(const String &p_query) const {
	Dictionary result;
	Vector<String> pairs = p_query.split("&");
	for (int i = 0; i < pairs.size(); i++) {
		const String &pair = pairs[i];
		int eq_pos = pair.find("=");
		if (eq_pos < 0) {
			continue;
		}
		String key = pair.substr(0, eq_pos);
		String value = pair.substr(eq_pos + 1);
		value = value.replace("+", " ");
		result[key] = value;
	}
	return result;
}

void AIOAuthService::_send_http_response(Ref<StreamPeerTCP> p_client, int p_status_code, const String &p_content_type, const String &p_body) {
	ERR_FAIL_COND(p_client.is_null());

	String status_text = "OK";
	if (p_status_code >= 400 && p_status_code < 500) {
		status_text = "Bad Request";
	} else if (p_status_code >= 500) {
		status_text = "Internal Server Error";
	}

	String response = vformat("HTTP/1.1 %d %s\r\n", p_status_code, status_text);
	response += vformat("Content-Type: %s\r\n", p_content_type);
	response += vformat("Content-Length: %d\r\n", p_body.utf8().size());
	response += "Connection: close\r\n";
	response += "\r\n";
	response += p_body;

	PackedByteArray data = response.to_utf8_buffer();
	p_client->put_data(data.ptr(), data.size());
}

bool AIOAuthService::_start_callback_server(int p_preferred_port) {
	callback_server = Ref<TCPServer>(memnew(TCPServer));
	ERR_FAIL_COND_V(callback_server.is_null(), false);

	IPAddress bind_addr("127.0.0.1");
	int port = p_preferred_port > 0 ? p_preferred_port : 18765;
	int max_attempts = 50;
	Error err = FAILED;

	for (int i = 0; i < max_attempts; i++) {
		err = callback_server->listen(port + i, bind_addr);
		if (err == OK) {
			callback_port = port + i;
			break;
		}
	}

	if (err != OK) {
		print_error("AIOAuthService: Could not start callback server on any port.");
		callback_server.unref();
		return false;
	}

	thread_quit.clear();
	auth_completed.clear();
	callback_thread.start(_callback_thread_poll, this);

	return true;
}

void AIOAuthService::_stop_callback_server() {
	thread_quit.set();
	if (callback_thread.is_started()) {
		callback_thread.wait_to_finish();
	}
	if (callback_server.is_valid()) {
		callback_server->stop();
		callback_server.unref();
	}
	callback_port = 0;
}

void AIOAuthService::_callback_thread_poll(void *p_data) {
	AIOAuthService *service = static_cast<AIOAuthService *>(p_data);
	ERR_FAIL_NULL(service);

	print_line("AIOAuthService: Callback thread started on port " + itos(service->callback_port));
	LocalVector<Ref<StreamPeerTCP>> clients;

	while (!service->thread_quit.is_set()) {
		if (!service->callback_server.is_valid()) {
			break;
		}

		if (service->callback_server->is_connection_available()) {
			Ref<StreamPeerTCP> client = service->callback_server->take_connection();
			if (client.is_valid()) {
				client->set_no_delay(true);
				clients.push_back(client);
				print_line("AIOAuthService: Callback connection received.");
			}
		}

		for (uint32_t i = clients.size(); i > 0; i--) {
			Ref<StreamPeerTCP> client = clients[i - 1];
			if (client->get_status() != StreamPeerTCP::STATUS_CONNECTED) {
				clients.remove_at(i - 1);
				continue;
			}

			if (client->get_available_bytes() > 0) {
				service->_handle_callback_client(client);
				clients.remove_at(i - 1);
			}
		}

		if (service->auth_completed.is_set()) {
			print_line("AIOAuthService: Auth completed, exiting callback thread.");
			break;
		}

		OS::get_singleton()->delay_usec(10000);
	}
}

void AIOAuthService::_handle_callback_client(Ref<StreamPeerTCP> p_client) {
	ERR_FAIL_COND(p_client.is_null());

	const int BUFFER_SIZE = 8192;
	uint8_t buffer[BUFFER_SIZE];
	int bytes_read = 0;

	while (p_client->get_available_bytes() > 0 && bytes_read < BUFFER_SIZE - 1) {
		int read = 0;
		Error err = p_client->get_partial_data(&buffer[bytes_read], BUFFER_SIZE - bytes_read - 1, read);
		if (err != OK || read == 0) {
			break;
		}
		bytes_read += read;
	}
	buffer[bytes_read] = 0;

	String request = String::utf8((const char *)buffer, bytes_read);
	Vector<String> lines = request.split("\r\n");
	if (lines.is_empty() || lines[0].strip_edges().is_empty()) {
		print_line("AIOAuthService: Empty request received.");
		String error_html = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Error</title></head><body><h1>Bad Request</h1></body></html>";
		_send_http_response(p_client, 400, "text/html; charset=utf-8", error_html);
		p_client->disconnect_from_host();
		return;
	}

	String request_line = lines[0].strip_edges();
	print_line("AIOAuthService: Request line: " + request_line);

	Vector<String> parts = request_line.split(" ", false);
	if (parts.size() < 2) {
		String error_html = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Error</title></head><body><h1>Bad Request</h1></body></html>";
		_send_http_response(p_client, 400, "text/html; charset=utf-8", error_html);
		p_client->disconnect_from_host();
		return;
	}

	String method = parts[0];
	String full_path = parts[1];
	if (method != "GET") {
		String error_html = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Error</title></head><body><h1>Method Not Allowed</h1></body></html>";
		_send_http_response(p_client, 405, "text/html; charset=utf-8", error_html);
		p_client->disconnect_from_host();
		return;
	}

	String path = full_path;
	Dictionary query_params;
	int q_pos = full_path.find("?");
	if (q_pos >= 0) {
		path = full_path.substr(0, q_pos);
		String query = full_path.substr(q_pos + 1);
		query_params = _parse_query_string(query);
	}

	print_line("AIOAuthService: Path: " + path + ", expected: " + _get_callback_path());

	if (path == _get_callback_path()) {
		String pending_html = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Logging in...</title><style>body{font-family:Segoe UI,Microsoft YaHei,sans-serif;display:flex;justify-content:center;align-items:center;height:100vh;margin:0;background:#202124;color:#f1f3f4;text-align:center}.spinner{width:44px;height:44px;border:4px solid rgba(255,255,255,0.25);border-top-color:#8ab4f8;border-radius:50%;animation:spin 1s linear infinite;margin:0 auto 20px}@keyframes spin{to{transform:rotate(360deg)}}h1{font-size:1.4em}</style></head><body><div><div class=\"spinner\"></div><h1>OAuth login received</h1><p>You can return to Jundot. The editor is exchanging the authorization code now.</p><p style=\"opacity:0.8;font-size:0.9em\">This window will close automatically.</p><script>setTimeout(function(){window.close()},5000)</script></div></body></html>";
		_send_http_response(p_client, 200, "text/html; charset=utf-8", pending_html);
		print_line("AIOAuthService: Callback response sent, now processing token exchange...");
		p_client->disconnect_from_host();
		_handle_callback_request(path, query_params);
		print_line("AIOAuthService: Callback processing complete.");
		return;
	}

	String not_found_html = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Not Found</title></head><body><h1>404 Not Found</h1><p>Path: " + path + "</p></body></html>";
	_send_http_response(p_client, 404, "text/html; charset=utf-8", not_found_html);
	p_client->disconnect_from_host();
}

void AIOAuthService::_handle_callback_request(const String &p_path, const Dictionary &p_query_params) {
	(void)p_path;

	String state = p_query_params.get("state", String());
	String code = p_query_params.get("code", String());
	String error = p_query_params.get("error", String());

	print_line(vformat("AIOAuthService: Callback received - state=%s, code=%s, error=%s",
			state.is_empty() ? "(empty)" : state.substr(0, 8) + "...",
			code.is_empty() ? "(empty)" : code.substr(0, 8) + "...",
			error));

	{
		MutexLock lock(state_mutex);

		if (!error.is_empty()) {
			auth_state = AUTH_STATE_FAILED;
			error_message = vformat("Authorization error: %s", error);
			print_error("AIOAuthService: " + error_message);
			auth_completed.set();
			return;
		}

		const bool allow_missing_state = state.is_empty() && _allow_missing_callback_state() && auth_state == AUTH_STATE_AUTHORIZING && !code.is_empty();
		if (state != auth_state_value && !allow_missing_state) {
			auth_state = AUTH_STATE_FAILED;
			error_message = "State mismatch - possible CSRF attack.";
			print_error("AIOAuthService: " + error_message);
			auth_completed.set();
			return;
		}
		if (allow_missing_state) {
			WARN_PRINT("AIOAuthService: OAuth callback did not include state; accepting because this provider does not reliably echo state.");
		}

		if (code.is_empty()) {
			auth_state = AUTH_STATE_FAILED;
			error_message = "No authorization code received.";
			print_error("AIOAuthService: " + error_message);
			auth_completed.set();
			return;
		}

		received_code = code;
		auth_state = AUTH_STATE_EXCHANGING;
		print_line("AIOAuthService: Authorization code received, exchanging for token...");
	}

	String redirect_uri = vformat("http://127.0.0.1:%d%s", callback_port, _get_callback_path());

	AIOAuthToken token;
	String token_error_message;
	Error token_err = _exchange_code_for_token(code, redirect_uri, token, &token_error_message);

	{
		MutexLock lock(state_mutex);
		if (token_err != OK) {
			auth_state = AUTH_STATE_FAILED;
			if (!token_error_message.is_empty()) {
				error_message = token_error_message;
			} else if (error_message.is_empty()) {
				error_message = "Failed to exchange authorization code for token.";
			}
			print_error("AIOAuthService: " + error_message);
			auth_completed.set();
			return;
		}
		print_line("AIOAuthService: Token exchange successful.");
	}

	AIOAuthUserInfo user_info;
	String user_error_message;
	Error user_err = _fetch_user_info(token, user_info, &user_error_message);

	{
		MutexLock lock(state_mutex);
		if (user_err != OK) {
			auth_state = AUTH_STATE_FAILED;
			error_message = user_error_message.is_empty() ? String("Failed to fetch user information.") : user_error_message;
			auth_completed.set();
			return;
		}

		_save_token_and_user(token, user_info);
		auth_state = AUTH_STATE_SUCCESS;
		auth_completed.set();
	}
}

Error AIOAuthService::_connect_http_client(Ref<HTTPClient> p_http, const String &p_host, int p_port, Ref<TLSOptions> p_tls_options, String *r_error_message) {
	ERR_FAIL_COND_V(p_http.is_null(), ERR_INVALID_PARAMETER);

	Error err = p_http->connect_to_host(p_host, p_port, p_tls_options);
	if (err != OK) {
		if (r_error_message) {
			*r_error_message = vformat("Could not start connection to %s:%d (error %d).", p_host, p_port, err);
		}
		return err;
	}

	uint64_t start_msec = OS::get_singleton()->get_ticks_msec();
	const uint64_t timeout_msec = 30000;
	while (p_http->get_status() == HTTPClient::STATUS_RESOLVING || p_http->get_status() == HTTPClient::STATUS_CONNECTING) {
		p_http->poll();
		if (OS::get_singleton()->get_ticks_msec() - start_msec > timeout_msec) {
			if (r_error_message) {
				*r_error_message = vformat("Timed out connecting to %s:%d.", p_host, p_port);
			}
			p_http->close();
			return ERR_TIMEOUT;
		}
		OS::get_singleton()->delay_usec(100000);
	}

	if (p_http->get_status() != HTTPClient::STATUS_CONNECTED) {
		HTTPClient::Status status = p_http->get_status();
		if (r_error_message) {
			*r_error_message = vformat("Could not connect to %s:%d (HTTPClient status %d).", p_host, p_port, status);
		}
		p_http->close();
		return ERR_CANT_CONNECT;
	}

	return OK;
}

Error AIOAuthService::_exchange_code_for_token(const String &p_code, const String &p_redirect_uri, AIOAuthToken &r_token, String *r_error_message) {
	String token_url = _get_token_url();
	String client_id = _get_client_id();
	String client_secret = _get_client_secret();

	String post_body = vformat(
			"client_id=%s&client_secret=%s&code=%s&redirect_uri=%s&grant_type=authorization_code",
			client_id.uri_encode(),
			client_secret.uri_encode(),
			p_code.uri_encode(),
			p_redirect_uri.uri_encode());

	Ref<HTTPClient> http = HTTPClient::create();
	ERR_FAIL_COND_V(http.is_null(), ERR_CANT_CREATE);

	// Parse URL to get host and path
	String host = token_url;
	host = host.replace("https://", "").replace("http://", "");
	int path_pos = host.find("/");
	String path = "/login/oauth/access_token";
	if (path_pos >= 0) {
		path = host.substr(path_pos);
		host = host.substr(0, path_pos);
	}

	Error err = _connect_http_client(http, host, 443, TLSOptions::client(), r_error_message);
	if (err != OK) {
		return err;
	}

	Vector<String> headers;
	headers.push_back("Content-Type: application/x-www-form-urlencoded");
	headers.push_back("Accept: application/json");
	headers.push_back("User-Agent: Jundot-Editor-AI");

	CharString body_utf8 = post_body.utf8();
	err = http->request(HTTPClient::METHOD_POST, path, headers,
			body_utf8.length() > 0 ? (const uint8_t *)body_utf8.get_data() : nullptr,
			body_utf8.length());
	if (err != OK) {
		if (r_error_message) {
			*r_error_message = vformat("Could not send token request to %s (error %d).", host, err);
		}
		http->close();
		return err;
	}

	PackedByteArray response_body;
	while (http->get_status() == HTTPClient::STATUS_REQUESTING) {
		http->poll();
		OS::get_singleton()->delay_usec(100000);
	}

	if (http->get_status() == HTTPClient::STATUS_BODY || http->get_status() == HTTPClient::STATUS_CONNECTED) {
		while (http->get_status() == HTTPClient::STATUS_BODY) {
			http->poll();
			PackedByteArray body_chunk = http->read_response_body_chunk();
			if (body_chunk.size() > 0) {
				response_body.append_array(body_chunk);
			}
			if (body_chunk.size() < http->get_read_chunk_size()) {
				break;
			}
			OS::get_singleton()->delay_usec(10000);
		}
	}

	int response_code = http->get_response_code();
	http->close();

	if (response_code < 200 || response_code >= 300) {
		String err_str = String::utf8((const char *)response_body.ptr(), response_body.size());
		ERR_PRINT(vformat("Token exchange failed (HTTP %d): %s", response_code, err_str));
		if (r_error_message) {
			*r_error_message = vformat("Token exchange failed (HTTP %d): %s", response_code, err_str);
		}
		return FAILED;
	}

	JSON json;
	String response_text = String::utf8((const char *)response_body.ptr(), response_body.size());
	err = json.parse(response_text);
	if (err != OK) {
		if (r_error_message) {
			*r_error_message = vformat("Could not parse token response: %s", response_text);
		}
		return err;
	}

	Variant data = json.get_data();
	if (data.get_type() != Variant::DICTIONARY) {
		if (r_error_message) {
			*r_error_message = "Token response was not a JSON object.";
		}
		return ERR_INVALID_DATA;
	}

	Dictionary response = Dictionary(data);
	String oauth_error = response.get("error", String());
	if (!oauth_error.is_empty()) {
		String oauth_description = response.get("error_description", String());
		if (r_error_message) {
			*r_error_message = oauth_description.is_empty() ? vformat("OAuth token error: %s", oauth_error) : vformat("OAuth token error: %s (%s)", oauth_error, oauth_description);
		}
		return FAILED;
	}

	r_token = _parse_token_response(response);
	if (r_token.access_token.is_empty()) {
		if (r_error_message) {
			*r_error_message = vformat("OAuth token response did not include an access token: %s", response_text);
		}
		return ERR_INVALID_DATA;
	}
	return OK;
}

Error AIOAuthService::_fetch_user_info(const AIOAuthToken &p_token, AIOAuthUserInfo &r_user, String *r_error_message) {
	String user_url = _get_user_url();

	Ref<HTTPClient> http = HTTPClient::create();
	ERR_FAIL_COND_V(http.is_null(), ERR_CANT_CREATE);

	// Parse URL to get host and path
	String host = user_url;
	host = host.replace("https://", "").replace("http://", "");
	int path_pos = host.find("/");
	String path = "/api/v5/user";
	if (path_pos >= 0) {
		path = host.substr(path_pos);
		host = host.substr(0, path_pos);
	}

	Error err = _connect_http_client(http, host, 443, TLSOptions::client(), r_error_message);
	if (err != OK) {
		return err;
	}

	Vector<String> headers;
	headers.push_back("Accept: application/json");
	headers.push_back(vformat("Authorization: Bearer %s", p_token.access_token));
	headers.push_back("User-Agent: Jundot-Editor-AI");

	err = http->request(HTTPClient::METHOD_GET, path, headers, nullptr, 0);
	if (err != OK) {
		if (r_error_message) {
			*r_error_message = vformat("Could not send user info request to %s (error %d).", host, err);
		}
		http->close();
		return err;
	}

	PackedByteArray response_body;
	while (http->get_status() == HTTPClient::STATUS_REQUESTING) {
		http->poll();
		OS::get_singleton()->delay_usec(100000);
	}

	if (http->get_status() == HTTPClient::STATUS_BODY || http->get_status() == HTTPClient::STATUS_CONNECTED) {
		while (http->get_status() == HTTPClient::STATUS_BODY) {
			http->poll();
			PackedByteArray body_chunk = http->read_response_body_chunk();
			if (body_chunk.size() > 0) {
				response_body.append_array(body_chunk);
			}
			if (body_chunk.size() < http->get_read_chunk_size()) {
				break;
			}
			OS::get_singleton()->delay_usec(10000);
		}
	}

	int response_code = http->get_response_code();
	http->close();
	String response_text = String::utf8((const char *)response_body.ptr(), response_body.size());

	if (response_code < 200 || response_code >= 300) {
		if (r_error_message) {
			*r_error_message = vformat("User info request failed (HTTP %d): %s", response_code, response_text);
		}
		return FAILED;
	}

	JSON json;
	err = json.parse(response_text);
	if (err != OK) {
		if (r_error_message) {
			*r_error_message = vformat("Could not parse user info response: %s", response_text);
		}
		return err;
	}

	Variant data = json.get_data();
	if (data.get_type() != Variant::DICTIONARY) {
		if (r_error_message) {
			*r_error_message = "User info response was not a JSON object.";
		}
		return ERR_INVALID_DATA;
	}

	r_user = _parse_user_response(Dictionary(data));
	return OK;
}

String AIOAuthService::_build_authorization_url(const String &p_client_id, const String &p_redirect_uri) const {
	String scope = _get_scope();

	String auth_url = _get_authorize_url();
	auth_url += "?";
	auth_url += "client_id=" + p_client_id.uri_encode();
	auth_url += "&redirect_uri=" + p_redirect_uri.uri_encode();
	auth_url += "&response_type=code";
	auth_url += "&scope=" + scope.uri_encode();
	auth_url += "&state=" + auth_state_value.uri_encode();
	return auth_url;
}

Error AIOAuthService::start_login() {
	MutexLock lock(state_mutex);

	if (auth_state == AUTH_STATE_AUTHORIZING || auth_state == AUTH_STATE_EXCHANGING) {
		return ERR_ALREADY_IN_USE;
	}

	String client_id = _get_client_id();
	if (client_id.is_empty()) {
		auth_state = AUTH_STATE_FAILED;
		error_message = "OAuth Client ID is not configured.";
		return ERR_UNCONFIGURED;
	}

	auth_state_value = _generate_state();
	auth_state = AUTH_STATE_AUTHORIZING;
	error_message.clear();
	received_code.clear();

	if (!_start_callback_server()) {
		auth_state = AUTH_STATE_FAILED;
		error_message = "Could not start local callback server.";
		return FAILED;
	}

	String redirect_uri = vformat("http://127.0.0.1:%d%s", callback_port, _get_callback_path());
	String auth_url = _build_authorization_url(client_id, redirect_uri);

	print_line("AIOAuthService: Opening auth URL: " + auth_url);

	Error shell_err = OS::get_singleton()->shell_open(auth_url);
	if (shell_err != OK) {
		_stop_callback_server();
		auth_state = AUTH_STATE_FAILED;
		error_message = "Could not open browser for authorization.";
		return shell_err;
	}

	return OK;
}

void AIOAuthService::cancel_login() {
	MutexLock lock(state_mutex);
	_stop_callback_server();
	auth_state = AUTH_STATE_IDLE;
	error_message.clear();
}

AIOAuthService::AuthState AIOAuthService::get_auth_state() const {
	MutexLock lock(state_mutex);
	return auth_state;
}

String AIOAuthService::get_error_message() const {
	MutexLock lock(state_mutex);
	return error_message;
}

bool AIOAuthService::is_logged_in() const {
	return false;
}

void AIOAuthService::logout() {
	MutexLock lock(state_mutex);
	auth_state = AUTH_STATE_IDLE;
	error_message.clear();
}

AIOAuthService::AIOAuthService() {
}

AIOAuthService::~AIOAuthService() {
	_stop_callback_server();
}
