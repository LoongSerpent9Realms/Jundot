/*  ai_build_bridge.cpp                                                    */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/
/* Copyright (c) 2024-present JunDot contributors.                        */
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
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/**************************************************************************/

#include "ai_build_bridge.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/templates/list.h"
#include "core/variant/dictionary.h"

// ---- Helpers ----

static String _repo_root() {
	// Walk up from the editor executable directory until we find version.py
	String dir = OS::get_singleton()->get_executable_path().get_base_dir();
	while (!dir.is_empty() && dir != "/" && dir != "\\") {
		if (FileAccess::exists(dir.path_join("version.py"))) {
			return dir;
		}
		dir = dir.get_base_dir();
	}
	// Fallback: current working directory
	return OS::get_singleton()->get_executable_path().get_base_dir();
}

static String _tools_dir() {
	return _repo_root().path_join("tools");
}

static String _package_builder_exe_path() {
	String base = _tools_dir().path_join("PackageBuilder").path_join("bin").path_join("Release").path_join("net8.0-windows");
	String exe = base.path_join("JundotPackageBuilder.exe");
	if (FileAccess::exists(exe)) {
		return exe;
	}
	// Try debug build
	exe = base.path_join("Debug").path_join("net8.0-windows").path_join("JundotPackageBuilder.exe").simplify_path();
	if (FileAccess::exists(exe)) {
		return exe;
	}
	return _tools_dir().path_join("PackageBuilder").path_join("bin").path_join("Release").path_join("net8.0-windows").path_join("JundotPackageBuilder.exe");
}

static String _artifacts_dir() {
	return _repo_root().path_join("artifacts");
}

static String _build_request_path() {
	return _artifacts_dir().path_join("ai_build_request.json");
}

static String _build_history_path() {
	return _artifacts_dir().path_join("packages").path_join(".build-history.json");
}

// ---- Public API ----

String AIBuildBridge::detect_repo_root() {
	return _repo_root();
}

Error AIBuildBridge::write_build_request() {
	String dir = _artifacts_dir();
	Ref<DirAccess> da = DirAccess::create_for_path(dir);
	if (da.is_valid()) {
		da->make_dir_recursive(dir);
	}

	Dictionary req;
	req["target"] = "editor";
	req["platform"] = "windows";
	req["arch"] = "x86_64";
	req["skip_build"] = false;
	req["mono"] = false;
	req["auto_update_version"] = true;
	req["generate_update_manifest"] = true;
	req["language"] = "zh_CN";
	req["requested_at"] = itos(static_cast<int64_t>(OS::get_singleton()->get_unix_time()));
	req["source"] = "ai_chat_panel";

	JSON json;
	String raw = json.stringify(req);

	Ref<FileAccess> file = FileAccess::open(_build_request_path(), FileAccess::WRITE);
	if (file.is_null()) {
		return ERR_FILE_CANT_WRITE;
	}
	file->store_string(raw);
	return OK;
}

Error AIBuildBridge::launch_package_builder() {
	String exe = _package_builder_exe_path();
	if (!FileAccess::exists(exe)) {
		return ERR_FILE_NOT_FOUND;
	}

	// Launch PackageBuilder as a detached process.
	// It will pick up the build request from artifacts/ai_build_request.json
	// if --ai-build flag is supported, or it opens the GUI for manual build.
	List<String> args;
	args.push_back("--ai-build");
	args.push_back(_build_request_path());

	ProcessID pid = 0;
	Error err = OS::get_singleton()->create_process(exe, args, &pid, false);
	if (err != OK) {
		return err;
	}
	return OK;
}

bool AIBuildBridge::get_latest_build_info(String &r_version, String &r_zip_path, String &r_manifest_path, String &r_build_log_path) {
	String path = _build_history_path();
	if (!FileAccess::exists(path)) {
		return false;
	}

	Error err = OK;
	String raw = FileAccess::get_file_as_string(path, &err);
	if (err != OK || raw.is_empty()) {
		return false;
	}

	JSON json;
	if (json.parse(raw) != OK) {
		return false;
	}

	Variant data = json.get_data();
	if (data.get_type() != Variant::DICTIONARY) {
		return false;
	}

	Dictionary root = data;
	Variant records_var = root.get("Records", Variant());
	if (records_var.get_type() != Variant::ARRAY) {
		return false;
	}

	Array records = records_var;
	if (records.is_empty()) {
		return false;
	}

	// Return the last (most recent) record
	Dictionary last = records[records.size() - 1];
	r_version = last.get("Version", String());
	r_zip_path = last.get("ZipPath", String());
	r_manifest_path = last.get("PackageDir", String());
	if (!r_manifest_path.is_empty()) {
		r_manifest_path = r_manifest_path.path_join("update-manifest.json");
	}
	r_build_log_path = last.get("BuildLogPath", String());

	return !r_zip_path.is_empty();
}

bool AIBuildBridge::is_build_ready() {
	String version, zip, manifest, log;
	return get_latest_build_info(version, zip, manifest, log);
}
