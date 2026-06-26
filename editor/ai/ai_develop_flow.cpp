/*  ai_develop_flow.cpp                                                  */
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

#include "ai_develop_flow.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/os/time.h"

static String _develop_flow_path() {
	return OS::get_singleton()->get_user_data_dir().path_join("ai_develop_flow.json");
}

static Dictionary _load_develop_flow() {
	const String path = _develop_flow_path();
	if (!FileAccess::exists(path)) {
		return Dictionary();
	}
	Error err = OK;
	const String content = FileAccess::get_file_as_string(path, &err);
	if (err != OK) {
		return Dictionary();
	}
	const Variant parsed = JSON::parse_string(content);
	return parsed.get_type() == Variant::DICTIONARY ? Dictionary(parsed) : Dictionary();
}

static void _save_develop_flow(AIDevelopFlow::Stage p_stage, const String &p_detail) {
	Dictionary state;
	state["stage"] = int(p_stage);
	state["detail"] = p_detail;
	state["updated_at"] = Time::get_singleton()->get_unix_time_from_system();
	const String path = _develop_flow_path();
	DirAccess::make_dir_recursive_absolute(path.get_base_dir());
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	if (file.is_valid()) {
		file->store_string(JSON::stringify(state, "\t"));
	}
}

void AIDevelopFlow::reset() { _save_develop_flow(IDLE, String()); }
void AIDevelopFlow::record_modified(const String &p_file) { _save_develop_flow(MODIFIED, "Local change prepared: " + p_file); }
void AIDevelopFlow::record_build_started() { _save_develop_flow(BUILDING, "Engine build is running."); }
void AIDevelopFlow::record_build_result(bool p_success, const String &p_detail) {
	if (get_stage() == BUILDING) {
		_save_develop_flow(p_success ? BUILT : FAILED, p_detail);
	}
}
void AIDevelopFlow::record_restart_requested() { _save_develop_flow(RESTARTING, "Switching to the AI-compiled editor."); }
void AIDevelopFlow::resume_after_restart() { if (get_stage() == RESTARTING) { _save_develop_flow(USER_VERIFICATION, "Waiting for the user to verify the feature in the restarted editor."); } }
void AIDevelopFlow::record_user_verification(bool p_passed, const String &p_detail) {
	_save_develop_flow(AI_VERIFICATION, String(p_passed ? "PASSED: " : "FAILED: ") + p_detail);
}
void AIDevelopFlow::record_ai_verification(bool p_passed, const String &p_detail) { _save_develop_flow(p_passed ? READY_TO_UPLOAD : FAILED, p_detail); }
void AIDevelopFlow::record_simulated_upload(const String &p_file) { _save_develop_flow(COMPLETE, "Upload validation demonstrated for " + p_file + "; commit and push skipped."); }

AIDevelopFlow::Stage AIDevelopFlow::get_stage() {
	return Stage(int(_load_develop_flow().get("stage", int(IDLE))));
}

bool AIDevelopFlow::is_waiting_for_user() { return get_stage() == USER_VERIFICATION; }

String AIDevelopFlow::get_status_text() {
	const Dictionary state = _load_develop_flow();
	const Stage stage = Stage(int(state.get("stage", int(IDLE))));
	const String detail = state.get("detail", String());
	static const char *names[] = { "Ready", "Modified", "Building", "Built", "Restarting", "User verification", "AI verification", "Ready for simulated upload", "Complete", "Failed" };
	const int progress = stage == FAILED ? 0 : MIN(int(stage), int(COMPLETE));
	String flow = vformat("Develop Mode | Modify %s | Build %s | Restart %s | User verify %s | AI verify %s | Upload dry-run %s", progress >= MODIFIED ? "OK" : "-", progress >= BUILT ? "OK" : (stage == BUILDING ? "..." : "-"), progress >= USER_VERIFICATION ? "OK" : (stage == RESTARTING ? "..." : "-"), progress >= AI_VERIFICATION ? "OK" : (stage == USER_VERIFICATION ? "WAIT" : "-"), progress >= READY_TO_UPLOAD ? "OK" : (stage == AI_VERIFICATION ? "..." : "-"), stage == COMPLETE ? "OK" : "-");
	return vformat("%s\nCurrent: %s%s", flow, names[int(stage)], detail.is_empty() ? String() : " - " + detail);
}
