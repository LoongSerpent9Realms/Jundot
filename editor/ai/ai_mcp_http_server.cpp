/**************************************************************************/
/*  ai_mcp_http_server.cpp                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             JUNDOT ENGINE                               */
/*                        https://jundotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "ai_mcp_http_server.h"

#include "core/io/json.h"
#include "core/os/os.h"
#include "editor/ai/ai_settings.h"
#include "editor/ai/ai_tool_defs.h"
#include "editor/ai/ai_tool_executor.h"
#include "editor/ai/ai_tool_registry.h"

static Dictionary _external_mcp_str_property(const String &p_description) {
	Dictionary prop;
	prop["type"] = "string";
	prop["description"] = p_description;
	return prop;
}

static Dictionary _external_mcp_bool_property(const String &p_description) {
	Dictionary prop;
	prop["type"] = "boolean";
	prop["description"] = p_description;
	return prop;
}

static Dictionary _external_mcp_int_property(const String &p_description) {
	Dictionary prop;
	prop["type"] = "integer";
	prop["description"] = p_description;
	return prop;
}

static Dictionary _external_mcp_number_property(const String &p_description) {
	Dictionary prop;
	prop["type"] = "number";
	prop["description"] = p_description;
	return prop;
}

static Dictionary _external_mcp_tool(const String &p_name, const String &p_description, const Dictionary &p_properties) {
	Dictionary params;
	params["type"] = "object";
	params["properties"] = p_properties;

	Dictionary fn;
	fn["name"] = p_name;
	fn["description"] = p_description;
	fn["parameters"] = params;

	Dictionary tool;
	tool["type"] = "function";
	tool["function"] = fn;
	tool["x_mcp_server_name"] = "ai_settings";
	tool["x_builtin"] = true;
	return tool;
}

static Array _external_mcp_protected_fields() {
	Array fields;
	fields.push_back("system_prompt");
	fields.push_back("engine_source_repository_url");
	fields.push_back("encrypt_engine_source_cache");
	return fields;
}

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
		case 200:
			status_text = "OK";
			break;
		case 400:
			status_text = "Bad Request";
			break;
		case 404:
			status_text = "Not Found";
			break;
		case 500:
			status_text = "Internal Server Error";
			break;
		default:
			status_text = "Unknown";
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

Array AIMCPHTTPServer::_get_jundot_plugin_tools() const {
	Array mode_tools = AIToolDefs::get_tools_for_mode(AISettings::load().context_mode);
	Array exposed_tools;
	HashSet<StringName> exposed_names;
	exposed_names.insert(StringName(AIToolNames::BATCH_TOOLS));
	exposed_names.insert(StringName(AIToolNames::LIST_FILES));
	exposed_names.insert(StringName(AIToolNames::READ_FILES));
	exposed_names.insert(StringName(AIToolNames::SEARCH_FILES));
	exposed_names.insert(StringName(AIToolNames::GREP_CODE));
	exposed_names.insert(StringName(AIToolNames::EDIT_FILE));
	exposed_names.insert(StringName(AIToolNames::WRITE_FILE));
	exposed_names.insert(StringName(AIToolNames::SHELL_COMMAND));
	exposed_names.insert(StringName(AIToolNames::RUN_BUILD));
	exposed_names.insert(StringName(AIToolNames::CHECK_BUILD_STATUS));
	exposed_names.insert(StringName(AIToolNames::READ_BUILD_LOG));

	for (int i = 0; i < mode_tools.size(); i++) {
		Dictionary tool = mode_tools[i];
		Dictionary fn_def = tool.get("function", Dictionary());
		const String tool_name = fn_def.get("name", String());
		if (!exposed_names.has(StringName(tool_name))) {
			continue;
		}
		tool["x_mcp_server_name"] = "jundot";
		tool["x_builtin"] = true;
		tool["x_jundot_plugin"] = true;
		exposed_tools.push_back(tool);
	}
	return exposed_tools;
}

Array AIMCPHTTPServer::_get_ai_settings_tools() const {
	Array tools;

	tools.push_back(_external_mcp_tool(
			"ai_settings.get_config",
			"Read the current JunDot AI settings that external AI clients are allowed to inspect. Secret values are redacted; the fixed engine source repository URL and source cache encryption policy are read-only.",
			Dictionary()));

	Dictionary update_props;
	update_props["base_url"] = _external_mcp_str_property("OpenAI-compatible API base URL.");
	update_props["model"] = _external_mcp_str_property("Model name used by the built-in AI assistant.");
	update_props["api_key"] = _external_mcp_str_property("API key to store. Passing an empty string clears it.");
	update_props["temperature"] = _external_mcp_number_property("Sampling temperature.");
	update_props["max_tokens"] = _external_mcp_int_property("Maximum response token budget.");
	update_props["output_language"] = _external_mcp_str_property("Output language, such as auto, English, Simplified Chinese, Traditional Chinese, Japanese, Korean, Spanish, French, or German.");
	update_props["tools_enabled"] = _external_mcp_bool_property("Enable built-in function calling tools.");
	update_props["mcp_tools_enabled"] = _external_mcp_bool_property("Enable configured external MCP server tools.");
	update_props["context_char_budget"] = _external_mcp_int_property("Compressed context character budget.");
	update_props["history_char_budget"] = _external_mcp_int_property("Conversation history character budget.");
	update_props["max_tool_iterations"] = _external_mcp_int_property("Maximum tool-call loop iterations.");
	update_props["user_extra_instructions"] = _external_mcp_str_property("User-editable extra instructions appended to the protected system prompt.");
	update_props["engine_source_root"] = _external_mcp_str_property("Absolute path to a local JunDot engine source checkout.");
	update_props["engine_source_cache_root"] = _external_mcp_str_property("Absolute path to the local encrypted engine source cache.");
	update_props["external_api_enabled"] = _external_mcp_bool_property("Enable the external MCP HTTP API server.");
	update_props["external_api_port"] = _external_mcp_int_property("External MCP HTTP API port.");
	update_props["external_api_bind_address"] = _external_mcp_str_property("External MCP HTTP API bind address.");
	tools.push_back(_external_mcp_tool(
			"ai_settings.update_config",
			"Update allowed JunDot AI settings. The system prompt, engine source repository URL, and source cache encryption policy cannot be changed through this tool.",
			update_props));

	tools.push_back(_external_mcp_tool(
			"ai_settings.reset_config",
			"Reset JunDot AI settings to product defaults. The protected system prompt, fixed repository URL, and source cache encryption policy remain enforced.",
			Dictionary()));

	return tools;
}

Dictionary AIMCPHTTPServer::_get_ai_settings_server_info() const {
	Dictionary s;
	s["name"] = "ai_settings";
	s["enabled"] = true;
	s["builtin"] = true;
	s["description"] = "Built-in JunDot AI settings service.";
	return s;
}

Dictionary AIMCPHTTPServer::_get_ai_settings_snapshot() const {
	const AISettingsData settings = AISettings::load();
	Dictionary d;
	d["base_url"] = settings.base_url;
	d["model"] = settings.model;
	d["api_key_configured"] = !settings.api_key.is_empty();
	d["temperature"] = settings.temperature;
	d["max_tokens"] = settings.max_tokens;
	d["output_language"] = settings.output_language;
	d["tools_enabled"] = settings.tools_enabled;
	d["mcp_tools_enabled"] = settings.mcp_tools_enabled;
	d["context_char_budget"] = settings.context_char_budget;
	d["history_char_budget"] = settings.history_char_budget;
	d["max_tool_iterations"] = settings.max_tool_iterations;
	d["user_extra_instructions"] = settings.user_extra_instructions;
	d["engine_source_root"] = settings.engine_source_root;
	d["engine_source_cache_root"] = settings.engine_source_cache_root;
	d["engine_source_repository_url"] = JUNDOT_ENGINE_SOURCE_REPOSITORY_URL;
	d["encrypt_engine_source_cache"] = true;
	d["external_api_enabled"] = settings.external_api_enabled;
	d["external_api_port"] = settings.external_api_port;
	d["external_api_bind_address"] = settings.external_api_bind_address;
	d["protected_fields"] = _external_mcp_protected_fields();
	return d;
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

String AIMCPHTTPServer::_execute_builtin_tool(const String &p_tool_name, const String &p_args_json) {
	Array available_tools = _get_jundot_plugin_tools();
	bool available = false;
	for (int i = 0; i < available_tools.size(); i++) {
		Dictionary tool = available_tools[i];
		Dictionary fn_def = tool.get("function", Dictionary());
		if (String(fn_def.get("name", String())) == p_tool_name) {
			available = true;
			break;
		}
	}
	if (!available) {
		return "Built-in tool is not available in the current editor mode: " + p_tool_name;
	}

	Dictionary tool_call;
	Dictionary fn;
	fn["name"] = p_tool_name;
	fn["arguments"] = p_args_json;
	tool_call["id"] = "ext-builtin-" + String::num_int64(OS::get_singleton()->get_ticks_usec());
	tool_call["type"] = "function";
	tool_call["function"] = fn;

	Dictionary result = AIToolExecutor::execute(tool_call);
	return result.get("content", "Unknown error").operator String();
}

String AIMCPHTTPServer::_execute_ai_settings_tool(const String &p_tool_name, const String &p_args_json) {
	Dictionary response;

	if (p_tool_name == "get_config") {
		response["settings"] = _get_ai_settings_snapshot();
		return JSON::stringify(response);
	}

	if (p_tool_name == "reset_config") {
		const Error err = AISettings::reset_to_defaults();
		response["ok"] = err == OK;
		if (err != OK) {
			response["error"] = "Failed to reset AI settings.";
		}
		response["settings"] = _get_ai_settings_snapshot();
		return JSON::stringify(response);
	}

	if (p_tool_name != "update_config") {
		response["ok"] = false;
		response["error"] = "Unknown ai_settings tool.";
		return JSON::stringify(response);
	}

	Variant parsed = JSON::parse_string(p_args_json);
	if (parsed.get_type() != Variant::DICTIONARY) {
		response["ok"] = false;
		response["error"] = "Invalid JSON arguments.";
		return JSON::stringify(response);
	}

	Dictionary args = parsed;
	AISettingsData settings = AISettings::load();
	const bool external_api_runtime_changed =
			args.has("external_api_enabled") ||
			args.has("external_api_port") ||
			args.has("external_api_bind_address");

	if (args.has("base_url")) {
		settings.base_url = String(args["base_url"]).strip_edges();
	}
	if (args.has("model")) {
		settings.model = String(args["model"]).strip_edges();
	}
	if (args.has("api_key")) {
		settings.api_key = String(args["api_key"]);
	}
	if (args.has("temperature")) {
		settings.temperature = double(args["temperature"]);
	}
	if (args.has("max_tokens")) {
		settings.max_tokens = int(args["max_tokens"]);
	}
	if (args.has("output_language")) {
		settings.output_language = String(args["output_language"]).strip_edges();
	}
	if (args.has("tools_enabled")) {
		settings.tools_enabled = bool(args["tools_enabled"]);
	}
	if (args.has("mcp_tools_enabled")) {
		settings.mcp_tools_enabled = bool(args["mcp_tools_enabled"]);
	}
	if (args.has("context_char_budget")) {
		settings.context_char_budget = int(args["context_char_budget"]);
	}
	if (args.has("history_char_budget")) {
		settings.history_char_budget = int(args["history_char_budget"]);
	}
	if (args.has("max_tool_iterations")) {
		settings.max_tool_iterations = int(args["max_tool_iterations"]);
	}
	if (args.has("user_extra_instructions")) {
		settings.user_extra_instructions = String(args["user_extra_instructions"]);
	}
	if (args.has("engine_source_root")) {
		settings.engine_source_root = String(args["engine_source_root"]).strip_edges();
	}
	if (args.has("engine_source_cache_root")) {
		settings.engine_source_cache_root = String(args["engine_source_cache_root"]).strip_edges();
	}
	if (args.has("external_api_enabled")) {
		settings.external_api_enabled = bool(args["external_api_enabled"]);
	}
	if (args.has("external_api_port")) {
		settings.external_api_port = int(args["external_api_port"]);
	}
	if (args.has("external_api_bind_address")) {
		settings.external_api_bind_address = String(args["external_api_bind_address"]).strip_edges();
	}

	settings.system_prompt = AISettings::get_default_system_prompt();
	settings.engine_source_repository_url = JUNDOT_ENGINE_SOURCE_REPOSITORY_URL;
	settings.encrypt_engine_source_cache = true;

	const Error err = AISettings::save(settings);
	response["ok"] = err == OK;
	if (err != OK) {
		response["error"] = "Failed to save AI settings.";
	}
	response["settings"] = _get_ai_settings_snapshot();
	response["protected_fields"] = _external_mcp_protected_fields();
	if (external_api_runtime_changed) {
		response["external_api_restart_required"] = true;
		response["note"] = "External API server runtime changes are applied from the editor settings flow or after restart.";
	}
	return JSON::stringify(response);
}

void AIMCPHTTPServer::_handle_request(Ref<StreamPeerTCP> p_client, const String &p_method, const String &p_path, const String &p_body) {
	if (p_method == "OPTIONS") {
		_send_response(p_client, 200, "text/plain", "");
		return;
	}

	if (p_method == "GET" && p_path == "/api/mcp/tools") {
		Array tools = _get_jundot_plugin_tools();
		tools.append_array(AIToolDefs::get_mcp_tools());
		tools.append_array(_get_ai_settings_tools());
		String json = JSON::stringify(tools);
		_send_response(p_client, 200, "application/json", json);
		return;
	}

	if (p_method == "GET" && p_path == "/api/mcp/servers") {
		Vector<AISkillEntry> skills;
		Vector<AIMCPServerEntry> mcp_servers_list;
		Array server_list;
		Dictionary jundot_server;
		jundot_server["name"] = "jundot";
		jundot_server["enabled"] = true;
		jundot_server["builtin"] = true;
		jundot_server["description"] = "Selected built-in Jundot editor tools exposed to the MiMoCode plugin for files, edits, builds, logs, and commands.";
		server_list.push_back(jundot_server);
		server_list.push_back(_get_ai_settings_server_info());
		if (AIToolRegistry::load(skills, mcp_servers_list) == OK) {
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
			Dictionary warning;
			warning["warning"] = "Failed to load configured MCP server registry.";
			warning["servers"] = server_list;
			_send_response(p_client, 200, "application/json", JSON::stringify(warning));
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
		if (tool_name.contains(".") && (server_name.is_empty() || tool_name.begins_with(server_name + "."))) {
			const int dot = tool_name.find(".");
			server_name = tool_name.substr(0, dot);
			tool_name = tool_name.substr(dot + 1);
		}
		Variant args_value = req.get("arguments", "{}");
		String args_json;
		if (args_value.get_type() == Variant::DICTIONARY || args_value.get_type() == Variant::ARRAY) {
			args_json = JSON::stringify(args_value);
		} else {
			args_json = String(args_value);
		}
		if (args_json.is_empty()) {
			args_json = "{}";
		}

		if (server_name.is_empty() || tool_name.is_empty()) {
			_send_response(p_client, 400, "application/json", "{\"error\": \"Missing server or tool name\"}");
			return;
		}

		String result;
		if (server_name == "jundot") {
			result = _execute_builtin_tool(tool_name, args_json);
		} else if (server_name == "ai_settings") {
			result = _execute_ai_settings_tool(tool_name, args_json);
		} else {
			result = _execute_tool(server_name, tool_name, args_json);
		}
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
