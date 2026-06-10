/**************************************************************************/
/*  ai_mcp_manager.h                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#pragma once

#include "ai_mcp_http_server.h"
#include "ai_settings.h"

class AIMCPManager {
private:
	static AIMCPManager *singleton;
	Ref<AIMCPHTTPServer> http_server;
	AISettingsData settings;
	bool initialized = false;

	AIMCPManager() {}
	~AIMCPManager() { shutdown(); }

public:
	static AIMCPManager *get_singleton();
	static void cleanup();

	void initialize();
	void shutdown();
	void update_settings(const AISettingsData &p_new_settings);
	bool is_http_server_running() const;
	Error start_http_server();
	void stop_http_server();
	int get_http_server_port() const;
};
