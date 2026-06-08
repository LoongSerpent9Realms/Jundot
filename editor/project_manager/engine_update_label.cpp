/**************************************************************************/
/*  engine_update_label.cpp                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             JUNDOT ENGINE                               */
/*                        https://jundotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Jundot Engine contributors (see AUTHORS.md). */
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
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/version.h"
#include "editor/editor_string_names.h"
#include "editor/settings/editor_settings.h"
#include "scene/main/http_request.h"

namespace {

const char *JUNDOT_AUTO_RELEASES_API = "https://api.github.com/repos/LoongSerpent9Realms/Jundot-Auto/releases";
const char *JUNDOT_AUTO_RELEASES_PAGE = "https://github.com/LoongSerpent9Realms/Jundot-Auto/releases";

bool _split_github_release_tag(const String &p_tag, String &r_base_version, String &r_release_status) {
	String tag = p_tag.strip_edges();
	tag = tag.trim_prefix("refs/tags/");
	tag = tag.trim_prefix("jundot-");
	tag = tag.trim_prefix("v");

	int separator = tag.find_char('-');
	if (separator == -1) {
		r_base_version = tag;
		r_release_status = "stable";
	} else {
		r_base_version = tag.substr(0, separator);
		r_release_status = tag.substr(separator + 1);
	}

	return r_base_version.split(".").size() >= 2;
}

} // namespace

bool EngineUpdateLabel::_can_check_updates() const {
	return int(EDITOR_GET("network/connection/network_mode")) == EditorSettings::NETWORK_ONLINE &&
			UpdateMode(int(EDITOR_GET("network/connection/check_for_updates"))) != UpdateMode::DISABLED;
}

void EngineUpdateLabel::_check_update() {
	checked_update = true;
	_set_status(UpdateStatus::BUSY);

	PackedStringArray headers;
	headers.push_back("Accept: application/vnd.github+json");
	headers.push_back("User-Agent: Jundot-Auto");
	http->request(JUNDOT_AUTO_RELEASES_API, headers);
}

