/**************************************************************************/
/*  ai_mcp_http_server.cpp                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#include "ai_mcp_http_server.h"

#include "ai_tool_executor.h"
#include "ai_tool_defs.h"
#include "ai_tool_registry.h"
#include "core/io/json.h"
#include "core/os/os.h"

void AIMCPHTTPServer::_server_thread_poll(void *data) {
	AIMCPHTTPServer *mcp_server = static_cast<AIMCPHTTPServer *>(data);
	while (!mcp_server->server_quit.is_set()) {
		OS::get_singleton()->delay_usec(10000);
		{
			MutexLock lock(mcp_server->server_lock);
			if (!mcp_server->server->is_listening()) {
				continue;
			}
			if (mcp_server->server->is_connection_available()) {
				Ref<StreamPeerTCP> client = mcp_server->server->take_connection();
				if (client.is_valid()) {
					mcp_server->clients.push_back(client);
				}
			}
			for (int i = mcp_server->clients.size() - 1; i >= 0; i--) {
				Ref<StreamPeerTCP> client = mcp_server->clients[i];
				if (client->get_status() != StreamPeerTCP::STATUS_CONNECTED) {
					mcp_server->clients.remove_at(i);
					continue;
				}
				if (client->get_available_bytes() > 0) {
					mcp_server->_handle_client(client);
					mcp_server->clients.remove_at(i);
				}
			}
		}
	}
}

void AIMCPHTTPServer::_handle_client(Ref<StreamPeerTCP> p_client) {
	const int BUFFER_SIZE = 8192;
	uint8_t buffer[BUFFER_SIZE];
	int bytes_read = 0;
	String request;

	while (p_client->get_available_bytes() > 0 && bytes_read < BUFFER_SIZE - 1) {
		int read = 0;
		Error err = p_client->get_partial_data(&buffer[bytes_read], BUFFER_SIZE - bytes_read - 1, read);
		if (err != OK || read == 0) {
			break;
		}
		bytes_read += read;
	}
	buffer[bytes_read] = 0;
	request = String::utf8((const char *)buffer, bytes_read);

	Vector<String> lines = request.split("\r\n");
	if (lines.is_empty()) {
		_send_response(p_client, 400, "text/plain", "Bad Request");
		return;
	}

	Vector<String> first_line = lines[0].split(" ", false);
	if (first_line.size() < 2) {
		_send_response(p_client, 400, "text/plain", "Bad Request");
		return;
	}

	String method = first_line[0].strip_edges();
	String path = first_line[1].strip_edges();

	String body;
	int content_length = 0;
	for (int i = 1; i < lines.size(); i++) {
		String line = lines[i];
		if (line.begins_with("Content-Length:")) {
			content_length = line.get_slice(":", 1).strip_edges().to_int();
		}
		if (line.is_empty()) {
			if (i + 1 < lines.size()) {
				body = lines[i + 1];
				if (content_length > 0 && body.length() < content_length) {
					for (int j = i + 2; j < lines.size(); j++) {
						body += "\r\n" + lines[j];
					}
				}
			}
			break;
		}
	}

	_handle_request(p_client, method, path, body);
}

void AIMCPHTTPServer::_send_response(Ref<StreamPeerTCP> p_client, int p_status_code, const String &p_content_type, const String &p_content) {
	String status_text;
	switch (p_status_code) {
		case 200: status_text = "OK"; break;
		case 400: status_text = "Bad Request"; break;
		case 404: status_text = "Not Found"; break;
		case 500: status_text = "Internal Server Error"; break;
		default: status_text = "Unknown";
	}

	String response = vformat("HTTP/1.1 %d %s\r\n", p_status_code, status_text);
	response += "Content-Type: " + p_content_type + "\r\n";
	response += "Content-Length: " + itos(p_content.utf8().length()) + "\r\n";
	response += "Access-Control-Allow-Origin: *\r\n";
	response += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
	response += "Access-Control-Allow-Headers: Content-Type\r\n";
	response += "\r\n";
	response += p_content;

	CharString cs = response.utf8();
	p_client->put_data((const uint8_t *)cs.get_data(), cs.size());
}

String AIMCPHTTPServer::_execute_tool(const String &p_server_name, const String &p_tool_name, const String &p_args_json) {
	Dictionary tool_call;
	Dictionary fn;
	fn["name"] = p_server_name + "." + p_tool_name;
	fn["arguments"] = p_args_json;
	tool_call["id"] = "ext-" + String::num_int64(OS::get_singleton()->get_ticks_usec());
	tool_call["type"] = "function";
	tool_call["function"] = fn;

	Dictionary result = AIToolExecutor::execute(tool_call);
	return result.get("content", "Unknown error").operator String();
}

void AIMCPHTTPServer::_handle_request(Ref<StreamPeerTCP> p_client, const String &p_method, const String &p_path, const String &p_body) {
	if (p_method == "OPTIONS") {
		_send_response(p_client, 200, "text/plain", "");
		return;
	}

	if (p_method == "GET" && p_path == "/api/mcp/tools") {
		Array tools = AIToolDefs::get_mcp_tools();
		String json = JSON::stringify(tools);
		_send_response(p_client, 200, "application/json", json);
		return;
	}

	if (p_method == "GET" && p_path == "/api/mcp/servers") {
		Vector<AISkillEntry> skills;
		Vector<AIMCPServerEntry> mcp_servers_list;
		if (AIToolRegistry::load(skills, mcp_servers_list) == OK) {
			Array server_list;
			for (const AIMCPServerEntry &mcp_entry : mcp_servers_list) {
				Dictionary s;
				s["name"] = mcp_entry.name;
				s["enabled"] = mcp_entry.enabled;
				s["command"] = mcp_entry.command;
				s["url"] = mcp_entry.url;
				server_list.push_back(s);
			}
			String json = JSON::stringify(server_list);
			_send_response(p_client, 200, "application/json", json);
		} else {
			_send_response(p_client, 500, "application/json", "{\"error\": \"Failed to load server registry\"}");
		}
		return;
	}

	if (p_method == "POST" && p_path == "/api/mcp/call") {
		Variant parsed = JSON::parse_string(p_body);
		if (parsed.get_type() != Variant::DICTIONARY) {
			_send_response(p_client, 400, "application/json", "{\"error\": \"Invalid JSON\"}");
			return;
		}

		Dictionary req = parsed;
		String server_name = req.get("server", String());
		String tool_name = req.get("tool", String());
		String args_json = req.get("arguments", "{}");

		if (server_name.is_empty() || tool_name.is_empty()) {
			_send_response(p_client, 400, "application/json", "{\"error\": \"Missing server or tool name\"}");
			return;
		}

		String result = _execute_tool(server_name, tool_name, args_json);
		Dictionary response;
		response["result"] = result;
		_send_response(p_client, 200, "application/json", JSON::stringify(response));
		return;
	}

	if (p_method == "GET" && p_path == "/api/mcp/health") {
		_send_response(p_client, 200, "application/json", "{\"status\": \"ok\", \"service\": \"mcp\"}");
		return;
	}

	_send_response(p_client, 404, "text/plain", "Not Found");
}

Error AIMCPHTTPServer::start(int p_port, const IPAddress &p_address) {
	MutexLock lock(server_lock);
	if (server->is_listening()) {
		return ERR_ALREADY_IN_USE;
	}

	port = p_port;
	bind_address = p_address;

	Error err = server->listen(port, bind_address);
	if (err != OK) {
		return err;
	}

	server_quit.clear();
	server_thread.start(_server_thread_poll, this);
	return OK;
}

void AIMCPHTTPServer::stop() {
	server_quit.set();
	if (server_thread.is_started()) {
		server_thread.wait_to_finish();
	}
	if (server.is_valid()) {
		server->stop();
	}
	clients.clear();
}

bool AIMCPHTTPServer::is_running() const {
	MutexLock lock(server_lock);
	return server->is_listening();
}

int AIMCPHTTPServer::get_port() const {
	return port;
}

AIMCPHTTPServer::AIMCPHTTPServer() {
	server.instantiate();
}

AIMCPHTTPServer::~AIMCPHTTPServer() {
	stop();
}
