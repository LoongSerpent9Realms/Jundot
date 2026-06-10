/**************************************************************************/
/*  ai_mcp_runtime.h                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#pragma once

#include "ai_tool_registry.h"
#include "core/io/file_access.h"
#include "core/os/mutex.h"
#include "core/os/process_id.h"
#include "core/os/thread.h"
#include "core/templates/list.h"
#include "core/variant/dictionary.h"
#include "core/variant/array.h"

class MCPServerRuntime : public RefCounted {
	GDCLASS(MCPServerRuntime, RefCounted);

public:
	enum class ServerState {
		STOPPED,
		STARTING,
		INITIALIZING,
		RUNNING,
		ERROR
	};

private:
	String server_name;
	String command;
	List<String> arguments;
	MCPServerLifecycle lifecycle = MCPServerLifecycle::DEFAULT;
	int timeout_ms = 30000;

	// Process handles
	ProcessID pid = 0;
	Ref<FileAccess> stdio_pipe;
	Ref<FileAccess> err_pipe;

	// Threading
	Mutex state_mutex;
	Thread reader_thread;
	SafeFlag shutdown_requested;

	// Message handling
	List<String> incoming_queue;
	HashMap<int, Dictionary> pending_responses;
	int next_request_id = 1;

	// Tool cache
	HashMap<String, Dictionary> tool_cache;
	String last_error;
	ServerState state = ServerState::STOPPED;
	String protocol_version;

	// Reader thread
	static void _reader_thread_func(void *p_userdata);
	void _reader_loop();
	Error _send_raw(const Vector<uint8_t> &p_data);
	String _read_line();

	// JSON-RPC helpers
	int _send_request(const String &p_method, const Dictionary &p_params);
	Dictionary _wait_for_response(int p_request_id, int p_timeout_ms);

	// Protocol
	Error _protocol_initialize();
	Error _protocol_tools_list();

public:
	MCPServerRuntime() {}
	~MCPServerRuntime();

	Error start(const AIMCPServerEntry &p_entry);
	void stop();
	bool is_alive() const;
	ServerState get_state() const;
	String get_last_error() const;
	String get_protocol_version() const;

	Array get_tools();
	Dictionary call_tool(const String &p_tool_name, const Dictionary &p_arguments);

	static MCPServerRuntime *get_singleton();
};
