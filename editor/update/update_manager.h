/**************************************************************************/
/*  update_manager.h                                                      */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/**************************************************************************/

#pragma once

#include "core/os/os.h"
#include "editor/update/update_manifest.h"
#include "scene/main/node.h"

class HTTPRequest;
class Timer;
class UpdateDialog;

/// Engine-side update manager integrated into the project manager.
/// Fetches and evaluates the remote update-manifest.json, shows the
/// UpdateDialog, and triggers the C# JundotLauncher for actual
/// download/install operations.
class UpdateManager : public Node {
	GDCLASS(UpdateManager, Node);

public:
	/// Status of the last update check.
	enum CheckStatus {
		CHECK_IDLE,
		CHECK_BUSY,
		CHECK_ERROR,
		CHECK_UP_TO_DATE,
		CHECK_UPDATE_AVAILABLE,
	};

	/// Construct and auto-register as a singleton in the project manager scene.
	UpdateManager();

	/// Start an asynchronous manifest fetch from the default URL.
	/// On completion, emits update_check_completed.
	void check_for_updates();

	/// Return the manifest from the last successful fetch.
	const UpdateManifest &get_manifest() const { return manifest; }

	/// Get the current status of the check.
	CheckStatus get_check_status() const { return check_status; }

	/// Trigger the C# launcher in update mode.
	/// Returns the launcher process ID, or -1 on failure.
	int trigger_launcher_update();

	/// Trigger the C# launcher in rollback mode.
	int trigger_launcher_rollback(const String &p_target_version = String());

	/// Find the launcher executable path relative to the engine directory.
	String find_launcher_path();

protected:
	static void _bind_methods();

private:
	HTTPRequest *http = nullptr;
	Timer *launcher_poll_timer = nullptr;
	UpdateManifest manifest;
	CheckStatus check_status = CHECK_IDLE;
	String machine_id;
	String manifest_url;
	String launcher_path_cache;
	ProcessID launcher_pid = 0;

	/// Parse a JSON manifest from the HTTP response body.
	void _parse_manifest_from_json(const String &p_json);

	/// Evaluate whether the fetched manifest warrants an update notification.
	bool _should_notify_update() const;

	void _http_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	void _set_check_status(CheckStatus p_status);
	void _poll_launcher_process();
};

