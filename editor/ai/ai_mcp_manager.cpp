/**************************************************************************/
/*  ai_mcp_manager.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#include "ai_mcp_manager.h"

AIMCPManager *AIMCPManager::singleton = nullptr;

AIMCPManager *AIMCPManager::get_singleton() {
	if (!singleton) {
		singleton = new AIMCPManager();
	}
	return singleton;
}

void AIMCPManager::cleanup() {
	if (singleton) {
		delete singleton;
		singleton = nullptr;
	}
}

void AIMCPManager::initialize() {
	if (initialized) {
		return;
	}
	settings = AISettings::load();
	http_server.instantiate();
	if (settings.external_api_enabled) {
		start_http_server();
	}
	initialized = true;
}

void AIMCPManager::shutdown() {
	if (!initialized) {
		return;
	}
	stop_http_server();
	http_server.unref();
	initialized = false;
}

void AIMCPManager::update_settings(const AISettingsData &p_new_settings) {
	bool was_running = is_http_server_running();
	bool should_run = p_new_settings.external_api_enabled;

	if (was_running && !should_run) {
		stop_http_server();
	} else if (!was_running && should_run) {
		start_http_server();
	} else if (was_running && should_run) {
		if (p_new_settings.external_api_port != settings.external_api_port ||
			p_new_settings.external_api_bind_address != settings.external_api_bind_address) {
			stop_http_server();
			settings = p_new_settings;
			start_http_server();
			return;
		}
	}

	settings = p_new_settings;
}

bool AIMCPManager::is_http_server_running() const {
	return http_server.is_valid() && http_server->is_running();
}

Error AIMCPManager::start_http_server() {
	if (!http_server.is_valid()) {
		http_server.instantiate();
	}
	if (http_server->is_running()) {
		http_server->stop();
	}
	return http_server->start(settings.external_api_port, IPAddress(settings.external_api_bind_address));
}

void AIMCPManager::stop_http_server() {
	if (http_server.is_valid() && http_server->is_running()) {
		http_server->stop();
	}
}

int AIMCPManager::get_http_server_port() const {
	return http_server.is_valid() ? http_server->get_port() : 0;
}
