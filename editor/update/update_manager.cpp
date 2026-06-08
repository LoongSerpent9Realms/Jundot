/**************************************************************************/
/*  update_manager.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             JUNDOT ENGINE                               */
/*                        https://jundotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Jundot Engine contributors (see AUTHORS.md). */
/**************************************************************************/

#include "update_manager.h"

#include "core/error/error_macros.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/version.h"
#include "editor/editor_string_names.h"
#include "editor/settings/editor_settings.h"
#include "scene/main/http_request.h"

namespace {

// Default manifest URL template (configured via editor settings).
// The launcher has more sophisticated URL resolution; here we keep it simple.
const char *DEFAULT_MANIFEST_URL = "https://github.com/LoongSerpent9Realms/Jundot-Auto/releases/latest/download/update-manifest.json";

/// Build the current Jundot version string (e.g. "1.7.2-beta").
String _get_current_version_string() {
#ifdef JUNDOT_VERSION_PATCH
	String ver = vformat("%d.%d.%d", JUNDOT_VERSION_MAJOR, JUNDOT_VERSION_MINOR, JUNDOT_VERSION_PATCH);
#else
	String ver = vformat("%d.%d", JUNDOT_VERSION_MAJOR, JUNDOT_VERSION_MINOR);
#endif
	String status(JUNDOT_VERSION_STATUS);
	if (!status.is_empty() && status != "stable") {
		ver += "-" + status;
	}
	return ver;
}

/// Get user's preferred update channel from editor settings.
String _get_user_channel() {
	int mode = int(EDITOR_GET("network/connection/check_for_updates"));
	switch (mode) {
		case 0: // DISABLED
			return "disabled";
		case 1: // AUTO — determine from current version status
			return (String(JUNDOT_VERSION_STATUS) == "stable") ? "stable" : "beta";
		case 2: // NEWEST_UNSTABLE
			return "beta";
		case 3: // NEWEST_STABLE
			return "stable";
		case 4: // NEWEST_PATCH
			return "stable";
		default:
			return "stable";
	}
}

} // namespace

// ═══════════════════════════════════════════════════════════════
//  UpdateManager
// ═══════════════════════════════════════════════════════════════

UpdateManager::UpdateManager() {
	// Generate stable machine ID
	machine_id = GrayscaleEvaluator::generate_machine_id();

	// Read manifest URL from editor settings (or use default)
	manifest_url = String(DEFAULT_MANIFEST_URL);

	// Create HTTPRequest for async manifest fetching
	http = memnew(HTTPRequest);
	http->set_https_proxy(EDITOR_GET("network/http_proxy/host"), EDITOR_GET("network/http_proxy/port"));
	http->set_timeout(15.0);
	add_child(http);
	http->connect("request_completed", callable_mp(this, &UpdateManager::_http_request_completed));
}

void UpdateManager::_bind_methods() {
	ADD_SIGNAL(MethodInfo("update_check_completed", PropertyInfo(Variant::INT, "status")));
}

void UpdateManager::check_for_updates() {
	// Don't re-check if already in progress
	if (check_status == CHECK_BUSY) {
		return;
	}

	// Skip if network is offline
	if (int(EDITOR_GET("network/connection/network_mode")) == EditorSettings::NETWORK_OFFLINE) {
		_set_check_status(CHECK_UP_TO_DATE);
		return;
	}

	// Skip if update checks are disabled
	if (int(EDITOR_GET("network/connection/check_for_updates")) == 0) {
		_set_check_status(CHECK_UP_TO_DATE);
		return;
	}

	_set_check_status(CHECK_BUSY);

	// Fetch the manifest from the remote URL
	// We use a lightweight manifest JSON (not the full GitHub Releases API)
	// to avoid rate limiting and simplify parsing.
	PackedStringArray headers;
	headers.push_back("Accept: application/json");
	headers.push_back(vformat("User-Agent: Jundot-Editor/%s", _get_current_version_string()));

	Error err = http->request(manifest_url, headers);
	if (err != OK) {
		_set_check_status(CHECK_ERROR);
	}
}

