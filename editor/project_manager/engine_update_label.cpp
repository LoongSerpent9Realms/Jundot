/**************************************************************************/
/*  engine_update_label.cpp                                               */
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

#include "engine_update_label.h"

#include "core/io/file_access.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/version.h"
#include "editor/editor_string_names.h"
#include "editor/settings/editor_settings.h"
#include "editor/update/update_manifest.h"
#include "scene/main/http_request.h"

namespace {

const char *JUNDOT_AUTO_MANIFEST_URL = "https://github.com/LoongSerpent9Realms/Jundot/releases/latest/download/update-manifest.json";
const char *JUNDOT_AUTO_RELEASES_PAGE = "https://github.com/LoongSerpent9Realms/Jundot/releases";

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

String _get_user_channel() {
	EngineUpdateLabel::UpdateMode mode = EngineUpdateLabel::UpdateMode(int(EDITOR_GET("network/connection/check_for_updates")));
	switch (mode) {
		case EngineUpdateLabel::UpdateMode::DISABLED:
			return "disabled";
		case EngineUpdateLabel::UpdateMode::AUTO: {
			const String status = String(JUNDOT_VERSION_STATUS).to_lower();
			if (status == "stable") {
				return "stable";
			}
			if (status == "beta" || status == "rc") {
				return "beta";
			}
			return "dev";
		}
		case EngineUpdateLabel::UpdateMode::NEWEST_UNSTABLE:
			return "dev";
		case EngineUpdateLabel::UpdateMode::NEWEST_STABLE:
		case EngineUpdateLabel::UpdateMode::NEWEST_PATCH:
			return "stable";
	}
	return "stable";
}

bool _is_newest_patch_mode() {
	return EngineUpdateLabel::UpdateMode(int(EDITOR_GET("network/connection/check_for_updates"))) == EngineUpdateLabel::UpdateMode::NEWEST_PATCH;
}

String _get_http_request_result_message(int p_result) {
	switch (p_result) {
		case HTTPRequest::RESULT_CHUNKED_BODY_SIZE_MISMATCH:
			return TTR("Received an incomplete response from the update server.");
		case HTTPRequest::RESULT_CANT_CONNECT:
			return TTR("Could not connect to the update server.");
		case HTTPRequest::RESULT_CANT_RESOLVE:
			return TTR("Could not resolve the update server address.");
		case HTTPRequest::RESULT_CONNECTION_ERROR:
			return TTR("The connection to the update server failed.");
		case HTTPRequest::RESULT_TLS_HANDSHAKE_ERROR:
			return TTR("Could not establish a secure connection to the update server.");
		case HTTPRequest::RESULT_NO_RESPONSE:
			return TTR("The update server did not respond.");
		case HTTPRequest::RESULT_BODY_SIZE_LIMIT_EXCEEDED:
			return TTR("The update response was too large.");
		case HTTPRequest::RESULT_BODY_DECOMPRESS_FAILED:
			return TTR("Could not decompress the update response.");
		case HTTPRequest::RESULT_REQUEST_FAILED:
			return TTR("The update request failed.");
		case HTTPRequest::RESULT_DOWNLOAD_FILE_CANT_OPEN:
			return TTR("Could not open the update download file.");
		case HTTPRequest::RESULT_DOWNLOAD_FILE_WRITE_ERROR:
			return TTR("Could not write the update download file.");
		case HTTPRequest::RESULT_REDIRECT_LIMIT_REACHED:
			return TTR("The update request was redirected too many times.");
		case HTTPRequest::RESULT_TIMEOUT:
			return TTR("The update request timed out.");
		default:
			return TTR("The update request failed.");
	}
}

} // namespace

bool EngineUpdateLabel::_can_check_updates() const {
	return int(EDITOR_GET("network/connection/network_mode")) == EditorSettings::NETWORK_ONLINE &&
			UpdateMode(int(EDITOR_GET("network/connection/check_for_updates"))) != UpdateMode::DISABLED;
}