void EngineUpdateLabel::_http_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	if (p_result != OK) {
		_set_status(UpdateStatus::ERROR);
		_set_message(vformat(TTR("Failed to check for updates. Error: %d."), p_result), theme_cache.error_color);
		return;
	}

	if (p_response_code != 200) {
		_set_status(UpdateStatus::ERROR);
		_set_message(vformat(TTR("Failed to check for updates. Response code: %d."), p_response_code), theme_cache.error_color);
		return;
	}

	Array version_array;
	{
		const uint8_t *r = p_body.ptr();
		String s = String::utf8((const char *)r, p_body.size());

		Variant result = JSON::parse_string(s);
		if (result == Variant()) {
			_set_status(UpdateStatus::ERROR);
			_set_message(TTR("Failed to parse version JSON."), theme_cache.error_color);
			return;
		}
		if (result.get_type() != Variant::ARRAY) {
			_set_status(UpdateStatus::ERROR);
			_set_message(TTR("Received JSON data is not a valid version array."), theme_cache.error_color);
			return;
		}
		version_array = result;
	}

	UpdateMode update_mode = UpdateMode(int(EDITOR_GET("network/connection/check_for_updates")));
	if (update_mode == UpdateMode::AUTO) {
		if (_get_version_type(JUNDOT_VERSION_STATUS) == VersionType::STABLE) {
			update_mode = UpdateMode::NEWEST_STABLE;
		} else {
			update_mode = UpdateMode::NEWEST_UNSTABLE;
		}
	}
	bool stable_only = update_mode == UpdateMode::NEWEST_STABLE || update_mode == UpdateMode::NEWEST_PATCH;

	available_newer_version = String();
	available_newer_url = String();
	for (const Variant &data_bit : version_array) {
		const Dictionary version_info = data_bit;

		String base_version_string;
		Array releases;
		String release_url;
		if (version_info.has("tag_name")) {
			String release_string;
			if (!_split_github_release_tag(version_info.get("tag_name", ""), base_version_string, release_string)) {
				continue;
			}

			Dictionary release_info;
			release_info["name"] = release_string;
			releases.push_back(release_info);
			release_url = version_info.get("html_url", JUNDOT_AUTO_RELEASES_PAGE);
		} else {
			base_version_string = version_info.get("name", "");
			releases = version_info.get("releases", Array());
		}

		const PackedStringArray version_bits = base_version_string.split(".");

		if (version_bits.size() < 2) {
			continue;
		}

		int minor = version_bits[1].to_int();
		if (version_bits[0].to_int() != JUNDOT_VERSION_MAJOR || minor < JUNDOT_VERSION_MINOR) {
			continue;
		}

		int patch = 0;
		if (version_bits.size() >= 3) {
			patch = version_bits[2].to_int();
		}

		if (minor == JUNDOT_VERSION_MINOR && patch < JUNDOT_VERSION_PATCH) {
			continue;
		}

		if (update_mode == UpdateMode::NEWEST_PATCH && minor > JUNDOT_VERSION_MINOR) {
			continue;
		}

		if (releases.is_empty()) {
			continue;
		}

		const Dictionary newest_release = releases[0];
		const String release_string = newest_release.get("name", "unknown");

		int release_index;
		VersionType release_type = _get_version_type(release_string, &release_index);

		if (minor > JUNDOT_VERSION_MINOR || patch > JUNDOT_VERSION_PATCH) {
			if (stable_only && release_type != VersionType::STABLE) {
				continue;
			}

			available_newer_version = vformat("%s-%s", base_version_string, release_string);
			available_newer_url = release_url;
			break;
		}

		int current_version_index;
		VersionType current_version_type = _get_version_type(JUNDOT_VERSION_STATUS, &current_version_index);

		if (int(release_type) > int(current_version_type)) {
			break;
		}

		if (int(release_type) == int(current_version_type) && release_index <= current_version_index) {
			break;
		}

		available_newer_version = vformat("%s-%s", base_version_string, release_string);
		available_newer_url = release_url;
		break;
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
	if (status == UpdateStatus::BUSY || status == UpdateStatus::UP_TO_DATE) {
		// Hide the label to prevent unnecessary distraction.
		hide();
		return;
	} else {
		show();
	}

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

		default: {
		}
	}
}

EngineUpdateLabel::VersionType EngineUpdateLabel::_get_version_type(const String &p_string, int *r_index) const {
	VersionType type = VersionType::UNKNOWN;
	String index_string;

	static HashMap<String, VersionType> type_map;
	if (type_map.is_empty()) {
		type_map["stable"] = VersionType::STABLE;
		type_map["rc"] = VersionType::RC;
		type_map["beta"] = VersionType::BETA;
		type_map["alpha"] = VersionType::ALPHA;
		type_map["dev"] = VersionType::DEV;
	}

	for (const KeyValue<String, VersionType> &kv : type_map) {
		if (p_string.begins_with(kv.key)) {
			index_string = p_string.trim_prefix(kv.key);
			type = kv.value;
			break;
		}
	}

	if (r_index) {
		if (index_string.is_empty()) {
			*r_index = DEV_VERSION;
		} else {
			*r_index = index_string.to_int();
		}
	}
	return type;
}

String EngineUpdateLabel::_extract_sub_string(const String &p_line) const {
	int j = p_line.find_char('"') + 1;
	return p_line.substr(j, p_line.find_char('"', j) - j);
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

	http = memnew(HTTPRequest);
	http->set_https_proxy(EDITOR_GET("network/http_proxy/host"), EDITOR_GET("network/http_proxy/port"));
	http->set_timeout(10.0);
	add_child(http);
	http->connect("request_completed", callable_mp(this, &EngineUpdateLabel::_http_request_completed));
}
