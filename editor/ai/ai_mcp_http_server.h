/**************************************************************************/
/*  ai_mcp_http_server.h                                                  */
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

#include "core/io/tcp_server.h"
#include "core/os/thread_safe.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

class AIMCPHTTPServer : public RefCounted {
	GDSOFTCLASS(AIMCPHTTPServer, RefCounted);

private:
	Ref<TCPServer> server;
	int port = 0;
	IPAddress bind_address;

	SafeFlag server_quit;
	Mutex server_lock;
	Thread server_thread;

	Vector<Ref<StreamPeerTCP>> clients;

	void _handle_client(Ref<StreamPeerTCP> p_client);
	void _send_response(Ref<StreamPeerTCP> p_client, int p_status_code, const String &p_content_type, const String &p_content);
	void _handle_request(Ref<StreamPeerTCP> p_client, const String &p_method, const String &p_path, const String &p_body);
	Array _get_ai_settings_tools() const;
	Array _get_jundot_plugin_tools() const;
	Dictionary _get_ai_settings_server_info() const;
	Dictionary _get_ai_settings_snapshot() const;
	String _execute_tool(const String &p_server_name, const String &p_tool_name, const String &p_args_json);
	String _execute_builtin_tool(const String &p_tool_name, const String &p_args_json);
	String _execute_ai_settings_tool(const String &p_tool_name, const String &p_args_json);

	static void _server_thread_poll(void *data);

public:
	AIMCPHTTPServer();
	~AIMCPHTTPServer();

	Error start(int p_port, const IPAddress &p_address = IPAddress("127.0.0.1"));
	void stop();
	bool is_running() const;
	int get_port() const;
};