void EngineUpdateLabel::_check_update() {
	checked_update = true;
	_set_status(UpdateStatus::BUSY);

	String manifest_url = EDITOR_GET("network/connection/update_manifest_url");
	if (manifest_url.is_empty()) {
		manifest_url = JUNDOT_AUTO_MANIFEST_URL;
	}

	PackedStringArray headers;
	headers.push_back("Accept: application/json");
	headers.push_back(vformat("User-Agent: Jundot-Editor/%s", _get_current_version_string()));
	Error err = http->request(manifest_url, headers);
	if (err != OK) {
		_set_status(UpdateStatus::ERROR);
		_set_message(vformat(TTR("Failed to check for updates. Error: %d."), err), theme_cache.error_color);
	}
}

void EngineUpdateLabel::_http_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	if (p_result != OK) {
		_set_status(UpdateStatus::ERROR);
		_set_message(vformat(TTR("Failed to check for updates. %s (Error: %d)."), _get_http_request_result_message(p_result), p_result), theme_cache.error_color);
		return;
	}

	if (p_response_code != 200) {
		_set_status(UpdateStatus::ERROR);
		_set_message(vformat(TTR("Failed to check for updates. Response code: %d."), p_response_code), theme_cache.error_color);
		return;
	}

	const uint8_t *r = p_body.ptr();
	String s = String::utf8((const char *)r, p_body.size());

	UpdateManifest manifest;
	if (!manifest.parse(s)) {
		_set_status(UpdateStatus::ERROR);
		_set_message(TTR("Failed to parse version JSON."), theme_cache.error_color);
		return;
	}

	String current_version = _get_current_version_string();
	String target_version = manifest.get_version_string();
	available_newer_version = String();
	available_newer_url = String();
	available_manifest.clear();

	String grayscale_reason;
	if (compare_versions(target_version, current_version) > 0 &&
			(!_is_newest_patch_mode() || manifest.version.minor == JUNDOT_VERSION_MINOR) &&
			channel_matches(manifest.channel, _get_user_channel()) &&
			meets_min_version(current_version, manifest.min_version) &&
			GrayscaleEvaluator::is_eligible(GrayscaleEvaluator::generate_machine_id(), manifest.grayscale, &grayscale_reason)) {
		available_newer_version = target_version;
		available_newer_url = JUNDOT_AUTO_RELEASES_PAGE;
		available_manifest = manifest;
	}

	if (!available_newer_version.is_empty()) {
		_set_status(UpdateStatus::UPDATE_AVAILABLE);
		_set_message(vformat(TTR("Update available: %s."), available_newer_version), theme_cache.update_color);
	} else if (available_newer_version.is_empty()) {
		_set_status(UpdateStatus::UP_TO_DATE);
	}
}

void EngineUpdateLabel::_set_message(const String &p_message, const Color &p_color) {
	if (is_disabled()) {
		add_theme_color_override("font_disabled_color", p_color);
	} else {
		add_theme_color_override(SceneStringName(font_color), p_color);
	}
	set_text(p_message);
}

void EngineUpdateLabel::_set_status(UpdateStatus p_status) {
	status = p_status;
	show();

	switch (status) {
		case UpdateStatus::OFFLINE: {
			set_disabled(false);
			if (int(EDITOR_GET("network/connection/network_mode")) == EditorSettings::NETWORK_OFFLINE) {
				_set_message(TTR("Offline mode, update checks disabled."), theme_cache.disabled_color);
			} else {
				_set_message(TTR("Update checks disabled."), theme_cache.disabled_color);
			}
			set_accessibility_live(AccessibilityServerEnums::AccessibilityLiveMode::LIVE_OFF);
			set_tooltip_text("");
			break;
		}

		case UpdateStatus::BUSY: {
			set_disabled(true);
			_set_message(TTR("Checking for updates..."), theme_cache.disabled_color);
			set_accessibility_live(AccessibilityServerEnums::AccessibilityLiveMode::LIVE_POLITE);
			set_tooltip_text("");
		} break;

		case UpdateStatus::ERROR: {
			set_disabled(false);
			set_accessibility_live(AccessibilityServerEnums::AccessibilityLiveMode::LIVE_POLITE);
			set_tooltip_text(TTR("An error has occurred. Click to try again."));
		} break;

		case UpdateStatus::UPDATE_AVAILABLE: {
			set_disabled(false);
			set_accessibility_live(AccessibilityServerEnums::AccessibilityLiveMode::LIVE_POLITE);
			set_tooltip_text(TTR("Click to open download page."));
		} break;

		case UpdateStatus::UP_TO_DATE: {
			set_disabled(true);
			_set_message(TTR("Jundot is up to date."), theme_cache.default_color);
			set_accessibility_live(AccessibilityServerEnums::AccessibilityLiveMode::LIVE_POLITE);
			set_tooltip_text("");
		} break;

		default: {
		}
	}
}