void UpdateManager::_http_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	if (p_result != OK) {
		_set_check_status(CHECK_ERROR);
		return;
	}

	if (p_response_code != 200) {
		// 404 or other → no manifest available (graceful degradation)
		_set_check_status(CHECK_UP_TO_DATE);
		return;
	}

	// Parse JSON response
	const uint8_t *r = p_body.ptr();
	String json_str = String::utf8((const char *)r, p_body.size());

	if (!manifest.parse(json_str)) {
		_set_check_status(CHECK_ERROR);
		return;
	}

	// Evaluate: should we notify?
	if (_should_notify_update()) {
		_set_check_status(CHECK_UPDATE_AVAILABLE);
	} else {
		_set_check_status(CHECK_UP_TO_DATE);
	}
}

void UpdateManager::_set_check_status(CheckStatus p_status) {
	check_status = p_status;
	emit_signal("update_check_completed", int(p_status));
}

bool UpdateManager::_should_notify_update() const {
	// Compare versions
	String current = _get_current_version_string();
	String target = manifest.get_version_string();

	if (compare_versions(target, current) <= 0) {
		return false; // Not newer
	}

	// Check channel eligibility
	if (!channel_matches(manifest.channel, _get_user_channel())) {
		return false; // Channel mismatch
	}

	// Check min version requirement
	if (!meets_min_version(current, manifest.min_version)) {
		return false; // Base version too old
	}

	// Check grayscale
	String reason;
	if (!GrayscaleEvaluator::is_eligible(machine_id, manifest.grayscale, &reason)) {
		return false; // Not in grayscale group
	}

	return true;
}

// ═══════════════════════════════════════════════════════════════
//  Launcher integration
// ═══════════════════════════════════════════════════════════════

String UpdateManager::find_launcher_path() const {
	if (!launcher_path_cache.is_empty()) {
		return launcher_path_cache;
	}

	// Look for launcher next to the engine executable
	String exe_path = OS::get_singleton()->get_executable_path();
	String exe_dir = exe_path.get_base_dir();

	// Try common names
	const char *candidates[] = {
		"JundotLauncher.exe",
		"Tools/Launcher/JundotLauncher.exe",
		"../tools/Launcher/bin/Debug/net8.0/JundotLauncher.exe",
	};

	for (const char *candidate : candidates) {
		String full_path = exe_dir.path_join(candidate);
		if (FileAccess::exists(full_path)) {
			launcher_path_cache = full_path;
			return full_path;
		}
	}

	return String(); // Not found
}

int UpdateManager::trigger_launcher_update() {
	String launcher = find_launcher_path();
	if (launcher.is_empty()) {
		WARN_PRINT("JundotLauncher.exe not found. Please build tools/Launcher/ first.");
		return -1;
	}

	String engine_dir = OS::get_singleton()->get_executable_path().get_base_dir();

	List<String> args;
	args.push_back("update");
	args.push_back(vformat("--engine-path=%s", engine_dir));

	String channel = _get_user_channel();
	if (channel != "stable" && channel != "disabled") {
		args.push_back(vformat("--channel=%s", channel));
	}

	// execute() is synchronous — blocks until the launcher completes.
	// The launcher handles download/install, then returns exit code.
	int exitcode = -1;
	Error err = OS::get_singleton()->execute(launcher, args, nullptr, &exitcode);
	if (err != OK) {
		WARN_PRINT(vformat("Failed to launch JundotLauncher for update. Error: %d", err));
		return -1;
	}

	if (exitcode != 0) {
		WARN_PRINT(vformat("JundotLauncher exited with code %d.", exitcode));
	}

	return exitcode;
}

int UpdateManager::trigger_launcher_rollback(const String &p_target_version) {
	String launcher = find_launcher_path();
	if (launcher.is_empty()) {
		return -1;
	}

	String engine_dir = OS::get_singleton()->get_executable_path().get_base_dir();

	List<String> args;
	args.push_back("rollback");
	args.push_back(vformat("--engine-path=%s", engine_dir));

	if (!p_target_version.is_empty()) {
		args.push_back(vformat("--target=%s", p_target_version));
	}

	int exitcode = -1;
	Error err = OS::get_singleton()->execute(launcher, args, nullptr, &exitcode);
	return (err == OK) ? exitcode : -1;
}

// ═══════════════════════════════════════════════════════════════
//  Editor settings registration
// ═══════════════════════════════════════════════════════════════

void register_update_settings() {
	// Reuse existing check_for_updates setting (0=DISABLED, 1=AUTO, etc.)
	// Add a new setting for manifest URL override
	EDITOR_DEF("network/connection/update_manifest_url", String(DEFAULT_MANIFEST_URL));
}
