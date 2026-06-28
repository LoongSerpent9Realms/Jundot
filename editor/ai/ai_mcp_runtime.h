/**************************************************************************/
/*  ai_mcp_runtime.h                                                      */
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

#pragma once

#include "core/io/file_access.h"
#include "core/os/mutex.h"
#include "core/os/process_id.h"
#include "core/os/thread.h"
#include "core/templates/list.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "editor/ai/ai_tool_registry.h"

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
	Dictionary environment;
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
	bool is_running_server(const String &p_server_name) const;
	ServerState get_state() const;
	String get_last_error() const;
	String get_protocol_version() const;

	Array get_tools();
	Dictionary call_tool(const String &p_tool_name, const Dictionary &p_arguments);

	static MCPServerRuntime *get_singleton();
};