void EngineUpdateLabel::_notification(int p_what) {
	switch (p_what) {
		case EditorSettings::NOTIFICATION_EDITOR_SETTINGS_CHANGED: {
			if (!EditorSettings::get_singleton()->check_changed_settings_in_group("network/connection")) {
				break;
			}

			if (_can_check_updates()) {
				_check_update();
			} else {
				_set_status(UpdateStatus::OFFLINE);
			}
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			theme_cache.default_color = get_theme_color(SceneStringName(font_color), "Button");
			theme_cache.disabled_color = get_theme_color("font_disabled_color", "Button");
			theme_cache.error_color = get_theme_color("error_color", EditorStringName(Editor));
			theme_cache.update_color = get_theme_color("warning_color", EditorStringName(Editor));
		} break;

		case NOTIFICATION_READY: {
			if (_can_check_updates()) {
				_check_update();
			} else {
				_set_status(UpdateStatus::OFFLINE);
			}
		} break;
	}
}

void EngineUpdateLabel::_bind_methods() {
	ADD_SIGNAL(MethodInfo("offline_clicked"));
	ADD_SIGNAL(MethodInfo("update_download_requested", PropertyInfo(Variant::STRING, "version"), PropertyInfo(Variant::STRING, "url")));
}

void EngineUpdateLabel::pressed() {
	switch (status) {
		case UpdateStatus::OFFLINE: {
			emit_signal("offline_clicked");
		} break;

		case UpdateStatus::ERROR: {
			_check_update();
		} break;

		case UpdateStatus::UPDATE_AVAILABLE: {
			// Emit signal so the project manager can show UpdateDialog.
			// The signal handler is responsible for triggering the launcher.
			if (available_newer_url.is_empty()) {
				available_newer_url = String(JUNDOT_AUTO_RELEASES_PAGE) + "/tag/" + available_newer_version;
			}
			emit_signal("update_download_requested", available_newer_version, available_newer_url);
		} break;

		default: {
		}
	}
}

void EngineUpdateLabel::_trigger_launcher_update() {
	// Find the launcher next to the current executable
	String exe_dir = OS::get_singleton()->get_executable_path().get_base_dir();
	String launcher_path = exe_dir.path_join("JundotLauncher.exe");

	// Also try the dev/build path
	if (!FileAccess::exists(launcher_path)) {
		launcher_path = exe_dir.path_join("Tools/Launcher/JundotLauncher.exe");
	}

	if (FileAccess::exists(launcher_path)) {
		// Launch the launcher in update mode
		List<String> args;
		args.push_back("update");
		args.push_back(vformat("--engine-path=\"%s\"", exe_dir));

		int exitcode = -1;
		Error err = OS::get_singleton()->execute(launcher_path, args, nullptr, &exitcode);
		if (err == OK) {
			return; // Launcher completed successfully
		}
	}

	// Fallback: open GitHub Releases page in browser
	if (available_newer_url.is_empty()) {
		available_newer_url = String(JUNDOT_AUTO_RELEASES_PAGE) + "/tag/" + available_newer_version;
	}
	OS::get_singleton()->shell_open(available_newer_url);
}

EngineUpdateLabel::EngineUpdateLabel() {
	set_underline_mode(UNDERLINE_MODE_ON_HOVER);
	EDITOR_DEF("network/connection/update_manifest_url", String(JUNDOT_AUTO_MANIFEST_URL));

	http = memnew(HTTPRequest);
	// Delay EDITOR_GET until EditorSettings is ready
	if (EditorSettings::get_singleton()) {
		http->set_https_proxy(EDITOR_GET("network/http_proxy/host"), EDITOR_GET("network/http_proxy/port"));
	}
	http->set_timeout(30.0);
	add_child(http);
	http->connect("request_completed", callable_mp(this, &EngineUpdateLabel::_http_request_completed));
}
