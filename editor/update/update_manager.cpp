/**************************************************************************/
/*  update_manager.cpp                                                    */
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
#include "scene/main/timer.h"

namespace {

// Default manifest URL template (configured via editor settings).
// The launcher has more sophisticated URL resolution; here we keep it simple.
const char *DEFAULT_MANIFEST_URL = "https://github.com/LoongSerpent9Realms/Jundot/releases/latest/download/update-manifest.json";

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
		case 1: { // AUTO - determine from current version status
			const String status = String(JUNDOT_VERSION_STATUS).to_lower();
			if (status == "stable") {
				return "stable";
			}
			if (status == "beta" || status == "rc") {
				return "beta";
			}
			return "dev";
		}
		case 2: // NEWEST_UNSTABLE
			return "dev";
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

	// Register editor settings defaults (EditorSettings is guaranteed to exist here,
	// since UpdateManager is created after EditorSettings::create() in ProjectManager).
	EDITOR_DEF("network/connection/update_manifest_url", String(DEFAULT_MANIFEST_URL));

	// Read manifest URL from editor settings (or use default)
	manifest_url = String(DEFAULT_MANIFEST_URL);

	// Create HTTPRequest for async manifest fetching
	http = memnew(HTTPRequest);
	// Delay EDITOR_GET until EditorSettings is ready
	if (EditorSettings::get_singleton()) {
		http->set_https_proxy(EDITOR_GET("network/http_proxy/host"), EDITOR_GET("network/http_proxy/port"));
	}
	http->set_timeout(15.0);
	add_child(http);
	http->connect("request_completed", callable_mp(this, &UpdateManager::_http_request_completed));

	launcher_poll_timer = memnew(Timer);
	launcher_poll_timer->set_wait_time(0.25);
	add_child(launcher_poll_timer);
	launcher_poll_timer->connect("timeout", callable_mp(this, &UpdateManager::_poll_launcher_process));
}

void UpdateManager::_bind_methods() {
	ADD_SIGNAL(MethodInfo("update_check_completed", PropertyInfo(Variant::INT, "status")));
	ADD_SIGNAL(MethodInfo("update_launcher_started"));
	ADD_SIGNAL(MethodInfo("update_launcher_finished", PropertyInfo(Variant::INT, "exit_code")));
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

	// v1.1: if platform_downloads[] is present, replace the top-level
	// download_url / package_size / sha256 / platform / arch with the
	// entry that best matches the runtime OS + CPU architecture. This
	// ensures users on Windows/Linux/macOS (and x86_64 vs arm64) always
	// get the correct binary from a multi-architecture Release.
	if (manifest.platform_downloads.size() > 0) {
		String runtime_platform;
		String runtime_arch;

#if defined(WINDOWS_ENABLED) || defined(_WIN32) || defined(_WIN64)
		runtime_platform = "windows";
#elif defined(MACOS_ENABLED) || defined(__APPLE__)
		runtime_platform = "macos";
#elif defined(LINUX_ENABLED) || defined(__linux__)
		runtime_platform = "linux";
#elif defined(__FreeBSD__)
		runtime_platform = "freebsd";
#elif defined(__OpenBSD__)
		runtime_platform = "openbsd";
#else
		runtime_platform = OS::get_singleton()->get_name().to_lower();
#endif

#if defined(__aarch64__) || defined(_M_ARM64) || defined(__arm64__)
		runtime_arch = "arm64";
#elif defined(__x86_64__) || defined(_M_X64) || defined(__x86_64)
		runtime_arch = "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
		runtime_arch = "x86";
#else
		// Fallback: inspect the OS executable path / info.
		runtime_arch = "x86_64";
#endif

		PlatformDownload resolved;
		if (manifest.resolve_platform_download(runtime_platform, runtime_arch, resolved)) {
			// Only override if the resolved entry has non-empty values.
			if (!resolved.download_url.is_empty()) {
				manifest.download_url = resolved.download_url;
			}
			if (resolved.package_size > 0) {
				manifest.package_size = resolved.package_size;
			}
			if (!resolved.sha256.is_empty()) {
				manifest.sha256 = resolved.sha256;
			}
			if (!resolved.platform.is_empty()) {
				manifest.platform = resolved.platform;
			}
			if (!resolved.arch.is_empty()) {
				manifest.arch = resolved.arch;
			}
		} else {
			// The manifest lists other platforms but not ours. Treat as
			// "no update available for this platform" so the user doesn't
			// get a (wrong) update notification.
			WARN_PRINT(vformat("Update manifest has platform_downloads[] but no entry for %s-%s; suppressing update notification.",
					runtime_platform, runtime_arch));
			_set_check_status(CHECK_UP_TO_DATE);
			return;
		}
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

String UpdateManager::find_launcher_path() {
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
	if (launcher_pid != 0 && OS::get_singleton()->is_process_running(launcher_pid)) {
		return int(launcher_pid);
	}

	String launcher = find_launcher_path();
	if (launcher.is_empty()) {
		WARN_PRINT("JundotLauncher.exe not found. Please build tools/Launcher/ first.");
		return -1;
	}

	String engine_dir = OS::get_singleton()->get_executable_path().get_base_dir();

	List<String> args;
	args.push_back("update");
	args.push_back(vformat("--engine-path=%s", engine_dir));
	args.push_back("--yes");

	String channel = _get_user_channel();
	if (channel != "stable" && channel != "disabled") {
		args.push_back(vformat("--channel=%s", channel));
	}

	Error err = OS::get_singleton()->create_process(launcher, args, &launcher_pid, true);
	if (err != OK) {
		WARN_PRINT(vformat("Failed to launch JundotLauncher for update. Error: %d", err));
		launcher_pid = 0;
		return -1;
	}

	launcher_poll_timer->start();
	emit_signal("update_launcher_started");
	return int(launcher_pid);
}

void UpdateManager::_poll_launcher_process() {
	if (launcher_pid == 0) {
		launcher_poll_timer->stop();
		return;
	}
	if (OS::get_singleton()->is_process_running(launcher_pid)) {
		return;
	}

	const int exit_code = OS::get_singleton()->get_process_exit_code(launcher_pid);
	launcher_poll_timer->stop();
	launcher_pid = 0;
	emit_signal("update_launcher_finished", exit_code);
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
