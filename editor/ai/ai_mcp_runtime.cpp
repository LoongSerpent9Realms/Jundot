/**************************************************************************/
/*  ai_mcp_runtime.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#include "ai_mcp_runtime.h"

#include "core/io/json.h"
#include "core/os/os.h"
#include "core/templates/hash_set.h"

static String _resolve_mcp_command(const String &p_command) {
	if (p_command.is_empty() || p_command.is_absolute_path() || p_command.contains("/") || p_command.contains("\\")) {
		return p_command;
	}

	Vector<String> executable_names;
	executable_names.push_back(p_command);
#ifdef WINDOWS_ENABLED
	if (p_command.get_extension().is_empty()) {
		executable_names.push_back(p_command + ".exe");
		executable_names.push_back(p_command + ".cmd");
		executable_names.push_back(p_command + ".bat");
	}
#endif

	Vector<String> search_directories;
	const String path_environment = OS::get_singleton()->get_environment("PATH");
#ifdef WINDOWS_ENABLED
	search_directories.append_array(path_environment.split(";", false));
#else
	search_directories.append_array(path_environment.split(":", false));
#endif

	// uv installs uv/uvx here by default on Windows, but GUI applications may
	// have been started before the installer added the directory to PATH.
	if (OS::get_singleton()->has_environment("USERPROFILE")) {
		search_directories.push_back(OS::get_singleton()->get_environment("USERPROFILE").path_join(".local/bin"));
	}
	if (OS::get_singleton()->has_environment("HOME")) {
		search_directories.push_back(OS::get_singleton()->get_environment("HOME").path_join(".local/bin"));
	}

	for (const String &directory : search_directories) {
		const String clean_directory = directory.strip_edges().trim_prefix("\"").trim_suffix("\"");
		if (clean_directory.is_empty()) {
			continue;
		}
		for (const String &executable_name : executable_names) {
			const String candidate = clean_directory.path_join(executable_name);
			if (FileAccess::exists(candidate)) {
				return candidate;
			}
		}
	}

	return p_command;
}

MCPServerRuntime *MCPServerRuntime::get_singleton() {
	static Ref<MCPServerRuntime> singleton;
	if (singleton.is_null()) {
		singleton.instantiate();
	}
	return singleton.ptr();
}

MCPServerRuntime::~MCPServerRuntime() {
	stop();
}

Error MCPServerRuntime::start(const AIMCPServerEntry &p_entry) {
	if (p_entry.command.is_empty()) {
		MutexLock lock(state_mutex);
		last_error = p_entry.url.is_empty() ?
				"MCP server command is empty" :
				"URL-only MCP servers require a stdio bridge command";
		state = ServerState::ERROR;
		return ERR_UNAVAILABLE;
	}

	{
		MutexLock lock(state_mutex);
		if ((state == ServerState::RUNNING || state == ServerState::STARTING || state == ServerState::INITIALIZING) && server_name == p_entry.name) {
			return OK;
		}
	}

	stop();

	{
		MutexLock lock(state_mutex);
		server_name = p_entry.name;
		command = _resolve_mcp_command(p_entry.command);
		if (p_entry.command.get_file().get_basename().to_lower() == "uvx" && command == p_entry.command && !command.is_absolute_path()) {
			last_error = "Could not find uvx. Install uv or add %USERPROFILE%\\.local\\bin to PATH.";
			state = ServerState::ERROR;
			return ERR_FILE_NOT_FOUND;
		}
		environment = p_entry.environment;
		if (p_entry.url.begins_with("http://127.0.0.1") || p_entry.url.begins_with("https://127.0.0.1") ||
				p_entry.url.begins_with("http://localhost") || p_entry.url.begins_with("https://localhost")) {
			String no_proxy = environment.get("NO_PROXY", OS::get_singleton()->get_environment("NO_PROXY"));
			Vector<String> bypass_hosts = no_proxy.split(",", false);
			if (!bypass_hosts.has("127.0.0.1")) {
				bypass_hosts.push_back("127.0.0.1");
			}
			if (!bypass_hosts.has("localhost")) {
				bypass_hosts.push_back("localhost");
			}
			environment["NO_PROXY"] = String(",").join(bypass_hosts);
		}
		timeout_ms = p_entry.timeout_seconds * 1000;

		// Parse arguments into List<String>.
		arguments.clear();
		if (!p_entry.arguments.is_empty()) {
			Vector<String> arg_parts = p_entry.arguments.split(" ", false);
			for (const String &arg : arg_parts) {
				arguments.push_back(arg);
			}
		}

		lifecycle = p_entry.lifecycle;
		last_error.clear();
		state = ServerState::STARTING;
	}

	HashMap<String, String> previous_environment;
	HashSet<String> missing_environment;
	for (const Variant *key = environment.next(nullptr); key; key = environment.next(key)) {
		const String env_name = *key;
		if (OS::get_singleton()->has_environment(env_name)) {
			previous_environment[env_name] = OS::get_singleton()->get_environment(env_name);
		} else {
			missing_environment.insert(env_name);
		}
		OS::get_singleton()->set_environment(env_name, String(environment[*key]));
	}

	// Launch subprocess with pipe
	// MCP servers such as `uvx mcp-proxy` may spawn child processes that inherit
	// the stdio handles. A blocking pipe can then keep the reader thread stuck
	// forever after the launcher process is stopped. Polling a non-blocking pipe
	// lets shutdown_requested reliably terminate the reader loop.
	Dictionary result = OS::get_singleton()->execute_with_pipe(command, arguments, false);
	for (const KeyValue<String, String> &entry : previous_environment) {
		OS::get_singleton()->set_environment(entry.key, entry.value);
	}
	for (const String &env_name : missing_environment) {
		OS::get_singleton()->unset_environment(env_name);
	}

	if (result.is_empty()) {
		MutexLock lock(state_mutex);
		last_error = command.get_file().get_basename().to_lower() == "uvx" ?
				"Could not start uvx. Install uv and ensure uvx is available in PATH." :
				"execute_with_pipe failed to start: " + command;
		state = ServerState::ERROR;
		return FAILED;
	}

	{
		MutexLock lock(state_mutex);
		stdio_pipe = result.get("stdio", Ref<FileAccess>());
		err_pipe = result.get("stderr", Ref<FileAccess>());
		pid = result.get("pid", 0);

		if (stdio_pipe.is_null() || pid == 0) {
			last_error = "execute_with_pipe returned invalid handles";
			state = ServerState::ERROR;
			return FAILED;
		}

		shutdown_requested.clear();
		state = ServerState::INITIALIZING;
	}

	// Start reader thread
	reader_thread.start(_reader_thread_func, this);

	// Protocol handshake
	Error err = _protocol_initialize();
	if (err != OK) {
		stop();
		return err;
	}

	// Fetch tools list
	err = _protocol_tools_list();
	if (err != OK) {
		// Tools list is optional, continue even if it fails
		ERR_PRINT("MCPServerRuntime: tools/list failed for " + server_name);
	}

	{
		MutexLock lock(state_mutex);
		state = ServerState::RUNNING;
	}
	return OK;
}

void MCPServerRuntime::stop() {
	ProcessID process_to_kill = 0;
	{
		MutexLock lock(state_mutex);
		if (state == ServerState::STOPPED) {
			return;
		}
		shutdown_requested.set();
		process_to_kill = pid;
	}

	// Closing the child first unblocks a reader waiting for more stdio data.
	if (process_to_kill != 0) {
		OS::get_singleton()->kill(process_to_kill);
	}

	if (reader_thread.is_started()) {
		reader_thread.wait_to_finish();
	}

	MutexLock lock(state_mutex);
	pid = 0;

	if (stdio_pipe.is_valid()) {
		stdio_pipe.unref();
	}

	if (err_pipe.is_valid()) {
		err_pipe.unref();
	}

	incoming_queue.clear();
	pending_responses.clear();
	tool_cache.clear();
	state = ServerState::STOPPED;
}

bool MCPServerRuntime::is_alive() const {
	MutexLock lock(state_mutex);
	return state == ServerState::RUNNING && pid != 0 && OS::get_singleton()->is_process_running(pid);
}

bool MCPServerRuntime::is_running_server(const String &p_server_name) const {
	MutexLock lock(state_mutex);
	return state == ServerState::RUNNING && server_name == p_server_name && pid != 0 && OS::get_singleton()->is_process_running(pid);
}

MCPServerRuntime::ServerState MCPServerRuntime::get_state() const {
	MutexLock lock(state_mutex);
	return state;
}

String MCPServerRuntime::get_last_error() const {
	MutexLock lock(state_mutex);
	return last_error;
}

String MCPServerRuntime::get_protocol_version() const {
	MutexLock lock(state_mutex);
	return protocol_version;
}

Array MCPServerRuntime::get_tools() {
	MutexLock lock(state_mutex);
	Array tools;
	for (const KeyValue<String, Dictionary> &E : tool_cache) {
		tools.push_back(E.value);
	}
	return tools;
}

Dictionary MCPServerRuntime::call_tool(const String &p_tool_name, const Dictionary &p_arguments) {
	Dictionary result;

	if (!is_alive()) {
		result["is_error"] = true;
		result["content"] = "MCP server is not running: " + get_last_error();
		return result;
	}

	Dictionary params;
	params["name"] = p_tool_name;
	params["arguments"] = p_arguments;

	int request_id = _send_request("tools/call", params);
	if (request_id < 0) {
		result["is_error"] = true;
		result["content"] = "Failed to send tools/call request";
		return result;
	}

	Dictionary response = _wait_for_response(request_id, timeout_ms);

	// Check for JSON-RPC error
	if (response.has("error")) {
		result["is_error"] = true;
		if (response["error"].get_type() == Variant::DICTIONARY) {
			Dictionary error = response["error"];
			result["content"] = error.get("message", "Unknown error");
		} else {
			result["content"] = "Unknown error";
		}
		return result;
	}

	if (response.has("result")) {
		Dictionary rpc_result = response["result"];
		// MCP tools return content in content[].text
		if (rpc_result.has("content")) {
			Array content = rpc_result["content"];
			if (!content.is_empty() && content[0].get_type() == Variant::DICTIONARY) {
				Dictionary first = content[0];
				if (first.has("text")) {
					result["content"] = first["text"];
					return result;
				}
			}
		}
		result["content"] = JSON::stringify(rpc_result);
		return result;
	}

	result["is_error"] = true;
	result["content"] = "Invalid response format from MCP server";
	return result;
}

void MCPServerRuntime::_reader_thread_func(void *p_userdata) {
	MCPServerRuntime *runtime = static_cast<MCPServerRuntime *>(p_userdata);
	runtime->_reader_loop();
}

void MCPServerRuntime::_reader_loop() {
	while (!shutdown_requested.is_set()) {
		String line = _read_line();
		if (line.is_empty()) {
			OS::get_singleton()->delay_usec(10000); // 10ms
			continue;
		}

		Variant parsed = JSON::parse_string(line);
		if (parsed.get_type() != Variant::DICTIONARY) {
			continue;
		}

		Dictionary msg = parsed;

		// Handle response
		if (msg.has("id")) {
			int id = msg["id"];
			MutexLock lock(state_mutex);
			pending_responses[id] = msg;
			continue;
		}

		// Handle notification (no id)
		if (msg.has("method")) {
			// Handle notifications if needed (logging, etc.)
			// For now, just log them
			String method = msg["method"];
			ERR_PRINT("MCPServerRuntime: received notification: " + method);
		}
	}
}

String MCPServerRuntime::_read_line() {
	if (stdio_pipe.is_null()) {
		return String();
	}

	Vector<char> buffer;
	while (true) {
		Vector<uint8_t> data = stdio_pipe->get_buffer(1);
		if (data.is_empty()) {
			break;
		}
		uint8_t byte = data[0];
		if (byte == '\n') {
			break;
		}
		buffer.push_back(byte);
	}

	if (buffer.is_empty()) {
		return String();
	}

	Vector<uint8_t> utf8_buf;
	for (char c : buffer) {
		utf8_buf.push_back((uint8_t)c);
	}
	return String::utf8((const char *)utf8_buf.ptr(), utf8_buf.size());
}

Error MCPServerRuntime::_send_raw(const Vector<uint8_t> &p_data) {
	if (stdio_pipe.is_null()) {
		return ERR_INVALID_DATA;
	}

	bool ok = stdio_pipe->store_buffer(p_data);
	stdio_pipe->flush();
	return ok ? OK : ERR_BUSY;
}

int MCPServerRuntime::_send_request(const String &p_method, const Dictionary &p_params) {
	MutexLock lock(state_mutex);

	int id = next_request_id++;

	Dictionary request;
	request["jsonrpc"] = "2.0";
	request["id"] = id;
	request["method"] = p_method;
	request["params"] = p_params;

	String json = JSON::stringify(request);
	CharString cs = json.utf8();
	Vector<uint8_t> data;
	data.resize(cs.length() + 1);
	for (int i = 0; i < cs.length(); i++) {
		data.set(i, (uint8_t)cs[i]);
	}
	data.set(cs.length(), '\n');

	Error err = _send_raw(data);
	if (err != OK) {
		return -1;
	}

	return id;
}

Dictionary MCPServerRuntime::_wait_for_response(int p_request_id, int p_timeout_ms) {
	uint64_t start = OS::get_singleton()->get_ticks_usec();
	uint64_t timeout_usec = (uint64_t)p_timeout_ms * 1000;

	while (!shutdown_requested.is_set()) {
		uint64_t elapsed = OS::get_singleton()->get_ticks_usec() - start;
		if (elapsed > timeout_usec) {
			Dictionary error;
			Dictionary err_detail;
			err_detail["message"] = "MCP request timeout after " + itos(p_timeout_ms) + "ms";
			error["error"] = err_detail;
			return error;
		}

		{
			MutexLock lock(state_mutex);
			if (pending_responses.has(p_request_id)) {
				Dictionary response = pending_responses[p_request_id];
				pending_responses.erase(p_request_id);
				return response;
			}
		}

		OS::get_singleton()->delay_usec(10000); // 10ms
	}

	Dictionary error;
	Dictionary err_detail;
	err_detail["message"] = "MCP request cancelled";
	error["error"] = err_detail;
	return error;
}

Error MCPServerRuntime::_protocol_initialize() {
	Dictionary params;
	params["protocolVersion"] = "2024-11-05";

	Dictionary capabilities;
	capabilities["roots"] = Dictionary();
	capabilities["tools"] = Dictionary();
	params["capabilities"] = capabilities;

	Dictionary client_info;
	client_info["name"] = "Jundot-Editor";
	client_info["version"] = "1.0";
	params["clientInfo"] = client_info;

	int request_id = _send_request("initialize", params);
	if (request_id < 0) {
		last_error = "Failed to send initialize request";
		return FAILED;
	}

	Dictionary response = _wait_for_response(request_id, timeout_ms);

	if (response.has("error")) {
		last_error = "initialize error: " + JSON::stringify(response["error"]);
		return FAILED;
	}

	if (response.has("result")) {
		Dictionary result = response["result"];
		protocol_version = result.get("protocolVersion", "");
	}

	// Send notifications/initialized
	String notif = "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\",\"params\":{}}";
	CharString cs = notif.utf8();
	Vector<uint8_t> notif_data;
	notif_data.resize(cs.length() + 1);
	for (int i = 0; i < cs.length(); i++) {
		notif_data.set(i, (uint8_t)cs[i]);
	}
	notif_data.set(cs.length(), '\n');
	_send_raw(notif_data);

	return OK;
}

Error MCPServerRuntime::_protocol_tools_list() {
	int request_id = _send_request("tools/list", Dictionary());
	if (request_id < 0) {
		return FAILED;
	}

	Dictionary response = _wait_for_response(request_id, timeout_ms);
	if (response.has("error")) {
		return FAILED;
	}

	if (response.has("result")) {
		Dictionary result = response["result"];
		if (result.has("tools")) {
			Array tools = result["tools"];
			MutexLock lock(state_mutex);
			tool_cache.clear();
			for (int i = 0; i < tools.size(); i++) {
				if (tools[i].get_type() == Variant::DICTIONARY) {
					Dictionary tool = tools[i];
					String name = tool.get("name", "");
					if (!name.is_empty()) {
						// Prefix with server name
						Dictionary prefixed_tool = tool;
						prefixed_tool["name"] = server_name + "." + name;
						tool_cache[name] = prefixed_tool;
					}
				}
			}
		}
	}

	return OK;
}
