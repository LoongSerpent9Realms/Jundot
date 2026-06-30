/*  ai_tool_executor.cpp                                                  */
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

#include "ai_tool_executor.h"

#include "core/config/project_settings.h"
#include "core/crypto/crypto_core.h"
#include "core/error/error_macros.h"
#include "core/extension/gdextension_manager.h"
#include "core/input/input_enums.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/math/rect2.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/os/thread_safe.h"
#include "core/os/time.h"
#include "core/templates/hash_map.h"
#include "core/string/string_builder.h"
#include "editor/ai/ai_build_bridge.h"
#include "editor/ai/ai_code_fetcher.h"
#include "editor/ai/ai_develop_flow.h"
#include "editor/ai/ai_code_security_checker.h"
#include "editor/ai/ai_code_uploader.h"
#include "editor/ai/ai_feature_gate.h"
#include "editor/ai/ai_mcp_runtime.h"
#include "editor/ai/ai_modified_scene_tracker.h"
#include "editor/ai/ai_restart_helper.h"
#include "editor/ai/ai_settings.h"
#include "editor/ai/ai_source_update_service.h"
#include "editor/ai/ai_tool_defs.h"
#include "editor/ai/ai_tool_registry.h"
#include "editor/editor_node.h"
#include "editor/debugger/script_editor_debugger.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/run/editor_run_bar.h"
#include "editor/run/game_view_plugin.h"

// ---- Async build state ----
struct BuildState {
	enum Status { IDLE,
		RUNNING,
		DONE,
		FAILED };

	Status status = IDLE;
	String std_out;
	int exit_code = -1;
	String extra_args; // snapshot of args for the running build
	String executable_path; // editor binary produced by the successful build
	bool has_result = false;

	Mutex mutex;
	Thread thread;

	void start(const String &p_extra_args) {
		MutexLock lock(mutex);
		if (status == RUNNING) {
			return; // already running
		}
		status = RUNNING;
		has_result = false;
		std_out.clear();
		exit_code = -1;
		extra_args = p_extra_args;
		executable_path.clear();
	}

	void complete(const String &p_std_out, int p_exit_code, const String &p_executable_path = String()) {
		MutexLock lock(mutex);
		std_out = p_std_out;
		exit_code = p_exit_code;
		executable_path = p_executable_path;
		status = (p_exit_code == 0) ? DONE : FAILED;
		has_result = true;
	}

	Dictionary get_result() {
		MutexLock lock(mutex);
		Dictionary r;
		r["status"] = (status == RUNNING) ? "running" : (status == DONE ? "done" : (status == FAILED ? "failed" : "idle"));
		r["exit_code"] = exit_code;
		r["stdout"] = std_out;
		r["has_result"] = has_result;
		r["executable_path"] = executable_path;
		return r;
	}

	bool is_running() {
		MutexLock lock(mutex);
		return status == RUNNING;
	}

	// Must be called from the main thread after the build finishes,
	// to clean up the thread resource.
	void join_thread() {
		MutexLock lock(mutex);
		if (status == RUNNING || status == IDLE) {
			return; // thread is still running or never started, don't join
		}
		lock.temp_unlock();
		if (thread.is_started()) {
			thread.wait_to_finish();
		}
		lock.temp_relock();
	}
};

static BuildState build_state;

static String _find_sconstruct_root_from(String p_dir, int p_max_depth = 10) {
	for (int i = 0; i < p_max_depth && !p_dir.is_empty(); i++) {
		if (FileAccess::exists(p_dir.path_join("SConstruct"))) {
			return p_dir;
		}

		String parent = p_dir.get_base_dir();
		if (parent == p_dir) {
			break;
		}
		p_dir = parent;
	}

	return String();
}

static String _get_default_engine_source_cache_root() {
	String user_data_dir = OS::get_singleton()->get_user_data_dir();
	if (user_data_dir.is_empty()) {
		return String();
	}
	return user_data_dir.path_join("engine_source");
}

static String _get_configured_engine_source_cache_root(const AISettingsData &p_settings) {
	if (!p_settings.engine_source_cache_root.is_empty()) {
		return p_settings.engine_source_cache_root;
	}
	return _get_default_engine_source_cache_root();
}

static String _get_engine_build_root() {
	AISettingsData settings = AISettings::load();

	if (!settings.engine_source_root.is_empty() && FileAccess::exists(settings.engine_source_root.path_join("SConstruct"))) {
		return settings.engine_source_root;
	}

	String detected = AISettings::get_engine_source_root(settings);
	if (!detected.is_empty() && FileAccess::exists(detected.path_join("SConstruct"))) {
		return detected;
	}

	String exe_path = OS::get_singleton()->get_executable_path();
	if (!exe_path.is_empty()) {
		detected = _find_sconstruct_root_from(exe_path.get_base_dir());
		if (!detected.is_empty()) {
			return detected;
		}
	}

	String cache_root = _get_configured_engine_source_cache_root(settings);
	if (!cache_root.is_empty() && FileAccess::exists(cache_root.path_join("SConstruct"))) {
		return cache_root;
	}

	return _find_sconstruct_root_from(OS::get_singleton()->get_cwd());
}

static String _source_root_missing_message() {
	AISettingsData settings = AISettings::load();
	String cache_root = _get_configured_engine_source_cache_root(settings);
	String message = "No JunDot engine source checkout is configured. A packaged editor executable does not contain source code.";
	if (!cache_root.is_empty()) {
		message += " Clone/download the JunDot source into the default cache directory (" + cache_root + ") or set AI engine_source_root to another source checkout, then retry.";
	} else {
		message += " Clone/download the JunDot source into a separate directory, set AI engine_source_root to that directory, then retry.";
	}
	return message;
}

static String _find_latest_built_editor_executable(const String &p_project_root, uint64_t p_build_started_at, bool p_mono_enabled) {
	const String bin_dir = p_project_root.path_join("bin");
	Ref<DirAccess> dir = DirAccess::open(bin_dir);
	if (dir.is_null()) {
		return String();
	}

	String latest_path;
	uint64_t latest_mtime = 0;
	dir->list_dir_begin();
	String name = dir->get_next();
	while (!name.is_empty()) {
		const String lower_name = name.to_lower();
		const bool is_mono = lower_name.contains(".mono.");
		if (!dir->current_is_dir() && lower_name.contains(".windows.editor") &&
				lower_name.ends_with(".exe") && !lower_name.ends_with(".console.exe") &&
				is_mono == p_mono_enabled) {
			const String candidate = bin_dir.path_join(name);
			const uint64_t mtime = FileAccess::get_modified_time(candidate);
			// Ignore stale editor binaries left by earlier build configurations.
			// Allow a small tolerance for filesystems with coarse timestamp precision.
			if (mtime + 2 >= p_build_started_at && mtime >= latest_mtime) {
				latest_mtime = mtime;
				latest_path = candidate;
			}
		}
		name = dir->get_next();
	}
	dir->list_dir_end();

	if (p_mono_enabled && !latest_path.is_empty()) {
		const String assemblies_dir = bin_dir.path_join("JundotSharp").path_join("Api").path_join("Debug");
		if (!DirAccess::dir_exists_absolute(assemblies_dir)) {
			return String();
		}
	}
	return latest_path;
}

static Error _run_prebuild_git_command(const List<String> &p_args, String &r_output, int &r_exit_code) {
	r_output.clear();
	r_exit_code = -1;

	List<String> args;
	args.push_back("-c");
	args.push_back("http.sslBackend=openssl");
	AISettingsData settings = AISettings::load();
	bool has_token = false;
	if (!settings.github_token.access_token.is_empty()) {
		const String basic = "x-access-token:" + settings.github_token.access_token;
		const CharString basic_utf8 = basic.utf8();
		args.push_back("-c");
		args.push_back("http.https://github.com/.extraHeader=Authorization: Basic " + CryptoCore::b64_encode_str((const uint8_t *)basic_utf8.get_data(), basic_utf8.length()));
		has_token = true;
	}
	if (!settings.gitee_token.access_token.is_empty()) {
		args.push_back("-c");
		args.push_back(vformat("http.https://gitee.com/.extraHeader=Authorization: Bearer %s", settings.gitee_token.access_token));
		has_token = true;
	}
	if (has_token) {
		args.push_back("-c");
		args.push_back("credential.helper=");
	}
	for (const List<String>::Element *E = p_args.front(); E; E = E->next()) {
		args.push_back(E->get());
	}

	const Error err = OS::get_singleton()->execute("git", args, &r_output, &r_exit_code, true);
	if (err != OK) {
		return err;
	}
	return r_exit_code == 0 ? OK : FAILED;
}

static void _restore_prebuild_stash_after_failed_update(const String &p_stash_ref, String &r_log) {
	if (p_stash_ref.is_empty()) {
		return;
	}

	String output;
	int exit_code = -1;
	List<String> abort_args;
	abort_args.push_back("merge");
	abort_args.push_back("--abort");
	_run_prebuild_git_command(abort_args, output, exit_code);

	List<String> apply_args;
	apply_args.push_back("stash");
	apply_args.push_back("apply");
	apply_args.push_back("--index");
	apply_args.push_back("--whitespace=nowarn");
	apply_args.push_back(p_stash_ref);
	Error apply_err = _run_prebuild_git_command(apply_args, output, exit_code);
	if (apply_err != OK) {
		List<String> conflicts_args;
		conflicts_args.push_back("diff");
		conflicts_args.push_back("--name-only");
		conflicts_args.push_back("--diff-filter=U");
		String conflicts;
		int conflict_exit = -1;
		_run_prebuild_git_command(conflicts_args, conflicts, conflict_exit);
		if (!conflicts.strip_edges().is_empty()) {
			List<String> theirs_args;
			theirs_args.push_back("checkout");
			theirs_args.push_back("--theirs");
			theirs_args.push_back("--");
			theirs_args.push_back(".");
			if (_run_prebuild_git_command(theirs_args, output, exit_code) == OK) {
				List<String> add_args;
				add_args.push_back("add");
				add_args.push_back("-A");
				apply_err = _run_prebuild_git_command(add_args, output, exit_code);
			}
		} else {
			apply_err = OK;
		}
	}

	if (apply_err == OK) {
		List<String> drop_args;
		drop_args.push_back("stash");
		drop_args.push_back("drop");
		drop_args.push_back(p_stash_ref);
		_run_prebuild_git_command(drop_args, output, exit_code);
		r_log += "[Source Update] Restored local changes after the failed update attempt.\n";
	} else {
		r_log += "[Source Update] Automatic update failed; local changes remain recoverable in " + p_stash_ref + ".\n";
	}
}

static Error _auto_update_source_before_build(String &r_log) {
	String output;
	int exit_code = -1;

	List<String> inside_args;
	inside_args.push_back("rev-parse");
	inside_args.push_back("--is-inside-work-tree");
	if (_run_prebuild_git_command(inside_args, output, exit_code) != OK || output.strip_edges() != "true") {
		r_log += "[Source Update] Current engine source is not a Git worktree; skipping update check.\n";
		return OK;
	}

	List<String> upstream_args;
	upstream_args.push_back("rev-parse");
	upstream_args.push_back("--abbrev-ref");
	upstream_args.push_back("--symbolic-full-name");
	upstream_args.push_back("@{upstream}");
	if (_run_prebuild_git_command(upstream_args, output, exit_code) != OK || output.strip_edges().is_empty()) {
		r_log += "[Source Update] Current branch has no upstream; skipping automatic pull.\n";
		return OK;
	}
	const String upstream = output.strip_edges();

	List<String> fetch_args;
	fetch_args.push_back("fetch");
	fetch_args.push_back("--prune");
	if (_run_prebuild_git_command(fetch_args, output, exit_code) != OK) {
		r_log += "[Source Update] git fetch failed:\n" + output + "\n";
		return FAILED;
	}

	List<String> behind_args;
	behind_args.push_back("rev-list");
	behind_args.push_back("--count");
	behind_args.push_back("HEAD.." + upstream);
	if (_run_prebuild_git_command(behind_args, output, exit_code) != OK) {
		r_log += "[Source Update] Could not compare local and upstream revisions:\n" + output + "\n";
		return FAILED;
	}
	const int behind_count = output.strip_edges().to_int();
	if (behind_count <= 0) {
		r_log += "[Source Update] Source is up to date.\n";
		return OK;
	}
	r_log += vformat("[Source Update] Upstream has %d new commit(s); updating before build.\n", behind_count);

	List<String> status_args;
	status_args.push_back("status");
	status_args.push_back("--porcelain");
	if (_run_prebuild_git_command(status_args, output, exit_code) != OK) {
		r_log += "[Source Update] Could not inspect local changes:\n" + output + "\n";
		return FAILED;
	}
	const bool had_local_changes = !output.strip_edges().is_empty();
	String stash_ref;
	if (had_local_changes) {
		const String stash_message = "jundot-ai-prebuild-" + String::num_int64(OS::get_singleton()->get_ticks_usec());
		List<String> stash_args;
		stash_args.push_back("stash");
		stash_args.push_back("push");
		stash_args.push_back("--include-untracked");
		stash_args.push_back("--message");
		stash_args.push_back(stash_message);
		if (_run_prebuild_git_command(stash_args, output, exit_code) != OK) {
			r_log += "[Source Update] Failed to preserve local changes before update:\n" + output + "\n";
			return FAILED;
		}
		stash_ref = "stash@{0}";
		r_log += "[Source Update] Preserved local tracked and untracked changes in " + stash_ref + ".\n";
	}

	List<String> merge_args;
	merge_args.push_back("merge");
	merge_args.push_back("--no-edit");
	merge_args.push_back("-X");
	merge_args.push_back("ours");
	merge_args.push_back(upstream);
	Error merge_err = _run_prebuild_git_command(merge_args, output, exit_code);
	if (merge_err != OK) {
		List<String> conflicts_args;
		conflicts_args.push_back("diff");
		conflicts_args.push_back("--name-only");
		conflicts_args.push_back("--diff-filter=U");
		String conflict_output;
		int conflict_exit = -1;
		_run_prebuild_git_command(conflicts_args, conflict_output, conflict_exit);
		if (conflict_output.strip_edges().is_empty()) {
			List<String> abort_args;
			abort_args.push_back("merge");
			abort_args.push_back("--abort");
			String ignored;
			int ignored_exit = -1;
			_run_prebuild_git_command(abort_args, ignored, ignored_exit);
			r_log += "[Source Update] Upstream merge failed without resolvable file conflicts:\n" + output + "\n";
			_restore_prebuild_stash_after_failed_update(stash_ref, r_log);
			return FAILED;
		}

		List<String> ours_args;
		ours_args.push_back("checkout");
		ours_args.push_back("--ours");
		ours_args.push_back("--");
		ours_args.push_back(".");
		String resolve_output;
		int resolve_exit = -1;
		if (_run_prebuild_git_command(ours_args, resolve_output, resolve_exit) != OK) {
			r_log += "[Source Update] Failed to resolve upstream conflicts with local branch versions:\n" + resolve_output + "\n";
			_restore_prebuild_stash_after_failed_update(stash_ref, r_log);
			return FAILED;
		}
		List<String> add_args;
		add_args.push_back("add");
		add_args.push_back("-A");
		if (_run_prebuild_git_command(add_args, resolve_output, resolve_exit) != OK) {
			r_log += "[Source Update] Failed to stage automatically resolved upstream conflicts:\n" + resolve_output + "\n";
			_restore_prebuild_stash_after_failed_update(stash_ref, r_log);
			return FAILED;
		}
		List<String> commit_args;
		commit_args.push_back("-c");
		commit_args.push_back("user.name=Jundot AI");
		commit_args.push_back("-c");
		commit_args.push_back("user.email=jundot-ai@local");
		commit_args.push_back("commit");
		commit_args.push_back("--no-edit");
		if (_run_prebuild_git_command(commit_args, resolve_output, resolve_exit) != OK) {
			r_log += "[Source Update] Failed to finalize automatically resolved upstream merge:\n" + resolve_output + "\n";
			_restore_prebuild_stash_after_failed_update(stash_ref, r_log);
			return FAILED;
		}
		r_log += "[Source Update] Resolved upstream conflicts by preserving current local branch versions.\n";
	}

	if (!stash_ref.is_empty()) {
		List<String> apply_args;
		apply_args.push_back("stash");
		apply_args.push_back("apply");
		apply_args.push_back("--index");
		apply_args.push_back("--whitespace=nowarn");
		apply_args.push_back(stash_ref);
		Error apply_err = _run_prebuild_git_command(apply_args, output, exit_code);
		if (apply_err != OK) {
			List<String> conflicts_args;
			conflicts_args.push_back("diff");
			conflicts_args.push_back("--name-only");
			conflicts_args.push_back("--diff-filter=U");
			String conflict_output;
			int conflict_exit = -1;
			_run_prebuild_git_command(conflicts_args, conflict_output, conflict_exit);
			if (conflict_output.strip_edges().is_empty()) {
				apply_err = OK;
			} else {
				List<String> theirs_args;
				theirs_args.push_back("checkout");
				theirs_args.push_back("--theirs");
				theirs_args.push_back("--");
				theirs_args.push_back(".");
				if (_run_prebuild_git_command(theirs_args, output, exit_code) != OK) {
					r_log += "[Source Update] Failed to restore local working changes during conflict resolution:\n" + output + "\n";
					return FAILED;
				}
				List<String> add_args;
				add_args.push_back("add");
				add_args.push_back("-A");
				if (_run_prebuild_git_command(add_args, output, exit_code) != OK) {
					r_log += "[Source Update] Failed to stage restored local working changes:\n" + output + "\n";
					return FAILED;
				}
				apply_err = OK;
				r_log += "[Source Update] Resolved restore conflicts by preserving the pre-build local working versions.\n";
			}
		}

		List<String> drop_args;
		drop_args.push_back("stash");
		drop_args.push_back("drop");
		drop_args.push_back(stash_ref);
		if (_run_prebuild_git_command(drop_args, output, exit_code) != OK) {
			r_log += "[Source Update] Warning: restored local changes, but could not remove " + stash_ref + ".\n";
		}
	}

	List<String> unresolved_args;
	unresolved_args.push_back("diff");
	unresolved_args.push_back("--name-only");
	unresolved_args.push_back("--diff-filter=U");
	if (_run_prebuild_git_command(unresolved_args, output, exit_code) != OK || !output.strip_edges().is_empty()) {
		r_log += "[Source Update] Unresolved conflicts remain; build cancelled.\n" + output + "\n";
		return FAILED;
	}

	r_log += "[Source Update] Source update completed successfully.\n";
	return OK;
}

static void _build_thread_callback(void *p_userdata) {
	String extra_args = String();
	if (p_userdata) {
		extra_args = *(static_cast<String *>(p_userdata));
		delete static_cast<String *>(p_userdata);
	}

	String project_root = _get_engine_build_root();
	if (project_root.is_empty()) {
		build_state.complete("Failed to start build: no JunDot engine source root with SConstruct was found. A packaged editor executable does not contain the engine source. Clone/download the JunDot source into a separate directory, set AI engine_source_root to that directory, then retry run_build.", -1);
		return;
	}

	String previous_cwd = OS::get_singleton()->get_cwd();
	Error cwd_err = OS::get_singleton()->set_cwd(project_root);
	if (cwd_err != OK) {
		build_state.complete("Failed to start build: could not switch to project root: " + project_root, -1);
		return;
	}

	String source_update_log;
	if (_auto_update_source_before_build(source_update_log) != OK) {
		OS::get_singleton()->set_cwd(previous_cwd);
		build_state.complete(source_update_log + "\nBuild cancelled because the engine source update did not complete safely.", -1);
		return;
	}

	// Build scons arguments.
	List<String> scons_args;
	scons_args.push_back("platform=windows");
	scons_args.push_back("target=editor");
	scons_args.push_back("arch=x86_64");
	scons_args.push_back("debug_symbols=no");
	// AI builds default to the native editor. A Mono editor also requires a
	// separate managed assemblies build, so never select it accidentally.
	scons_args.push_back("module_mono_enabled=no");
	scons_args.push_back("-j4");
	scons_args.push_back("d3d12=no");
	scons_args.push_back("accesskit=no");
	scons_args.push_back("angle=no");

	if (!extra_args.is_empty()) {
		Vector<String> extras = extra_args.split(" ", false);
		for (int i = 0; i < extras.size(); i++) {
			if (extras[i].begins_with("module_mono_enabled=")) {
				continue;
			}
			scons_args.push_back(extras[i]);
		}
	}

	const bool mono_enabled = false;
	const uint64_t build_started_at = OS::get_singleton()->get_unix_time();

	// Try python -m SCons first, then scons, then python3 -m SCons.
	String std_out;
	int exit_code = -1;

	// First try: python -m SCons
	List<String> py_args;
	py_args.push_back("-m");
	py_args.push_back("SCons");
	for (const String &arg : scons_args) {
		py_args.push_back(arg);
	}
	Error err = OS::get_singleton()->execute("python", py_args, &std_out, &exit_code, true);

	if (err != OK) {
		// Second try: scons directly
		err = OS::get_singleton()->execute("scons", scons_args, &std_out, &exit_code, true);
		if (err != OK) {
			// Third try: python3 -m SCons
			err = OS::get_singleton()->execute("python3", py_args, &std_out, &exit_code, true);
			if (err != OK) {
				std_out = "Failed to start build: neither 'python -m SCons', 'scons', nor 'python3 -m SCons' was found.";
				exit_code = -1;
			}
		}
	}

	OS::get_singleton()->set_cwd(previous_cwd);
	std_out = source_update_log + "\n" + std_out;
	const String built_executable = exit_code == 0 ? _find_latest_built_editor_executable(project_root, build_started_at, mono_enabled) : String();
	if (exit_code == 0 && built_executable.is_empty()) {
		std_out += mono_enabled ? "\n\nBuild succeeded, but the Mono editor or its JundotSharp/Api/Debug assemblies were not generated by this build." : "\n\nBuild succeeded, but no native non-console Windows editor executable generated by this build was found in: " + project_root.path_join("bin");
	}
	build_state.complete(std_out, exit_code, built_executable);
}

Dictionary AIToolExecutor::execute(const Dictionary &p_tool_call) {
	const String tool_call_id = p_tool_call.get("id", String());
	const Dictionary fn_def = p_tool_call.get("function", Dictionary());
	String name = fn_def.get("name", String());
	const String args_json = fn_def.get("arguments", "{}");

	if (name == "read_file") {
		name = AIToolNames::READ_FILES;
	} else if (name == "glob" || name == "glob_search") {
		name = AIToolNames::SEARCH_FILES;
	}

	Dictionary args;
	Variant parsed = JSON::parse_string(args_json);
	if (parsed.get_type() == Variant::DICTIONARY) {
		args = parsed;
	}
	if (name == "memory_search") {
		name = AIToolNames::READ_FILES;
		Array paths;
		paths.push_back(".JundotAI/memory.json");
		args["paths"] = paths;
	} else if (name == "session_list") {
		name = AIToolNames::READ_FILES;
		Array paths;
		paths.push_back(".JundotAI/conversations.json");
		args["paths"] = paths;
	}

	if (name.find_char('.') < 0) {
		const Vector<String> disabled_tools = AISettings::load().disabled_builtin_tools;
		for (const String &disabled_tool : disabled_tools) {
			if (disabled_tool == name) {
				Dictionary disabled_result = _make_result(vformat("Tool `%s` is disabled in AI tool settings. Re-enable it in AI Configuration > Built-in Tools before using it.", name), true);
				disabled_result["role"] = "tool";
				disabled_result["tool_call_id"] = tool_call_id;
				return disabled_result;
			}
		}
	}

	Dictionary result;
	if (name == AIToolNames::READ_FILES) {
		result = _read_files(args);
	} else if (name == AIToolNames::WRITE_FILE) {
		result = _write_file(args);
	} else if (name == AIToolNames::EDIT_FILE) {
		result = _edit_file(args);
	} else if (name == AIToolNames::SEARCH_FILES) {
		result = _search_files(args);
	} else if (name == AIToolNames::LIST_FILES) {
		result = _list_files(args);
	} else if (name == AIToolNames::GREP_CODE) {
		result = _grep_code(args);
	} else if (name == AIToolNames::CHECK_PROJECT_SCRIPTS) {
		result = _check_project_scripts(args);
	} else if (name == AIToolNames::CHECK_HTML_PROTOTYPE) {
		result = _check_html_prototype(args);
	} else if (name == AIToolNames::CHECK_UI_LAYOUT) {
		result = _check_ui_layout(args);
	} else if (name == AIToolNames::CREATE_3D_SCENE) {
		result = _create_3d_scene(args);
	} else if (name == AIToolNames::ADD_3D_OBJECT) {
		result = _add_3d_object(args);
	} else if (name == AIToolNames::ADD_3D_LIGHT) {
		result = _add_3d_light(args);
	} else if (name == AIToolNames::CHECK_3D_SCENE) {
		result = _check_3d_scene(args);
	} else if (name == AIToolNames::BUILD_PROJECT) {
		result = _build_project(args);
	} else if (name == AIToolNames::BUILD_CPP_HOT_MODULE) {
		result = _build_cpp_hot_module(args);
	} else if (name == AIToolNames::RELOAD_CPP_HOT_MODULE) {
		result = _reload_cpp_hot_module(args);
	} else if (name == AIToolNames::PACKAGE_PROJECT) {
		result = _package_project(args);
	} else if (name == AIToolNames::CHECK_PACKAGE_STATUS) {
		result = _check_package_status(args);
	} else if (name == AIToolNames::TEST_PACKAGE) {
		result = _test_package(args);
	} else if (name == AIToolNames::CAPTURE_PACKAGE_SCREENSHOT) {
		result = _capture_package_screenshot(args);
	} else if (name == AIToolNames::PLAY_SCENE) {
		result = _play_scene(args);
	} else if (name == AIToolNames::CLICK_UI_POSITION) {
		result = _click_ui_position(args);
	} else if (name == AIToolNames::CLICK_UI_NODE) {
		result = _click_ui_node(args);
	} else if (name == AIToolNames::ASSERT_NODE_VISIBLE) {
		result = _assert_node_visible(args);
	} else if (name == AIToolNames::ASSERT_NO_RUNTIME_ERRORS) {
		result = _assert_no_runtime_errors(args);
	} else if (name == AIToolNames::CAPTURE_GAME_SCREENSHOT) {
		result = _capture_game_screenshot(args);
	} else if (name == AIToolNames::CAPTURE_RUNTIME_UI_SNAPSHOT) {
		result = _capture_runtime_ui_snapshot(args);
	} else if (name == AIToolNames::STOP_PLAY_SCENE) {
		result = _stop_play_scene(args);
	} else if (name == AIToolNames::RUN_BUILD) {
		result = _run_build(args);
	} else if (name == AIToolNames::CHECK_BUILD_STATUS) {
		result = _check_build_status(args);
	} else if (name == AIToolNames::READ_BUILD_LOG) {
		result = _read_build_log(args);
	} else if (name == AIToolNames::FETCH_URL) {
		result = _fetch_url(args);
	} else if (name == AIToolNames::SHELL_COMMAND) {
		result = _shell_command(args);
	} else if (name == AIToolNames::UPLOAD_CODE) {
		result = _upload_code(args);
	} else if (name == AIToolNames::DEVELOP_AI_VERIFY) {
		result = _develop_ai_verify(args);
	} else if (name == AIToolNames::RESTART_ENGINE) {
		result = _restart_engine(args);
	} else if (name == AIToolNames::SETUP_ENGINE_WORKSPACE) {
		result = _setup_engine_workspace(args);
	} else if (name == AIToolNames::REQUEST_ENGINE_CHANGE) {
		result = _request_engine_change(args);
	} else if (name == AIToolNames::RETURN_TO_PROJECT_MODE) {
		result = _return_to_project_mode(args);
	} else if (name == AIToolNames::ADD_PHYSICS) {
		result = _add_physics(args);
	} else if (name == AIToolNames::ADD_ANIMATION) {
		result = _add_animation(args);
	} else if (name == AIToolNames::ADD_PARTICLES) {
		result = _add_particles(args);
	} else if (name == AIToolNames::ADD_VFX) {
		result = _add_vfx(args);
	} else if (name == AIToolNames::ADD_CHARACTER_CONTROLLER) {
		result = _add_character_controller(args);
	} else if (name == AIToolNames::REMOVE_NODE) {
		result = _remove_node(args);
	} else if (name == AIToolNames::MODIFY_NODE_PROPERTIES) {
		result = _modify_node_properties(args);
	} else if (name == AIToolNames::CONNECT_SIGNAL) {
		result = _connect_signal(args);
	} else if (name == AIToolNames::DUPLICATE_NODE) {
		result = _duplicate_node(args);
	} else if (name == AIToolNames::REPARENT_NODE) {
		result = _reparent_node(args);
	} else if (name == AIToolNames::BATCH_TOOLS) {
		result = _batch_tools(args);
	} else if (name.find_char('.') >= 0) {
		// Tool names with a dot separator indicate MCP tools (e.g. "server_name.tool_name").
		int dot = name.find_char('.');
		String server_name = name.substr(0, dot);
		String tool_name = name.substr(dot + 1);
		result = _execute_mcp_tool(server_name, tool_name, args_json);
	} else {
		result = _make_result(vformat("Unknown tool: %s", name), true);
	}

	result["role"] = "tool";
	result["tool_call_id"] = tool_call_id;
	return result;
}

Dictionary AIToolExecutor::_make_result(const String &p_content, bool p_is_error) {
	Dictionary d;
	d["content"] = p_content;
	if (p_is_error) {
		d["is_error"] = true;
	}
	return d;
}

String AIToolExecutor::_get_project_root() {
	// In ENGINE mode, return the engine source directory from settings
	// (auto-detected via AISettings::get_engine_source_root).
	// In PROJECT mode, return the currently-opened project directory.
	AISettingsData s = AISettings::load();
	if (s.context_mode == AIContextMode::ENGINE) {
		String engine_root = _get_engine_build_root();
		if (!engine_root.is_empty()) {
			return engine_root;
		}
		return String();
	}

	if (ProjectSettings::get_singleton()) {
		String project_root = ProjectSettings::get_singleton()->get_resource_path();
		if (!project_root.is_empty() && (FileAccess::exists(project_root.path_join("project.jundot")) || FileAccess::exists(project_root.path_join("project.godot")))) {
			return project_root;
		}
	}

	// Project mode may only operate on a verified game-project root. Falling
	// back to the process cwd can accidentally expose an engine checkout when
	// the editor was launched from its source directory.
	String exe_path = OS::get_singleton()->get_executable_path();
	if (!exe_path.is_empty() && (FileAccess::exists(exe_path.get_base_dir().path_join("project.jundot")) || FileAccess::exists(exe_path.get_base_dir().path_join("project.godot")))) {
		return exe_path.get_base_dir();
	}

	return String();
}

static Error _ensure_engine_source_updated_before_mutation(String &r_message) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::ENGINE) {
		return OK;
	}
	return AISourceUpdateService::ensure_updated_before_edit(r_message);
}

static bool _resolve_tool_file_path(const String &p_root, const String &p_input, String &r_full_path, String &r_error) {
	String input = p_input.strip_edges().replace("\\", "/");
	if (input.is_empty()) {
		r_error = "Path is empty.";
		return false;
	}

	const AISettingsData settings = AISettings::load();
	if (settings.context_mode == AIContextMode::ENGINE) {
		r_full_path = input.is_absolute_path() ? input.simplify_path() : p_root.path_join(input).simplify_path();
		return true;
	}

	input = input.trim_prefix("res://").simplify_path();
	if (input.is_absolute_path() || input == ".." || input.begins_with("../") || input.contains("/../")) {
		r_error = "PROJECT mode path rejected: the path must stay inside the open project root.";
		return false;
	}

	const String normalized_root = p_root.replace("\\", "/").simplify_path().trim_suffix("/");
	const String candidate = normalized_root.path_join(input).simplify_path();
	const String root_lower = normalized_root.to_lower();
	const String candidate_lower = candidate.to_lower();
	if (candidate_lower != root_lower && !candidate_lower.begins_with(root_lower + "/")) {
		r_error = "PROJECT mode path rejected: the resolved path escapes the open project root.";
		return false;
	}
	r_full_path = candidate;
	return true;
}

static bool _project_shell_command_stays_in_root(const String &p_command) {
	const String command = p_command.to_lower().replace("\\", "/");
	if (command.contains("..") || command.contains("pushd ") || command.contains("popd") || command.contains("cd /") || command.contains("sconstruct") || command.contains("editor/ai/")) {
		return false;
	}
	for (int i = 0; i + 2 < command.length(); i++) {
		const char32_t c = command[i];
		if (((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) && command[i + 1] == ':' && command[i + 2] == '/') {
			return false;
		}
	}
	return true;
}

static Error _run_command_in_root(const String &p_root, const String &p_program, const List<String> &p_args, String &r_output, int &r_exit_code);
static String _safe_ai_screenshot_file_name(const String &p_name);

static String _ai_safe_git_segment(const String &p_value, const String &p_fallback) {
	String value = p_value.strip_edges().to_lower().replace("\\", "-").replace("/", "-");
	String out;
	for (int i = 0; i < value.length(); i++) {
		const char32_t c = value[i];
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
			out += String::chr(c);
		} else if (c == ' ' || c == '\t') {
			out += "-";
		}
	}
	out = out.strip_edges().trim_prefix(".").trim_suffix(".");
	while (out.contains("--")) {
		out = out.replace("--", "-");
	}
	return out.is_empty() ? p_fallback : out;
}

static Error _run_git_in_root(const String &p_root, const Vector<String> &p_args, String &r_output, int &r_exit_code) {
	List<String> args;
	for (const String &arg : p_args) {
		args.push_back(arg);
	}
	return _run_command_in_root(p_root, "git", args, r_output, r_exit_code);
}

static bool _git_branch_exists(const String &p_root, const String &p_branch) {
	String output;
	int exit_code = -1;
	_run_git_in_root(p_root, { "rev-parse", "--verify", "--quiet", p_branch }, output, exit_code);
	return exit_code == 0;
}

static String _get_project_workspace_metadata_path(const String &p_project_root) {
	return p_project_root.path_join(".JundotAI").path_join("engine_workspace.json");
}

static Error _write_project_engine_workspace_metadata(const String &p_project_root, const Dictionary &p_metadata, String &r_error) {
	const String dir = p_project_root.path_join(".JundotAI");
	Error err = DirAccess::make_dir_recursive_absolute(dir);
	if (err != OK) {
		r_error = "Could not create .JundotAI metadata directory.";
		return err;
	}

	const String metadata_path = _get_project_workspace_metadata_path(p_project_root);
	Ref<FileAccess> file = FileAccess::open(metadata_path, FileAccess::WRITE, &err);
	if (err != OK || file.is_null()) {
		r_error = "Could not write engine workspace metadata: " + metadata_path;
		return err == OK ? ERR_CANT_OPEN : err;
	}
	file->store_string(JSON::stringify(p_metadata, "\t"));
	return OK;
}

Dictionary AIToolExecutor::_setup_engine_workspace(const Dictionary &p_args) {
	const AISettingsData current_settings = AISettings::load();
	if (current_settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("setup_engine_workspace is only available in PROJECT mode.", true);
	}

	const String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result("Project root not detected. Open a project with project.jundot before creating an engine workspace.", true);
	}

	const String base_engine_root = _get_engine_build_root();
	if (base_engine_root.is_empty() || !FileAccess::exists(base_engine_root.path_join("SConstruct"))) {
		return _make_result(_source_root_missing_message(), true);
	}

	const String project_name = _ai_safe_git_segment(project_root.get_file(), "jundot-project");
	const String workspace_name = _ai_safe_git_segment(String(p_args.get("workspace_name", String())), project_name);
	const String provider_raw = String(p_args.get("provider", "local")).strip_edges().to_lower();
	const String provider = (provider_raw == "github" || provider_raw == "gitee") ? provider_raw : "local";
	const String remote_url = String(p_args.get("remote_url", String())).strip_edges();
	const String branch_arg = String(p_args.get("branch", String())).strip_edges();
	const String branch = branch_arg.is_empty() ? "project/" + workspace_name : branch_arg;
	const String base_ref_arg = String(p_args.get("base_ref", "HEAD")).strip_edges();
	const String base_ref = base_ref_arg.is_empty() ? "HEAD" : base_ref_arg;

	const String user_data_dir = OS::get_singleton()->get_user_data_dir();
	if (user_data_dir.is_empty()) {
		return _make_result("Could not resolve the editor user data directory for engine workspaces.", true);
	}
	const String workspace_parent = user_data_dir.path_join("project_engine_workspaces");
	const String workspace_path = workspace_parent.path_join(workspace_name).simplify_path();

	Error err = DirAccess::make_dir_recursive_absolute(workspace_parent);
	if (err != OK) {
		return _make_result("Could not create engine workspace parent directory: " + workspace_parent, true);
	}

	String output;
	int exit_code = -1;
	bool created_workspace = false;
	if (!FileAccess::exists(workspace_path.path_join("SConstruct"))) {
		Vector<String> worktree_args;
		worktree_args.push_back("worktree");
		worktree_args.push_back("add");
		if (_git_branch_exists(base_engine_root, branch)) {
			worktree_args.push_back(workspace_path);
			worktree_args.push_back(branch);
		} else {
			worktree_args.push_back("-b");
			worktree_args.push_back(branch);
			worktree_args.push_back(workspace_path);
			worktree_args.push_back(base_ref);
		}
		err = _run_git_in_root(base_engine_root, worktree_args, output, exit_code);
		if (err != OK || exit_code != 0) {
			return _make_result("Failed to create project engine worktree.\nCommand: git worktree add\nOutput:\n" + output, true);
		}
		created_workspace = true;
	}

	String remote_message;
	if (!remote_url.is_empty()) {
		_run_git_in_root(workspace_path, { "remote", "get-url", "project-engine" }, output, exit_code);
		if (exit_code == 0) {
			err = _run_git_in_root(workspace_path, { "remote", "set-url", "project-engine", remote_url }, output, exit_code);
		} else {
			err = _run_git_in_root(workspace_path, { "remote", "add", "project-engine", remote_url }, output, exit_code);
		}
		if (err != OK || exit_code != 0) {
			return _make_result("Engine workspace was prepared, but the project-engine remote could not be configured.\nOutput:\n" + output, true);
		}
		remote_message = "Remote project-engine configured: " + remote_url;
	} else if (provider != "local") {
		remote_message = "Provider recorded as " + provider + ", but no remote_url was provided. The user can create/login to the remote provider and rerun setup_engine_workspace with remote_url.";
	}

	AISettingsData updated_settings = current_settings;
	updated_settings.engine_source_root = workspace_path;
	AISettings::save(updated_settings);

	Dictionary metadata;
	metadata["provider"] = provider;
	metadata["remote_url"] = remote_url;
	metadata["remote_name"] = remote_url.is_empty() ? String() : String("project-engine");
	metadata["branch"] = branch;
	metadata["local_path"] = workspace_path;
	metadata["base_engine_root"] = base_engine_root;
	metadata["base_ref"] = base_ref;
	metadata["workspace_name"] = workspace_name;
	metadata["created_or_updated_at"] = Time::get_singleton()->get_datetime_string_from_system(true);
	metadata["schema_version"] = 1;

	String metadata_error;
	if (_write_project_engine_workspace_metadata(project_root, metadata, metadata_error) != OK) {
		return _make_result("Engine workspace was prepared, but project metadata could not be saved. " + metadata_error, true);
	}

	String result = "ENGINE_WORKSPACE_READY\n";
	result += "Workspace: " + workspace_path + "\n";
	result += "Branch: " + branch + "\n";
	result += "Provider: " + provider + "\n";
	result += "Project metadata: " + _get_project_workspace_metadata_path(project_root) + "\n";
	result += created_workspace ? "Created a new git worktree for this project.\n" : "Reused the existing project engine worktree.\n";
	if (!remote_message.is_empty()) {
		result += remote_message + "\n";
	}
	result += "Engine mode is now configured to use this project-specific workspace. If the project requires engine changes, call request_engine_change next.";
	return _make_result(result);
}


Dictionary AIToolExecutor::_request_engine_change(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("request_engine_change is only needed in PROJECT mode; the assistant is already in ENGINE mode.");
	}

	const String reason = String(p_args.get("reason", String())).strip_edges();
	const String required_change = String(p_args.get("required_change", String())).strip_edges();
	const String project_work_done = String(p_args.get("project_work_done", String())).strip_edges();
	if (reason.is_empty() || required_change.is_empty()) {
		return _make_result("request_engine_change rejected: reason and required_change are required.", true);
	}

	String result = "ENGINE_MODE_REQUEST_ACCEPTED\n";
	result += "Reason: " + reason + "\n";
	result += "Required engine change: " + required_change + "\n";
	if (!project_work_done.is_empty()) {
		result += "Project-side work already done: " + project_work_done + "\n";
	}
	result += "The editor should switch to ENGINE mode, continue the same task with engine tools, build and verify engine changes when needed, then call return_to_project_mode.";
	return _make_result(result);
}

Dictionary AIToolExecutor::_return_to_project_mode(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::ENGINE) {
		return _make_result("return_to_project_mode is only needed in ENGINE mode; the assistant is already in PROJECT mode.");
	}

	const String summary = String(p_args.get("summary", String())).strip_edges();
	if (summary.is_empty()) {
		return _make_result("return_to_project_mode rejected: summary is required.", true);
	}

	String result = "PROJECT_MODE_RETURN_ACCEPTED\n";
	result += "Engine work summary: " + summary + "\n";
	result += "The editor should switch back to PROJECT mode and continue the original project task with project-scoped tools.";
	return _make_result(result);
}
static Error _run_command_in_root(const String &p_root, const String &p_program, const List<String> &p_args, String &r_output, int &r_exit_code) {
	const String previous_cwd = OS::get_singleton()->get_cwd();
	if (OS::get_singleton()->set_cwd(p_root) != OK) {
		r_output = "Could not switch command working directory to: " + p_root;
		r_exit_code = -1;
		return ERR_CANT_OPEN;
	}

	r_output.clear();
	r_exit_code = -1;
	const Error err = OS::get_singleton()->execute(p_program, p_args, &r_output, &r_exit_code, true);
	OS::get_singleton()->set_cwd(previous_cwd);
	return err;
}

static bool _is_script_check_path_rejected(const String &p_path) {
	const String normalized = p_path.replace("\\", "/").trim_prefix("res://").simplify_path();
	return normalized.is_absolute_path() || normalized == ".." || normalized.begins_with("../") || normalized.contains("/../");
}

static void _collect_gdscript_files(const String &p_root, const String &p_rel_dir, Vector<String> &r_paths, int p_limit) {
	if (r_paths.size() >= p_limit) {
		return;
	}

	const String abs_dir = p_rel_dir.is_empty() ? p_root : p_root.path_join(p_rel_dir);
	Ref<DirAccess> da = DirAccess::open(abs_dir);
	if (da.is_null()) {
		return;
	}

	da->list_dir_begin();
	String name = da->get_next();
	while (!name.is_empty() && r_paths.size() < p_limit) {
		if (name == "." || name == "..") {
			name = da->get_next();
			continue;
		}

		const String rel = p_rel_dir.is_empty() ? name : p_rel_dir.path_join(name);
		if (da->current_is_dir()) {
			if (!name.begins_with(".") && name != "bin" && name != "obj" && name != ".godot" && name != ".jundot" && name != ".import" && name != "__pycache__") {
				_collect_gdscript_files(p_root, rel, r_paths, p_limit);
			}
		} else if (name.to_lower().ends_with(".gd")) {
			r_paths.push_back(rel);
		}

		name = da->get_next();
	}
	da->list_dir_end();
}

static bool _project_has_dotnet_project(const String &p_root) {
	Ref<DirAccess> da = DirAccess::open(p_root);
	if (da.is_null()) {
		return false;
	}

	da->list_dir_begin();
	String name = da->get_next();
	while (!name.is_empty()) {
		const String lower = name.to_lower();
		if (!da->current_is_dir() && (lower.ends_with(".sln") || lower.ends_with(".csproj"))) {
			da->list_dir_end();
			return true;
		}
		name = da->get_next();
	}
	da->list_dir_end();
	return false;
}

static String _find_single_root_dotnet_project(const String &p_root, bool &r_ambiguous) {
	r_ambiguous = false;
	Ref<DirAccess> da = DirAccess::open(p_root);
	if (da.is_null()) {
		return String();
	}

	String found;
	da->list_dir_begin();
	String name = da->get_next();
	while (!name.is_empty()) {
		const String lower = name.to_lower();
		if (!da->current_is_dir() && (lower.ends_with(".sln") || lower.ends_with(".csproj"))) {
			if (!found.is_empty()) {
				r_ambiguous = true;
				da->list_dir_end();
				return String();
			}
			found = name;
		}
		name = da->get_next();
	}
	da->list_dir_end();
	return found;
}

static String _truncate_tool_output(const String &p_output, int p_max_chars) {
	if (p_output.length() <= p_max_chars) {
		return p_output;
	}
	return p_output.substr(p_output.length() - p_max_chars) + vformat("\n\n[... output truncated to last %d chars]\n", p_max_chars);
}

static bool _save_open_scene_before_ai_write(const String &p_full_path, String &r_message) {
	const String lower_path = p_full_path.to_lower();
	if (!lower_path.ends_with(".tscn") && !lower_path.ends_with(".scn")) {
		return false;
	}
	if (!EditorNode::get_singleton() || !ProjectSettings::get_singleton()) {
		return false;
	}

	const String scene_path = ProjectSettings::get_singleton()->localize_path(p_full_path);
	EditorData &editor_data = EditorNode::get_editor_data();
	const int scene_idx = editor_data.get_edited_scene_from_path(scene_path);
	if (scene_idx < 0) {
		return false;
	}

	const int history_id = editor_data.get_scene_history_id(scene_idx);
	if (!EditorUndoRedoManager::get_singleton()->is_history_unsaved(history_id)) {
		return false;
	}

	EditorNode::get_singleton()->save_scene_if_open(scene_path);
	if (EditorUndoRedoManager::get_singleton()->is_history_unsaved(history_id)) {
		r_message = "AI scene write blocked: the scene is open in the editor and has unsaved changes that could not be saved automatically. Save the scene manually, then ask the AI to read the latest .tscn and try again.\nScene: " + scene_path;
	} else {
		r_message = "AI scene write paused: the open scene had unsaved editor changes, so Jundot saved them first. The AI must read the latest scene file again before editing, otherwise it may overwrite the user's just-saved changes.\nScene: " + scene_path;
	}
	return true;
}

// ---- Tool implementations ----

Dictionary AIToolExecutor::_read_files(const Dictionary &p_args) {
	Array paths = p_args.get("paths", Array());
	if (paths.is_empty() && p_args.has("path")) {
		paths.push_back(String(p_args.get("path", String())));
	}
	if (paths.is_empty() && p_args.has("paths") && p_args["paths"].get_type() == Variant::STRING) {
		paths.push_back(String(p_args["paths"]));
	}
	if (paths.is_empty()) {
		return _make_result("No file paths provided.", true);
	}

	String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result(_source_root_missing_message(), true);
	}
	StringBuilder result;

	for (int i = 0; i < paths.size(); i++) {
		String rel_path = paths[i];
		String full_path;
		String path_error;
		if (!_resolve_tool_file_path(project_root, rel_path, full_path, path_error)) {
			result += vformat("[%s] Error: %s\n", rel_path, path_error);
			continue;
		}

		if (!FileAccess::exists(full_path)) {
			result += vformat("[%s] Error: file not found at %s\n", rel_path, full_path);
			continue;
		}

		Error err = OK;
		String content = FileAccess::get_file_as_string(full_path, &err);
		if (err != OK) {
			result += vformat("[%s] Error: failed to read file (err=%d)\n", rel_path, (int)err);
			continue;
		}

		// Truncate very large files to avoid blowing the API context.
		const int MAX_FILE_CHARS = 50000;
		if (content.length() > MAX_FILE_CHARS) {
			content = content.substr(0, MAX_FILE_CHARS) + vformat("\n\n[... truncated at %d characters]\n", MAX_FILE_CHARS);
		}

		result += vformat("=== %s ===\n%s\n", rel_path, content);
	}

	return _make_result(result.as_string());
}

Dictionary AIToolExecutor::_write_file(const Dictionary &p_args) {
	String path = p_args.get("path", String());
	String content = p_args.get("content", String());
	if (content.is_empty() && p_args.has("new_string")) {
		// Compatibility with models that emit write_file(path, new_string)
		// using edit-style argument naming in text-form tool calls.
		content = p_args.get("new_string", String());
	}

	if (path.is_empty()) {
		return _make_result("No file path provided.", true);
	}
	if (content.is_empty()) {
		return _make_result(vformat("File write rejected: content is empty, so the existing file was not modified.\nPath: %s", path), true);
	}

	String source_update_message;
	if (_ensure_engine_source_updated_before_mutation(source_update_message) != OK) {
		return _make_result("Engine source update check failed before modification: " + source_update_message, true);
	}

	// Validate content: reject Markdown code fences, which usually mean the
	// model returned a formatted snippet instead of raw file contents.
	if (content.contains("```")) {
		return _make_result(vformat("File write rejected: content contains Markdown code fences (```). Please call write_file again with raw file content only.\nPath: %s", path), true);
	}

	// Reject obviously truncated code (ends mid-statement).
	String trimmed = content.strip_edges();
	if (trimmed.ends_with("...") || trimmed.ends_with(",") || trimmed.ends_with("(") || trimmed.ends_with("=")) {
		return _make_result(vformat("File write rejected: content appears truncated (ends with '%s'). Please regenerate the complete content.\nPath: %s", String::chr(trimmed[trimmed.length() - 1]), path), true);
	}

	String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result(_source_root_missing_message(), true);
	}
	String full_path;
	String path_error;
	if (!_resolve_tool_file_path(project_root, path, full_path, path_error)) {
		return _make_result(path_error + "\nPath: " + path, true);
	}
	if (AISettings::load().context_mode == AIContextMode::PROJECT) {
		String save_message;
		if (_save_open_scene_before_ai_write(full_path, save_message)) {
			return _make_result(save_message, true);
		}
	}

	// Create parent directories.
	Ref<DirAccess> da = DirAccess::create_for_path(full_path);
	if (da.is_valid()) {
		Error mkdir_err = da->make_dir_recursive(full_path.get_base_dir());
		if (mkdir_err != OK) {
			return _make_result(vformat("Failed to create directory for %s", path), true);
		}
	} else {
		return _make_result(vformat("Failed to access directory for %s", path), true);
	}

	// Write to a temporary file first so a failed write does not remove the
	// previous version.
	String tmp_path = full_path + ".ai_tmp";
	Error err = OK;
	Ref<FileAccess> file = FileAccess::open(tmp_path, FileAccess::WRITE, &err);
	if (err != OK || file.is_null()) {
		return _make_result(vformat("Failed to open temporary file for writing: %s", path), true);
	}

	const int64_t expected_size = content.utf8().length();
	file->store_string(content);
	file->flush();
	file.unref();

	const int64_t temporary_size = FileAccess::get_size(tmp_path);
	if (temporary_size != expected_size) {
		da->remove(tmp_path);
		return _make_result(vformat("File write rejected: temporary file size mismatch (expected %d bytes, wrote %d bytes). The existing file was not modified.\nPath: %s", expected_size, temporary_size, path), true);
	}

	String bak_path = full_path + ".bak";
	if (FileAccess::exists(bak_path)) {
		Ref<DirAccess> bak_da = DirAccess::create_for_path(bak_path);
		if (bak_da.is_valid()) {
			bak_da->remove(bak_path);
		}
	}

	if (FileAccess::exists(full_path)) {
		Error backup_err = da->rename(full_path, bak_path);
		if (backup_err != OK) {
			da->remove(tmp_path);
			return _make_result(vformat("Failed to create backup before writing: %s", path), true);
		}
	}

	Error rename_err = da->rename(tmp_path, full_path);
	if (rename_err != OK) {
		if (FileAccess::exists(bak_path) && !FileAccess::exists(full_path)) {
			da->rename(bak_path, full_path);
		}
		da->remove(tmp_path);
		return _make_result(vformat("Failed to replace file after writing temporary content: %s", path), true);
	}

	const int64_t final_size = FileAccess::get_size(full_path);
	if (final_size != expected_size) {
		da->remove(full_path);
		if (FileAccess::exists(bak_path)) {
			da->rename(bak_path, full_path);
		}
		return _make_result(vformat("File write failed verification: final file size mismatch (expected %d bytes, got %d bytes). The previous file was restored when available.\nPath: %s", expected_size, final_size, path), true);
	}

	if (AISettings::load().develop_mode) {
		AIDevelopFlow::record_modified(path);
	}
	if (AISettings::load().context_mode == AIContextMode::PROJECT) {
		AIModifiedSceneTracker::mark_scene_written(path);
	}

	// Automatically open HTML prototypes after they are created/updated.
	String path_lower = path.to_lower();
	if (path_lower.contains(".jundotai/prototypes/") && (path_lower.ends_with(".html") || path_lower.ends_with(".htm"))) {
		OS::get_singleton()->shell_open(full_path);
	}

	return _make_result(vformat("Successfully wrote %s (%d bytes).%s", path, expected_size, AISettings::load().develop_mode ? " [Develop Mode: local change only]" : ""));
}

Dictionary AIToolExecutor::_edit_file(const Dictionary &p_args) {
	const String path = p_args.get("path", String());
	const String old_string = p_args.get("old_string", String());
	const String new_string = p_args.get("new_string", String());

	if (path.is_empty()) {
		return _make_result("No file path provided.", true);
	}
	if (old_string.is_empty()) {
		return _make_result(vformat("File edit rejected: old_string is empty.\nPath: %s", path), true);
	}

	const String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result(_source_root_missing_message(), true);
	}
	String full_path;
	String path_error;
	if (!_resolve_tool_file_path(project_root, path, full_path, path_error)) {
		return _make_result(path_error + "\nPath: " + path, true);
	}
	if (!FileAccess::exists(full_path)) {
		return _make_result(vformat("File edit rejected: file not found.\nPath: %s", path), true);
	}

	Error read_err = OK;
	const String content = FileAccess::get_file_as_string(full_path, &read_err);
	if (read_err != OK) {
		return _make_result(vformat("File edit rejected: failed to read file (err=%d).\nPath: %s", (int)read_err, path), true);
	}

	const int occurrence_count = content.count(old_string);
	if (occurrence_count == 0) {
		return _make_result(vformat("File edit rejected: old_string was not found.\nPath: %s", path), true);
	}
	if (occurrence_count > 1) {
		return _make_result(vformat("File edit rejected: old_string occurs %d times; provide more surrounding context so it matches exactly once.\nPath: %s", occurrence_count, path), true);
	}

	Dictionary write_args;
	write_args["path"] = path;
	write_args["content"] = content.replace(old_string, new_string);
	Dictionary result = _write_file(write_args);
	if (!bool(result.get("is_error", false))) {
		result["content"] = vformat("Successfully edited %s by replacing one exact match.", path);
	}
	return result;
}

static String _extract_tscn_quoted_attr(const String &p_line, const String &p_key) {
	const String needle = p_key + "=\"";
	const int start = p_line.find(needle);
	if (start < 0) {
		return String();
	}
	const int value_start = start + needle.length();
	const int value_end = p_line.find("\"", value_start);
	if (value_end < 0) {
		return String();
	}
	return p_line.substr(value_start, value_end - value_start);
}

static String _scene_node_path(const String &p_parent, const String &p_name) {
	if (p_parent.is_empty() || p_parent == ".") {
		return p_name;
	}
	return p_parent.path_join(p_name).replace("\\", "/");
}

static String _sanitize_scene_node_name(const String &p_name, const String &p_fallback) {
	String value = p_name.strip_edges();
	String out;
	for (int i = 0; i < value.length(); i++) {
		const char32_t c = value[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' ') {
			out += String::chr(c);
		}
	}
	out = out.strip_edges();
	return out.is_empty() ? p_fallback : out;
}

static Vector<double> _parse_number_list(String p_value) {
	p_value = p_value.strip_edges();
	const int open = p_value.find("(");
	const int close = p_value.rfind(")");
	if (open >= 0 && close > open) {
		p_value = p_value.substr(open + 1, close - open - 1);
	}
	p_value = p_value.replace(";", ",").replace(" ", ",");
	Vector<double> values;
	PackedStringArray parts = p_value.split(",", false);
	for (int i = 0; i < parts.size(); i++) {
		const String part = String(parts[i]).strip_edges();
		if (!part.is_empty() && part.is_valid_float()) {
			values.push_back(part.to_float());
		}
	}
	return values;
}

static Vector3 _parse_vector3_arg(const Dictionary &p_args, const String &p_key, const Vector3 &p_default, bool p_degrees_to_radians = false) {
	if (!p_args.has(p_key)) {
		return p_default;
	}
	const Vector<double> values = _parse_number_list(String(p_args.get(p_key, String())));
	if (values.size() < 3) {
		return p_default;
	}
	Vector3 result(values[0], values[1], values[2]);
	if (p_degrees_to_radians) {
		result.x = Math::deg_to_rad(result.x);
		result.y = Math::deg_to_rad(result.y);
		result.z = Math::deg_to_rad(result.z);
	}
	return result;
}

static String _format_vector2(const Vector2 &p_value) {
	return vformat("Vector2(%.4f, %.4f)", p_value.x, p_value.y);
}

static String _format_vector3(const Vector3 &p_value) {
	return vformat("Vector3(%.4f, %.4f, %.4f)", p_value.x, p_value.y, p_value.z);
}

static Vector3 _vector3_degrees_to_radians(const Vector3 &p_value) {
	return Vector3(Math::deg_to_rad(p_value.x), Math::deg_to_rad(p_value.y), Math::deg_to_rad(p_value.z));
}

static String _format_color_arg(const Dictionary &p_args, const String &p_key, const String &p_default) {
	if (!p_args.has(p_key)) {
		return p_default;
	}
	String value = String(p_args.get(p_key, String())).strip_edges();
	if (value.is_empty()) {
		return p_default;
	}
	if (value.begins_with("Color(")) {
		return value;
	}
	if (value.begins_with("#") && (value.length() == 7 || value.length() == 9)) {
		const String hex = value.substr(1).to_lower();
		int r = ("0x" + hex.substr(0, 2)).hex_to_int();
		int g = ("0x" + hex.substr(2, 2)).hex_to_int();
		int b = ("0x" + hex.substr(4, 2)).hex_to_int();
		int a = hex.length() >= 8 ? ("0x" + hex.substr(6, 2)).hex_to_int() : 255;
		return vformat("Color(%.4f, %.4f, %.4f, %.4f)", r / 255.0, g / 255.0, b / 255.0, a / 255.0);
	}
	const Vector<double> values = _parse_number_list(value);
	if (values.size() >= 3) {
		const double scale = (values[0] > 1.0 || values[1] > 1.0 || values[2] > 1.0) ? 255.0 : 1.0;
		const double alpha = values.size() >= 4 ? values[3] / (values[3] > 1.0 ? 255.0 : 1.0) : 1.0;
		return vformat("Color(%.4f, %.4f, %.4f, %.4f)", values[0] / scale, values[1] / scale, values[2] / scale, alpha);
	}
	return p_default;
}

static String _unique_tscn_id(const String &p_prefix, const String &p_name) {
	String value = (p_prefix + "_" + p_name).to_lower();
	String out;
	for (int i = 0; i < value.length(); i++) {
		const char32_t c = value[i];
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
			out += String::chr(c);
		} else if (c == '-' || c == ' ') {
			out += "_";
		}
	}
	return out.is_empty() ? p_prefix : out;
}

static String _increment_tscn_load_steps(String p_content, int p_added_steps) {
	const int pos = p_content.find("load_steps=");
	if (pos < 0) {
		const int header_end = p_content.find("]");
		if (header_end >= 0 && p_content.begins_with("[gd_scene")) {
			return p_content.substr(0, header_end) + vformat(" load_steps=%d", MAX(1, p_added_steps)) + p_content.substr(header_end);
		}
		return p_content;
	}
	const int value_start = pos + String("load_steps=").length();
	int value_end = value_start;
	while (value_end < p_content.length() && p_content[value_end] >= '0' && p_content[value_end] <= '9') {
		value_end++;
	}
	const int old_value = p_content.substr(value_start, value_end - value_start).to_int();
	return p_content.substr(0, value_start) + String::num_int64(MAX(1, old_value + p_added_steps)) + p_content.substr(value_end);
}

static String _insert_tscn_subresources(const String &p_content, const String &p_resources) {
	const int first_node = p_content.find("\n[node ");
	if (first_node < 0) {
		return p_content.strip_edges() + "\n\n" + p_resources.strip_edges() + "\n";
	}
	return p_content.substr(0, first_node + 1) + p_resources.strip_edges() + "\n\n" + p_content.substr(first_node + 1);
}

static String _mesh_resource_for_type(const String &p_mesh_type, const String &p_mesh_id, const Dictionary &p_args) {
	const String mesh_type = p_mesh_type.strip_edges().to_lower();
	const Vector<double> size_values = _parse_number_list(String(p_args.get("size", String())));
	if (mesh_type == "sphere") {
		const double radius = size_values.size() >= 1 ? size_values[0] : 0.5;
		return vformat("[sub_resource type=\"SphereMesh\" id=\"%s\"]\nradius = %.4f\nheight = %.4f\n", p_mesh_id, radius, radius * 2.0);
	}
	if (mesh_type == "cylinder") {
		const double radius = size_values.size() >= 1 ? size_values[0] : 0.5;
		const double height = size_values.size() >= 2 ? size_values[1] : 1.0;
		return vformat("[sub_resource type=\"CylinderMesh\" id=\"%s\"]\ntop_radius = %.4f\nbottom_radius = %.4f\nheight = %.4f\n", p_mesh_id, radius, radius, height);
	}
	if (mesh_type == "capsule") {
		const double radius = size_values.size() >= 1 ? size_values[0] : 0.5;
		const double height = size_values.size() >= 2 ? size_values[1] : 2.0;
		return vformat("[sub_resource type=\"CapsuleMesh\" id=\"%s\"]\nradius = %.4f\nheight = %.4f\n", p_mesh_id, radius, height);
	}
	if (mesh_type == "plane" || mesh_type == "quad") {
		const double width = size_values.size() >= 1 ? size_values[0] : 2.0;
		const double depth = size_values.size() >= 2 ? size_values[1] : width;
		const String resource_type = mesh_type == "quad" ? "QuadMesh" : "PlaneMesh";
		return vformat("[sub_resource type=\"%s\" id=\"%s\"]\nsize = %s\n", resource_type, p_mesh_id, _format_vector2(Vector2(width, depth)));
	}
	const Vector3 size = size_values.size() >= 3 ? Vector3(size_values[0], size_values[1], size_values[2]) : Vector3(1, 1, 1);
	return vformat("[sub_resource type=\"BoxMesh\" id=\"%s\"]\nsize = %s\n", p_mesh_id, _format_vector3(size));
}

// ---------------------------------------------------------------------------
// add_physics: Add 2D/3D physics body with collision shape to a .tscn scene.
// ---------------------------------------------------------------------------
Dictionary AIToolExecutor::_add_physics(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("add_physics is only available in PROJECT mode.", true);
	}

	const String scene_path = String(p_args.get("scene_path", String())).strip_edges();
	const String node_name = _sanitize_scene_node_name(String(p_args.get("name", String())), "PhysicsBody");
	if (scene_path.is_empty() || String(p_args.get("name", String())).strip_edges().is_empty()) {
		return _make_result("add_physics rejected: scene_path and name are required.", true);
	}

	const String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result("No open project root is available for physics editing.", true);
	}
	String full_path;
	String path_error;
	if (!_resolve_tool_file_path(project_root, scene_path, full_path, path_error)) {
		return _make_result(path_error + "\nPath: " + scene_path, true);
	}
	if (!full_path.to_lower().ends_with(".tscn") || !FileAccess::exists(full_path)) {
		return _make_result("add_physics requires an existing .tscn scene file.\nPath: " + scene_path, true);
	}

	Error err = OK;
	String content = FileAccess::get_file_as_string(full_path, &err);
	if (err != OK) {
		return _make_result(vformat("Failed to read scene before adding physics (err=%d).\nPath: %s", (int)err, scene_path), true);
	}

	// Determine body type.
	String body_type = String(p_args.get("body_type", "static_3d")).strip_edges().to_lower();
	String body_node_type;
	bool is_2d = false;
	if (body_type == "static_2d") {
		body_node_type = "StaticBody2D";
		is_2d = true;
	} else if (body_type == "rigid_2d") {
		body_node_type = "RigidBody2D";
		is_2d = true;
	} else if (body_type == "character_2d") {
		body_node_type = "CharacterBody2D";
		is_2d = true;
	} else if (body_type == "area_2d") {
		body_node_type = "Area2D";
		is_2d = true;
	} else if (body_type == "rigid_3d") {
		body_node_type = "RigidBody3D";
	} else if (body_type == "character_3d") {
		body_node_type = "CharacterBody3D";
	} else if (body_type == "area_3d") {
		body_node_type = "Area3D";
	} else {
		body_type = "static_3d";
		body_node_type = "StaticBody3D";
	}

	// Determine shape type.
	String shape_type = String(p_args.get("shape_type", is_2d ? "rectangle" : "box")).strip_edges().to_lower();
	String shape_resource_type;
	if (is_2d) {
		if (shape_type == "circle") {
			shape_resource_type = "CircleShape2D";
		} else if (shape_type == "capsule") {
			shape_resource_type = "CapsuleShape2D";
		} else {
			shape_type = "rectangle";
			shape_resource_type = "RectangleShape2D";
		}
	} else {
		if (shape_type == "sphere") {
			shape_resource_type = "SphereShape3D";
		} else if (shape_type == "capsule") {
			shape_resource_type = "CapsuleShape3D";
		} else if (shape_type == "cylinder") {
			shape_resource_type = "CylinderShape3D";
		} else {
			shape_type = "box";
			shape_resource_type = "BoxShape3D";
		}
	}

	const String shape_id = _unique_tscn_id("Shape", node_name);
	const String material_id = _unique_tscn_id("PhysMat", node_name);
	const Vector<double> size_values = _parse_number_list(String(p_args.get("shape_size", String())));
	const double friction = double(p_args.get("friction", 1.0));
	const double bounce = double(p_args.get("bounce", 0.0));
	const double mass = double(p_args.get("mass", 1.0));

	// Build sub-resources.
	String resources;
	resources += vformat("[sub_resource type=\"%s\" id=\"%s\"]\n", shape_resource_type, shape_id);

	if (shape_resource_type == "BoxShape3D") {
		const Vector3 sz = size_values.size() >= 3 ? Vector3(size_values[0], size_values[1], size_values[2]) : Vector3(1, 1, 1);
		resources += "size = " + _format_vector3(sz) + "\n";
	} else if (shape_resource_type == "SphereShape3D") {
		const double radius = size_values.size() >= 1 ? size_values[0] : 0.5;
		resources += vformat("radius = %.4f\n", radius);
	} else if (shape_resource_type == "CapsuleShape3D") {
		const double radius = size_values.size() >= 1 ? size_values[0] : 0.5;
		const double height = size_values.size() >= 2 ? size_values[1] : 2.0;
		resources += vformat("radius = %.4f\n", radius);
		resources += vformat("height = %.4f\n", height);
	} else if (shape_resource_type == "CylinderShape3D") {
		const double radius = size_values.size() >= 1 ? size_values[0] : 0.5;
		const double height = size_values.size() >= 2 ? size_values[1] : 2.0;
		resources += vformat("radius = %.4f\n", radius);
		resources += vformat("height = %.4f\n", height);
	} else if (shape_resource_type == "RectangleShape2D") {
		const Vector2 sz = size_values.size() >= 2 ? Vector2(size_values[0], size_values[1]) : Vector2(1, 1);
		resources += "size = " + _format_vector2(sz) + "\n";
	} else if (shape_resource_type == "CircleShape2D") {
		const double radius = size_values.size() >= 1 ? size_values[0] : 0.5;
		resources += vformat("radius = %.4f\n", radius);
	} else if (shape_resource_type == "CapsuleShape2D") {
		const double radius = size_values.size() >= 1 ? size_values[0] : 0.5;
		const double height = size_values.size() >= 2 ? size_values[1] : 2.0;
		resources += vformat("radius = %.4f\n", radius);
		resources += vformat("height = %.4f\n", height);
	}

	resources += "\n";
	resources += vformat("[sub_resource type=\"PhysicsMaterial\" id=\"%s\"]\n", material_id);
	resources += vformat("friction = %.4f\n", friction);
	resources += vformat("bounce = %.4f\n", bounce);
	resources += "\n";

	// Build body node.
	const String parent = String(p_args.get("parent", ".")).strip_edges().is_empty() ? "." : String(p_args.get("parent", ".")).strip_edges();
	String node_text;
	node_text += vformat("\n[node name=\"%s\" type=\"%s\" parent=\"%s\"]\n", node_name, body_node_type, parent);

	if (is_2d) {
		const Vector<double> pos_values = _parse_number_list(String(p_args.get("position", String())));
		if (pos_values.size() >= 2) {
			node_text += "position = " + _format_vector2(Vector2(pos_values[0], pos_values[1])) + "\n";
		}
	} else {
		const Vector3 position = _parse_vector3_arg(p_args, "position", Vector3());
		if (!position.is_zero_approx()) {
			node_text += "position = " + _format_vector3(position) + "\n";
		}
	}

	if (body_type.begins_with("rigid")) {
		resources = vformat("[sub_resource type=\"PhysicsMaterial\" id=\"%s\"]\nfriction = %.4f\nbounce = %.4f\n\n", material_id, friction, bounce) + resources;
		node_text += vformat("mass = %.4f\n", mass);
		node_text += vformat("physics_material_override = SubResource(\"%s\")\n", material_id);
	} else if (!body_type.begins_with("area")) {
		node_text += vformat("physics_material_override = SubResource(\"%s\")\n", material_id);
	}

	// Build collision shape child node.
	const String collision_name = _sanitize_scene_node_name(node_name + "_Collision", "CollisionShape");
	node_text += vformat("\n[node name=\"%s\" type=\"%s\" parent=\"%s/%s\"]\n",
			collision_name, is_2d ? "CollisionShape2D" : "CollisionShape3D", parent, node_name);
	node_text += vformat("shape = SubResource(\"%s\")\n", shape_id);

	content = _increment_tscn_load_steps(content, 2);
	content = _insert_tscn_subresources(content, resources);
	content = content.strip_edges() + "\n" + node_text;

	Dictionary write_args;
	write_args["path"] = scene_path;
	write_args["content"] = content;
	Dictionary result = _write_file(write_args);
	if (result.has("is_error")) {
		return result;
	}
	return _make_result(String(result.get("content", String())) + vformat("\nAdded %s '%s' with %s collision. Run check_3d_scene on %s after physics edits.", body_node_type, node_name, shape_resource_type, scene_path));
}

// ---------------------------------------------------------------------------
// add_animation: Add AnimationPlayer with animation to a .tscn scene.
// ---------------------------------------------------------------------------
Dictionary AIToolExecutor::_add_animation(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("add_animation is only available in PROJECT mode.", true);
	}

	const String scene_path = String(p_args.get("scene_path", String())).strip_edges();
	const String node_name = _sanitize_scene_node_name(String(p_args.get("name", String())), "AnimationPlayer");
	if (scene_path.is_empty() || String(p_args.get("name", String())).strip_edges().is_empty()) {
		return _make_result("add_animation rejected: scene_path and name are required.", true);
	}

	const String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result("No open project root is available for animation editing.", true);
	}
	String full_path;
	String path_error;
	if (!_resolve_tool_file_path(project_root, scene_path, full_path, path_error)) {
		return _make_result(path_error + "\nPath: " + scene_path, true);
	}
	if (!full_path.to_lower().ends_with(".tscn") || !FileAccess::exists(full_path)) {
		return _make_result("add_animation requires an existing .tscn scene file.\nPath: " + scene_path, true);
	}

	Error err = OK;
	String content = FileAccess::get_file_as_string(full_path, &err);
	if (err != OK) {
		return _make_result(vformat("Failed to read scene before adding animation (err=%d).\nPath: %s", (int)err, scene_path), true);
	}

	const String anim_name = String(p_args.get("animation_name", "default")).strip_edges();
	const String parent = String(p_args.get("parent", ".")).strip_edges().is_empty() ? "." : String(p_args.get("parent", ".")).strip_edges();
	const double duration = double(p_args.get("duration", 1.0));
	const bool loop_mode = bool(p_args.get("loop", false));

	// Determine .tres path for the animation resource.
	const String safe_name = _ai_safe_git_segment(anim_name, "anim");
	String anim_tres_rel;
	const int last_slash = scene_path.rfind("/");
	if (last_slash >= 0) {
		anim_tres_rel = scene_path.substr(0, last_slash + 1) + "animations/" + safe_name + ".tres";
	} else {
		anim_tres_rel = "animations/" + safe_name + ".tres";
	}

	// Build the .tres animation resource.
	StringBuilder tres;
	tres += "[gd_resource type=\"Animation\" format=3]\n\n";

	// Parse optional tracks JSON.
	const String tracks_json = String(p_args.get("tracks", String())).strip_edges();
	Array tracks_array;
	if (!tracks_json.is_empty()) {
		Variant parsed = JSON::parse_string(tracks_json);
		if (parsed.get_type() == Variant::ARRAY) {
			tracks_array = parsed;
		}
	}

	int track_count = tracks_array.size();
	if (track_count == 0) {
		// Create a placeholder track animating the root node visibility.
		tres += "[resource]\n";
		tres += vformat("length = %.4f\n", duration);
		tres += vformat("loop_mode = %d\n", loop_mode ? 1 : 0);
		tres += "tracks/0/type = \"value\"\n";
		tres += "tracks/0/imported = false\n";
		tres += "tracks/0/enabled = true\n";
		tres += "tracks/0/path = NodePath(\".:visible\")\n";
		tres += "tracks/0/interp = 1\n";
		tres += "tracks/0/loop_wrap = -1\n";
		tres += "tracks/0/keys = PackedFloat32Array(0, 1, 1)\n";
	} else {
		tres += "[resource]\n";
		tres += vformat("length = %.4f\n", duration);
		tres += vformat("loop_mode = %d\n", loop_mode ? 1 : 0);
		for (int i = 0; i < track_count; i++) {
			Dictionary track = tracks_array[i];
			const String node_path = String(track.get("node_path", ".")).strip_edges();
			const String property = String(track.get("property", "visible")).strip_edges();
			const String track_type = String(track.get("type", "value")).strip_edges();
			const String full_path_np = node_path + ":" + property;

			tres += vformat("tracks/%d/type = \"%s\"\n", i, track_type);
			tres += "tracks/" + String::num_int64(i) + "/imported = false\n";
			tres += "tracks/" + String::num_int64(i) + "/enabled = true\n";
			tres += vformat("tracks/%d/path = NodePath(\"%s\")\n", i, full_path_np);
			tres += "tracks/" + String::num_int64(i) + "/interp = 1\n";
			tres += "tracks/" + String::num_int64(i) + "/loop_wrap = -1\n";

			// Build keys array.
			Array keys = track.get("keys", Array());
			if (keys.is_empty()) {
				tres += "tracks/" + String::num_int64(i) + "/keys = PackedFloat32Array(0, 1, 1)\n";
			} else {
				// Build a PackedFloat32Array from keyframes: [time, transition, value, ...]
				StringBuilder key_str;
				key_str += "PackedFloat32Array(";
				for (int k = 0; k < keys.size(); k++) {
					Dictionary kf = keys[k];
					const double time = double(kf.get("time", 0.0));
					const double transition = double(kf.get("transition", 1.0));
					const double value = double(kf.get("value", 0.0));
					if (k > 0) {
						key_str += ", ";
					}
					key_str += vformat("%.4f, %.4f, %.4f", time, transition, value);
				}
				key_str += ")";
				tres += "tracks/" + String::num_int64(i) + "/keys = " + key_str.as_string() + "\n";
			}
		}
	}

	// Write the .tres file.
	Dictionary tres_write_args;
	tres_write_args["path"] = anim_tres_rel;
	tres_write_args["content"] = tres.as_string();
	Dictionary tres_result = _write_file(tres_write_args);
	if (tres_result.has("is_error")) {
		return _make_result(vformat("Failed to write animation resource: %s", String(tres_result.get("content", String()))), true);
	}

	// Add AnimationPlayer node to the scene.
	String node_text;
	node_text += vformat("\n[node name=\"%s\" type=\"AnimationPlayer\" parent=\"%s\"]\n", node_name, parent);
	node_text += vformat("libraries = {&\"\": ExtResource(\"ext_resource_placeholder\")}\n");

	// We need to add an ext_resource for the .tres file. Find the highest ext_resource id.
	int max_ext_id = 0;
	PackedStringArray content_lines = content.split("\n");
	for (int i = 0; i < content_lines.size(); i++) {
		const String line = String(content_lines[i]).strip_edges();
		if (line.begins_with("[ext_resource ")) {
			const String id_str = _extract_tscn_quoted_attr(line, "id");
			if (!id_str.is_empty()) {
				// ext_resource ids can be numeric or string.
				const int numeric_id = id_str.to_int();
				if (numeric_id > max_ext_id) {
					max_ext_id = numeric_id;
				}
			}
		}
	}
	const int new_ext_id = max_ext_id + 1;
	const String ext_id_str = String::num_int64(new_ext_id);

	// Insert ext_resource before the first [sub_resource] or [node].
	const String ext_resource_line = vformat("[ext_resource type=\"Animation\" path=\"%s\" id=\"%s\"]\n", anim_tres_rel, ext_id_str);
	const int first_sub = content.find("[sub_resource ");
	const int first_node = content.find("[node ");
	int insert_pos = -1;
	if (first_sub >= 0 && (first_node < 0 || first_sub < first_node)) {
		insert_pos = first_sub;
	} else if (first_node >= 0) {
		insert_pos = first_node;
	}
	if (insert_pos >= 0) {
		content = content.substr(0, insert_pos) + ext_resource_line + "\n" + content.substr(insert_pos);
	} else {
		content = content.strip_edges() + "\n" + ext_resource_line;
	}

	// Fix the AnimationPlayer libraries to use the correct ext_resource id.
	node_text = vformat("\n[node name=\"%s\" type=\"AnimationPlayer\" parent=\"%s\"]\n", node_name, parent);
	node_text += vformat("libraries = {&\"\": ExtResource(\"%s\")}\n", ext_id_str);
	node_text += vformat("autoplay = \"%s\"\n", anim_name);

	content = content.strip_edges() + "\n" + node_text;

	Dictionary write_args;
	write_args["path"] = scene_path;
	write_args["content"] = content;
	Dictionary result = _write_file(write_args);
	if (result.has("is_error")) {
		return result;
	}
	return _make_result(String(result.get("content", String())) + vformat("\nAdded AnimationPlayer '%s' with animation '%s' (%.2fs%s). Animation resource saved to %s.",
			node_name, anim_name, duration, loop_mode ? ", looping" : "", anim_tres_rel));
}

// ---------------------------------------------------------------------------
// add_particles: Add 2D/3D GPU particles to a .tscn scene.
// ---------------------------------------------------------------------------
Dictionary AIToolExecutor::_add_particles(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("add_particles is only available in PROJECT mode.", true);
	}

	const String scene_path = String(p_args.get("scene_path", String())).strip_edges();
	const String node_name = _sanitize_scene_node_name(String(p_args.get("name", String())), "Particles");
	if (scene_path.is_empty() || String(p_args.get("name", String())).strip_edges().is_empty()) {
		return _make_result("add_particles rejected: scene_path and name are required.", true);
	}

	const String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result("No open project root is available for particle editing.", true);
	}
	String full_path;
	String path_error;
	if (!_resolve_tool_file_path(project_root, scene_path, full_path, path_error)) {
		return _make_result(path_error + "\nPath: " + scene_path, true);
	}
	if (!full_path.to_lower().ends_with(".tscn") || !FileAccess::exists(full_path)) {
		return _make_result("add_particles requires an existing .tscn scene file.\nPath: " + scene_path, true);
	}

	Error err = OK;
	String content = FileAccess::get_file_as_string(full_path, &err);
	if (err != OK) {
		return _make_result(vformat("Failed to read scene before adding particles (err=%d).\nPath: %s", (int)err, scene_path), true);
	}

	const String dimension = String(p_args.get("dimension", "3d")).strip_edges().to_lower();
	const bool is_2d = dimension == "2d" || dimension == "2";
	const String particle_node = is_2d ? "GPUParticles2D" : "GPUParticles3D";

	const String parent = String(p_args.get("parent", ".")).strip_edges().is_empty() ? "." : String(p_args.get("parent", ".")).strip_edges();
	const int amount = int(p_args.get("amount", 32));
	const double lifetime = double(p_args.get("lifetime", 2.0));
	const bool one_shot = bool(p_args.get("one_shot", false));
	const double explosiveness = double(p_args.get("explosiveness", 0.0));
	const double initial_velocity = double(p_args.get("initial_velocity", 2.0));
	const double angular_velocity = double(p_args.get("angular_velocity", 0.0));
	const double scale_amount = double(p_args.get("scale_amount", 1.0));
	const double spread = double(p_args.get("spread", 45.0));

	// Parse direction and gravity vectors.
	const Vector<double> dir_values = _parse_number_list(String(p_args.get("direction", String())));
	Vector3 direction(0, -1, 0);
	if (dir_values.size() >= 3) {
		direction = Vector3(dir_values[0], dir_values[1], dir_values[2]);
	} else if (dir_values.size() >= 2) {
		direction = Vector3(dir_values[0], dir_values[1], 0);
	}

	const Vector<double> grav_values = _parse_number_list(String(p_args.get("gravity", String())));
	Vector3 gravity(0, -9.8, 0);
	if (grav_values.size() >= 3) {
		gravity = Vector3(grav_values[0], grav_values[1], grav_values[2]);
	} else if (grav_values.size() >= 2) {
		gravity = Vector3(grav_values[0], grav_values[1], 0);
	}

	const String color_str = _format_color_arg(p_args, "color", "Color(1, 1, 1, 1)");

	// Emission shape.
	const String emission_shape = String(p_args.get("emission_shape", "point")).strip_edges().to_lower();
	const Vector<double> extent_values = _parse_number_list(String(p_args.get("emission_extents", String())));

	// Build the .tres ParticleProcessMaterial.
	const String safe_name = _ai_safe_git_segment(node_name, "particles");
	String mat_tres_rel;
	const int last_slash = scene_path.rfind("/");
	if (last_slash >= 0) {
		mat_tres_rel = scene_path.substr(0, last_slash + 1) + "particles/" + safe_name + "_material.tres";
	} else {
		mat_tres_rel = "particles/" + safe_name + "_material.tres";
	}

	StringBuilder mat_tres;
	mat_tres += "[gd_resource type=\"ParticleProcessMaterial\" format=3]\n\n";
	mat_tres += "[resource]\n";
	mat_tres += vformat("direction = %s\n", _format_vector3(direction));
	mat_tres += vformat("spread = %.4f\n", spread);
	mat_tres += vformat("initial_velocity_min = %.4f\n", initial_velocity);
	mat_tres += vformat("initial_velocity_max = %.4f\n", initial_velocity);
	if (angular_velocity != 0.0) {
		mat_tres += vformat("angular_velocity_min = %.4f\n", angular_velocity);
		mat_tres += vformat("angular_velocity_max = %.4f\n", angular_velocity);
	}
	mat_tres += vformat("gravity = %s\n", _format_vector3(gravity));
	mat_tres += vformat("damping_min = 0.0\n");
	mat_tres += vformat("damping_max = 0.0\n");
	mat_tres += vformat("scale_min = %.4f\n", scale_amount);
	mat_tres += vformat("scale_max = %.4f\n", scale_amount);
	mat_tres += vformat("color = %s\n", color_str);

	// Emission shape.
	if (emission_shape == "sphere") {
		const double radius = extent_values.size() >= 1 ? extent_values[0] : 1.0;
		mat_tres += "emission_shape = 1\n";
		mat_tres += vformat("emission_sphere_radius = %.4f\n", radius);
	} else if (emission_shape == "box") {
		const Vector3 extents = extent_values.size() >= 3 ? Vector3(extent_values[0], extent_values[1], extent_values[2]) : Vector3(1, 1, 1);
		mat_tres += "emission_shape = 2\n";
		mat_tres += "emission_box_extents = " + _format_vector3(extents) + "\n";
	}

	// Write the .tres file.
	Dictionary tres_write_args;
	tres_write_args["path"] = mat_tres_rel;
	tres_write_args["content"] = mat_tres.as_string();
	Dictionary tres_result = _write_file(tres_write_args);
	if (tres_result.has("is_error")) {
		return _make_result(vformat("Failed to write particle material: %s", String(tres_result.get("content", String()))), true);
	}

	// Add ext_resource for the material.
	int max_ext_id = 0;
	PackedStringArray content_lines = content.split("\n");
	for (int i = 0; i < content_lines.size(); i++) {
		const String line = String(content_lines[i]).strip_edges();
		if (line.begins_with("[ext_resource ")) {
			const String id_str = _extract_tscn_quoted_attr(line, "id");
			if (!id_str.is_empty()) {
				const int numeric_id = id_str.to_int();
				if (numeric_id > max_ext_id) {
					max_ext_id = numeric_id;
				}
			}
		}
	}
	const int new_ext_id = max_ext_id + 1;
	const String ext_id_str = String::num_int64(new_ext_id);

	const String ext_resource_line = vformat("[ext_resource type=\"ParticleProcessMaterial\" path=\"%s\" id=\"%s\"]\n", mat_tres_rel, ext_id_str);
	const int first_sub = content.find("[sub_resource ");
	const int first_node = content.find("[node ");
	int insert_pos = -1;
	if (first_sub >= 0 && (first_node < 0 || first_sub < first_node)) {
		insert_pos = first_sub;
	} else if (first_node >= 0) {
		insert_pos = first_node;
	}
	if (insert_pos >= 0) {
		content = content.substr(0, insert_pos) + ext_resource_line + "\n" + content.substr(insert_pos);
	} else {
		content = content.strip_edges() + "\n" + ext_resource_line;
	}

	// Build particle node.
	String node_text;
	node_text += vformat("\n[node name=\"%s\" type=\"%s\" parent=\"%s\"]\n", node_name, particle_node, parent);

	if (is_2d) {
		const Vector<double> pos_values = _parse_number_list(String(p_args.get("position", String())));
		if (pos_values.size() >= 2) {
			node_text += "position = " + _format_vector2(Vector2(pos_values[0], pos_values[1])) + "\n";
		}
	} else {
		const Vector3 position = _parse_vector3_arg(p_args, "position", Vector3());
		if (!position.is_zero_approx()) {
			node_text += "position = " + _format_vector3(position) + "\n";
		}
	}

	node_text += vformat("amount = %d\n", amount);
	node_text += vformat("lifetime = %.4f\n", lifetime);
	if (one_shot) {
		node_text += "one_shot = true\n";
	}
	if (explosiveness > 0.0) {
		node_text += vformat("explosiveness = %.4f\n", explosiveness);
	}
	node_text += vformat("process_material = ExtResource(\"%s\")\n", ext_id_str);

	content = content.strip_edges() + "\n" + node_text;

	Dictionary write_args;
	write_args["path"] = scene_path;
	write_args["content"] = content;
	Dictionary result = _write_file(write_args);
	if (result.has("is_error")) {
		return result;
	}
	return _make_result(String(result.get("content", String())) + vformat("\nAdded %s '%s' (%d particles, %.2fs lifetime). Material saved to %s.",
			particle_node, node_name, amount, lifetime, mat_tres_rel));
}

// ---------------------------------------------------------------------------
// add_vfx: Add WorldEnvironment with visual effects to a .tscn scene.
// ---------------------------------------------------------------------------
Dictionary AIToolExecutor::_add_vfx(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("add_vfx is only available in PROJECT mode.", true);
	}

	const String scene_path = String(p_args.get("scene_path", String())).strip_edges();
	const String node_name = _sanitize_scene_node_name(String(p_args.get("name", String())), "WorldEnvironment");
	if (scene_path.is_empty() || String(p_args.get("name", String())).strip_edges().is_empty()) {
		return _make_result("add_vfx rejected: scene_path and name are required.", true);
	}

	const String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result("No open project root is available for VFX editing.", true);
	}
	String full_path;
	String path_error;
	if (!_resolve_tool_file_path(project_root, scene_path, full_path, path_error)) {
		return _make_result(path_error + "\nPath: " + scene_path, true);
	}
	if (!full_path.to_lower().ends_with(".tscn") || !FileAccess::exists(full_path)) {
		return _make_result("add_vfx requires an existing .tscn scene file.\nPath: " + scene_path, true);
	}

	Error err = OK;
	String content = FileAccess::get_file_as_string(full_path, &err);
	if (err != OK) {
		return _make_result(vformat("Failed to read scene before adding VFX (err=%d).\nPath: %s", (int)err, scene_path), true);
	}

	const String parent = String(p_args.get("parent", ".")).strip_edges().is_empty() ? "." : String(p_args.get("parent", ".")).strip_edges();
	const bool glow_enabled = bool(p_args.get("glow_enabled", false));
	const double glow_intensity = double(p_args.get("glow_intensity", 0.3));
	const bool ao_enabled = bool(p_args.get("ao_enabled", false));
	const double ao_radius = double(p_args.get("ao_radius", 2.0));
	const double ao_power = double(p_args.get("ao_power", 1.5));
	const bool fog_enabled = bool(p_args.get("fog_enabled", false));
	const String fog_color = _format_color_arg(p_args, "fog_color", "Color(0.7, 0.7, 0.7, 1)");
	const double fog_depth_begin = double(p_args.get("fog_depth_begin", 10.0));
	const double fog_depth_end = double(p_args.get("fog_depth_end", 100.0));
	const bool volumetric_fog_enabled = bool(p_args.get("volumetric_fog_enabled", false));
	const double volumetric_fog_density = double(p_args.get("volumetric_fog_density", 0.05));
	const String ambient_color = _format_color_arg(p_args, "ambient_light_color", "Color(0.1, 0.1, 0.1, 1)");
	const double ambient_energy = double(p_args.get("ambient_light_energy", 0.5));

	// Tonemap mode.
	String tonemap_str = String(p_args.get("tonemap_mode", "filmic")).strip_edges().to_lower();
	int tonemap_mode_int = 3; // filmic
	if (tonemap_str == "disabled") {
		tonemap_mode_int = 0;
	} else if (tonemap_str == "linear") {
		tonemap_mode_int = 1;
	} else if (tonemap_str == "reinhart") {
		tonemap_mode_int = 2;
	} else if (tonemap_str == "aces") {
		tonemap_mode_int = 4;
	}

	const String env_id = _unique_tscn_id("Env", node_name);

	// Build Environment sub-resource.
	String resources;
	resources += vformat("[sub_resource type=\"Environment\" id=\"%s\"]\n", env_id);
	resources += vformat("tonemap_mode = %d\n", tonemap_mode_int);
	resources += vformat("ambient_light_color = %s\n", ambient_color);
	resources += vformat("ambient_light_energy = %.4f\n", ambient_energy);

	if (glow_enabled) {
		resources += "glow_enabled = true\n";
		resources += vformat("glow_bloom = %.4f\n", glow_intensity);
		// Parse glow strength per level if provided.
		const String glow_strength_str = String(p_args.get("glow_strength", String())).strip_edges();
		if (!glow_strength_str.is_empty()) {
			const Vector<double> strength_values = _parse_number_list(glow_strength_str);
			if (strength_values.size() >= 7) {
				resources += "glow_strength = PackedFloat32Array(";
				for (int i = 0; i < 7; i++) {
					if (i > 0) {
						resources += ", ";
					}
					resources += vformat("%.4f", strength_values[i]);
				}
				resources += ")\n";
			}
		}
	}

	if (ao_enabled) {
		resources += "ssao_enabled = true\n";
		resources += vformat("ssao_radius = %.4f\n", ao_radius);
		resources += vformat("ssao_power = %.4f\n", ao_power);
	}

	if (fog_enabled) {
		resources += "fog_enabled = true\n";
		resources += vformat("fog_light_color = %s\n", fog_color);
		resources += vformat("fog_depth_begin = %.4f\n", fog_depth_begin);
		resources += vformat("fog_depth_end = %.4f\n", fog_depth_end);
	}

	if (volumetric_fog_enabled) {
		resources += "volumetric_fog_enabled = true\n";
		resources += vformat("volumetric_fog_density = %.4f\n", volumetric_fog_density);
	}

	// Build WorldEnvironment node.
	String node_text;
	node_text += vformat("\n[node name=\"%s\" type=\"WorldEnvironment\" parent=\"%s\"]\n", node_name, parent);
	node_text += vformat("environment = SubResource(\"%s\")\n", env_id);

	content = _increment_tscn_load_steps(content, 1);
	content = _insert_tscn_subresources(content, resources);
	content = content.strip_edges() + "\n" + node_text;

	Dictionary write_args;
	write_args["path"] = scene_path;
	write_args["content"] = content;
	Dictionary result = _write_file(write_args);
	if (result.has("is_error")) {
		return result;
	}

	// Build a summary of enabled effects.
	Vector<String> enabled_effects;
	if (glow_enabled) {
		enabled_effects.push_back("glow");
	}
	if (ao_enabled) {
		enabled_effects.push_back("AO");
	}
	if (fog_enabled) {
		enabled_effects.push_back("fog");
	}
	if (volumetric_fog_enabled) {
		enabled_effects.push_back("volumetric fog");
	}
	String effects_summary;
	for (int i = 0; i < enabled_effects.size(); i++) {
		if (i > 0) {
			effects_summary += ", ";
		}
		effects_summary += enabled_effects[i];
	}
	if (effects_summary.is_empty()) {
		effects_summary = "ambient light + tonemap";
	}

	return _make_result(String(result.get("content", String())) + vformat("\nAdded WorldEnvironment '%s' with effects: %s.", node_name, effects_summary));
}

// ---------------------------------------------------------------------------
// add_character_controller: Add 2D/3D CharacterBody with movement script.
// ---------------------------------------------------------------------------
Dictionary AIToolExecutor::_add_character_controller(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("add_character_controller is only available in PROJECT mode.", true);
	}

	const String scene_path = String(p_args.get("scene_path", String())).strip_edges();
	const String node_name = _sanitize_scene_node_name(String(p_args.get("name", String())), "Character");
	if (scene_path.is_empty() || String(p_args.get("name", String())).strip_edges().is_empty()) {
		return _make_result("add_character_controller rejected: scene_path and name are required.", true);
	}

	const String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result("No open project root is available for character controller editing.", true);
	}
	String full_path;
	String path_error;
	if (!_resolve_tool_file_path(project_root, scene_path, full_path, path_error)) {
		return _make_result(path_error + "\nPath: " + scene_path, true);
	}
	if (!full_path.to_lower().ends_with(".tscn") || !FileAccess::exists(full_path)) {
		return _make_result("add_character_controller requires an existing .tscn scene file.\nPath: " + scene_path, true);
	}

	Error err = OK;
	String content = FileAccess::get_file_as_string(full_path, &err);
	if (err != OK) {
		return _make_result(vformat("Failed to read scene before adding character controller (err=%d).\nPath: %s", (int)err, scene_path), true);
	}

	const String dimension = String(p_args.get("dimension", "3d")).strip_edges().to_lower();
	const bool is_2d = dimension == "2d" || dimension == "2";
	const String body_node = is_2d ? "CharacterBody2D" : "CharacterBody3D";
	const String parent = String(p_args.get("parent", ".")).strip_edges().is_empty() ? "." : String(p_args.get("parent", ".")).strip_edges();
	const double speed = double(p_args.get("speed", 5.0));
	const double jump_vel = double(p_args.get("jump_velocity", 4.5));

	// Determine shape type and size.
	String shape_type = String(p_args.get("shape_type", "capsule")).strip_edges().to_lower();
	const String shape_id = _unique_tscn_id("CharShape", node_name);
	const Vector<double> size_values = _parse_number_list(String(p_args.get("shape_size", String())));

	String shape_resource_type;
	if (is_2d) {
		if (shape_type == "rectangle") {
			shape_resource_type = "RectangleShape2D";
		} else if (shape_type == "circle") {
			shape_resource_type = "CircleShape2D";
		} else {
			shape_type = "capsule";
			shape_resource_type = "CapsuleShape2D";
		}
	} else {
		if (shape_type == "box") {
			shape_resource_type = "BoxShape3D";
		} else if (shape_type == "sphere") {
			shape_resource_type = "SphereShape3D";
		} else {
			shape_type = "capsule";
			shape_resource_type = "CapsuleShape3D";
		}
	}

	// Build shape sub-resource.
	String resources;
	resources += vformat("[sub_resource type=\"%s\" id=\"%s\"]\n", shape_resource_type, shape_id);
	if (shape_resource_type == "CapsuleShape3D") {
		const double radius = size_values.size() >= 1 ? size_values[0] : 0.5;
		const double height = size_values.size() >= 2 ? size_values[1] : 1.8;
		resources += vformat("radius = %.4f\n", radius);
		resources += vformat("height = %.4f\n", height);
	} else if (shape_resource_type == "SphereShape3D") {
		const double radius = size_values.size() >= 1 ? size_values[0] : 0.5;
		resources += vformat("radius = %.4f\n", radius);
	} else if (shape_resource_type == "BoxShape3D") {
		const Vector3 sz = size_values.size() >= 3 ? Vector3(size_values[0], size_values[1], size_values[2]) : Vector3(1, 1.8, 1);
		resources += "size = " + _format_vector3(sz) + "\n";
	} else if (shape_resource_type == "CapsuleShape2D") {
		const double radius = size_values.size() >= 1 ? size_values[0] : 0.5;
		const double height = size_values.size() >= 2 ? size_values[1] : 1.8;
		resources += vformat("radius = %.4f\n", radius);
		resources += vformat("height = %.4f\n", height);
	} else if (shape_resource_type == "CircleShape2D") {
		const double radius = size_values.size() >= 1 ? size_values[0] : 0.5;
		resources += vformat("radius = %.4f\n", radius);
	} else if (shape_resource_type == "RectangleShape2D") {
		const Vector2 sz = size_values.size() >= 2 ? Vector2(size_values[0], size_values[1]) : Vector2(1, 1.8);
		resources += "size = " + _format_vector2(sz) + "\n";
	}

	// Determine script path.
	String script_path_rel = String(p_args.get("script_path", String())).strip_edges();
	if (script_path_rel.is_empty()) {
		const String safe_name = _ai_safe_git_segment(node_name, "character");
		script_path_rel = "scripts/" + safe_name + (is_2d ? "_controller_2d.gd" : "_controller.gd");
	}

	// Build GDScript content.
	StringBuilder script_content;
	if (is_2d) {
		script_content += "extends CharacterBody2D\n\n";
		script_content += vformat("const SPEED = %.1f\n", speed);
		script_content += vformat("const JUMP_VELOCITY = %.1f\n\n", jump_vel);
		script_content += "func _physics_process(delta: float) -> void:\n";
		script_content += "\t# Add gravity.\n";
		script_content += "\tif not is_on_floor():\n";
		script_content += "\t\tvelocity += get_gravity() * delta\n\n";
		script_content += "\t# Handle jump.\n";
		script_content += "\tif Input.is_action_just_pressed(\"ui_accept\") and is_on_floor():\n";
		script_content += "\t\tvelocity.y = JUMP_VELOCITY\n\n";
		script_content += "\t# Get input direction.\n";
		script_content += "\tvar direction := Input.get_vector(\"ui_left\", \"ui_right\", \"ui_up\", \"ui_down\")\n";
		script_content += "\tif direction:\n";
		script_content += "\t\tvelocity.x = direction.x * SPEED\n";
		script_content += "\t\tvelocity.y = direction.y * SPEED\n";
		script_content += "\telse:\n";
		script_content += "\t\tvelocity.x = move_toward(velocity.x, 0, SPEED)\n\n";
		script_content += "\tmove_and_slide()\n";
	} else {
		script_content += "extends CharacterBody3D\n\n";
		script_content += vformat("const SPEED = %.1f\n", speed);
		script_content += vformat("const JUMP_VELOCITY = %.1f\n\n", jump_vel);
		script_content += "func _physics_process(delta: float) -> void:\n";
		script_content += "\t# Add gravity.\n";
		script_content += "\tif not is_on_floor():\n";
		script_content += "\t\tvelocity += get_gravity() * delta\n\n";
		script_content += "\t# Handle jump.\n";
		script_content += "\tif Input.is_action_just_pressed(\"ui_accept\") and is_on_floor():\n";
		script_content += "\t\tvelocity.y = JUMP_VELOCITY\n\n";
		script_content += "\t# Get input direction.\n";
		script_content += "\tvar input_dir := Input.get_vector(\"ui_left\", \"ui_right\", \"ui_up\", \"ui_down\")\n";
		script_content += "\tvar direction := (transform.basis * Vector3(input_dir.x, 0, input_dir.y)).normalized()\n";
		script_content += "\tif direction:\n";
		script_content += "\t\tvelocity.x = direction.x * SPEED\n";
		script_content += "\t\tvelocity.z = direction.z * SPEED\n";
		script_content += "\telse:\n";
		script_content += "\t\tvelocity.x = move_toward(velocity.x, 0, SPEED)\n";
		script_content += "\t\tvelocity.z = move_toward(velocity.z, 0, SPEED)\n\n";
		script_content += "\tmove_and_slide()\n";
	}

	// Write the script file.
	Dictionary script_write_args;
	script_write_args["path"] = script_path_rel;
	script_write_args["content"] = script_content.as_string();
	Dictionary script_result = _write_file(script_write_args);
	if (script_result.has("is_error")) {
		return _make_result(vformat("Failed to write movement script: %s", String(script_result.get("content", String()))), true);
	}

	// Add ext_resource for the script.
	int max_ext_id = 0;
	PackedStringArray content_lines = content.split("\n");
	for (int i = 0; i < content_lines.size(); i++) {
		const String line = String(content_lines[i]).strip_edges();
		if (line.begins_with("[ext_resource ")) {
			const String id_str = _extract_tscn_quoted_attr(line, "id");
			if (!id_str.is_empty()) {
				const int numeric_id = id_str.to_int();
				if (numeric_id > max_ext_id) {
					max_ext_id = numeric_id;
				}
			}
		}
	}
	const int new_ext_id = max_ext_id + 1;
	const String ext_id_str = String::num_int64(new_ext_id);

	const String ext_resource_line = vformat("[ext_resource type=\"Script\" path=\"%s\" id=\"%s\"]\n", script_path_rel, ext_id_str);
	const int first_sub = content.find("[sub_resource ");
	const int first_node = content.find("[node ");
	int insert_pos = -1;
	if (first_sub >= 0 && (first_node < 0 || first_sub < first_node)) {
		insert_pos = first_sub;
	} else if (first_node >= 0) {
		insert_pos = first_node;
	}
	if (insert_pos >= 0) {
		content = content.substr(0, insert_pos) + ext_resource_line + "\n" + content.substr(insert_pos);
	} else {
		content = content.strip_edges() + "\n" + ext_resource_line;
	}

	// Build CharacterBody node.
	String node_text;
	node_text += vformat("\n[node name=\"%s\" type=\"%s\" parent=\"%s\"]\n", node_name, body_node, parent);

	if (is_2d) {
		const Vector<double> pos_values = _parse_number_list(String(p_args.get("position", String())));
		if (pos_values.size() >= 2) {
			node_text += "position = " + _format_vector2(Vector2(pos_values[0], pos_values[1])) + "\n";
		}
	} else {
		const Vector3 position = _parse_vector3_arg(p_args, "position", Vector3());
		if (!position.is_zero_approx()) {
			node_text += "position = " + _format_vector3(position) + "\n";
		}
	}

	node_text += vformat("script = ExtResource(\"%s\")\n", ext_id_str);

	// Build collision shape child.
	const String collision_name = _sanitize_scene_node_name(node_name + "_Collision", "CollisionShape");
	node_text += vformat("\n[node name=\"%s\" type=\"%s\" parent=\"%s/%s\"]\n",
			collision_name, is_2d ? "CollisionShape2D" : "CollisionShape3D", parent, node_name);
	node_text += vformat("shape = SubResource(\"%s\")\n", shape_id);

	content = _increment_tscn_load_steps(content, 1);
	content = _insert_tscn_subresources(content, resources);
	content = content.strip_edges() + "\n" + node_text;

	Dictionary write_args;
	write_args["path"] = scene_path;
	write_args["content"] = content;
	Dictionary result = _write_file(write_args);
	if (result.has("is_error")) {
		return result;
	}
	return _make_result(String(result.get("content", String())) + vformat("\nAdded %s '%s' with %s collision and movement script (%s). Speed=%.1f, Jump=%.1f.",
			body_node, node_name, shape_resource_type, script_path_rel, speed, jump_vel));
}

// ---------------------------------------------------------------------------
// Helper: parse .tscn into node blocks.
// ---------------------------------------------------------------------------
struct AIToolNodeBlock {
	String name;
	String type;
	String parent;
	String full_path;
	int start_line;
	int end_line;
	Vector<String> lines;
};

static Vector<AIToolNodeBlock> _parse_tscn_node_blocks(const String &p_content) {
	Vector<AIToolNodeBlock> blocks;
	PackedStringArray lines = p_content.split("\n");
	AIToolNodeBlock current;
	bool in_node = false;

	for (int i = 0; i < lines.size(); i++) {
		const String line = String(lines[i]).strip_edges();
		if (line.begins_with("[node ")) {
			if (in_node) {
				blocks.push_back(current);
			}
			in_node = true;
			current = AIToolNodeBlock();
			current.name = _extract_tscn_quoted_attr(line, "name");
			current.type = _extract_tscn_quoted_attr(line, "type");
			current.parent = _extract_tscn_quoted_attr(line, "parent");
			current.start_line = i;
			current.lines.clear();
			current.lines.push_back(String(lines[i]));
			// Compute full path.
			if (current.parent.is_empty() || current.parent == ".") {
				current.full_path = current.name;
			} else {
				current.full_path = current.parent + "/" + current.name;
			}
		} else if (in_node) {
			current.lines.push_back(String(lines[i]));
		}
	}
	if (in_node) {
		blocks.push_back(current);
	}
	return blocks;
}

static bool _is_descendant_path(const String &p_parent_path, const String &p_child_path) {
	if (p_parent_path == p_child_path) {
		return true;
	}
	return p_child_path.begins_with(p_parent_path + "/");
}

// ---------------------------------------------------------------------------
// remove_node: Remove a node and its children from a .tscn scene.
// ---------------------------------------------------------------------------
Dictionary AIToolExecutor::_remove_node(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("remove_node is only available in PROJECT mode.", true);
	}

	const String scene_path = String(p_args.get("scene_path", String())).strip_edges();
	const String node_path = String(p_args.get("node_path", String())).strip_edges();
	if (scene_path.is_empty() || node_path.is_empty()) {
		return _make_result("remove_node rejected: scene_path and node_path are required.", true);
	}

	const String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result("No open project root is available.", true);
	}
	String full_path;
	String path_error;
	if (!_resolve_tool_file_path(project_root, scene_path, full_path, path_error)) {
		return _make_result(path_error + "\nPath: " + scene_path, true);
	}
	if (!full_path.to_lower().ends_with(".tscn") || !FileAccess::exists(full_path)) {
		return _make_result("remove_node requires an existing .tscn scene file.\nPath: " + scene_path, true);
	}

	Error err = OK;
	String content = FileAccess::get_file_as_string(full_path, &err);
	if (err != OK) {
		return _make_result(vformat("Failed to read scene (err=%d).\nPath: %s", (int)err, scene_path), true);
	}

	Vector<AIToolNodeBlock> blocks = _parse_tscn_node_blocks(content);
	if (blocks.is_empty()) {
		return _make_result("No nodes found in scene.", true);
	}

	// Find the target node.
	int target_idx = -1;
	for (int i = 0; i < blocks.size(); i++) {
		if (blocks[i].full_path == node_path || (node_path == "." && i == 0)) {
			target_idx = i;
			break;
		}
	}
	if (target_idx < 0) {
		return _make_result(vformat("Node '%s' not found in %s.", node_path, scene_path), true);
	}

	// Check if trying to remove root node.
	if (target_idx == 0 && (blocks[0].parent.is_empty() || blocks[0].parent == ".")) {
		return _make_result("Cannot remove the root node of a scene.", true);
	}

	// Collect indices to remove (target + all descendants).
	const String target_full_path = blocks[target_idx].full_path;
	Vector<bool> keep_flags;
	keep_flags.resize(blocks.size());
	int removed_count = 0;
	for (int i = 0; i < blocks.size(); i++) {
		if (_is_descendant_path(target_full_path, blocks[i].full_path)) {
			keep_flags.write[i] = false;
			removed_count++;
		} else {
			keep_flags.write[i] = true;
		}
	}

	// Rebuild content: keep header lines (before first node) and surviving node blocks.
	PackedStringArray all_lines = content.split("\n");
	StringBuilder result_content;

	// Add header lines (everything before the first [node ...]).
	for (int i = 0; i < all_lines.size(); i++) {
		const String line = String(all_lines[i]).strip_edges();
		if (line.begins_with("[node ")) {
			break;
		}
		result_content += String(all_lines[i]) + "\n";
	}

	// Add surviving node blocks.
	for (int i = 0; i < blocks.size(); i++) {
		if (!keep_flags[i]) {
			continue;
		}
		for (int j = 0; j < blocks[i].lines.size(); j++) {
			result_content += blocks[i].lines[j] + "\n";
		}
	}

	Dictionary write_args;
	write_args["path"] = scene_path;
	write_args["content"] = result_content.as_string();
	Dictionary write_result = _write_file(write_args);
	if (write_result.has("is_error")) {
		return write_result;
	}
	return _make_result(String(write_result.get("content", String())) + vformat("\nRemoved node '%s' and %d descendant(s) from %s.", node_path, removed_count - 1, scene_path));
}

// ---------------------------------------------------------------------------
// modify_node_properties: Modify properties of an existing node.
// ---------------------------------------------------------------------------
Dictionary AIToolExecutor::_modify_node_properties(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("modify_node_properties is only available in PROJECT mode.", true);
	}

	const String scene_path = String(p_args.get("scene_path", String())).strip_edges();
	const String node_path = String(p_args.get("node_path", String())).strip_edges();
	const String properties_json = String(p_args.get("properties", String())).strip_edges();
	if (scene_path.is_empty() || node_path.is_empty() || properties_json.is_empty()) {
		return _make_result("modify_node_properties rejected: scene_path, node_path, and properties are required.", true);
	}

	const String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result("No open project root is available.", true);
	}
	String full_path;
	String path_error;
	if (!_resolve_tool_file_path(project_root, scene_path, full_path, path_error)) {
		return _make_result(path_error + "\nPath: " + scene_path, true);
	}
	if (!full_path.to_lower().ends_with(".tscn") || !FileAccess::exists(full_path)) {
		return _make_result("modify_node_properties requires an existing .tscn scene file.\nPath: " + scene_path, true);
	}

	// Parse properties JSON.
	Dictionary properties;
	Variant parsed = JSON::parse_string(properties_json);
	if (parsed.get_type() == Variant::DICTIONARY) {
		properties = parsed;
	}
	if (properties.is_empty()) {
		return _make_result("modify_node_properties: properties must be a non-empty JSON object.", true);
	}

	Error err = OK;
	String content = FileAccess::get_file_as_string(full_path, &err);
	if (err != OK) {
		return _make_result(vformat("Failed to read scene (err=%d).\nPath: %s", (int)err, scene_path), true);
	}

	Vector<AIToolNodeBlock> blocks = _parse_tscn_node_blocks(content);
	int target_idx = -1;
	for (int i = 0; i < blocks.size(); i++) {
		if (blocks[i].full_path == node_path || (node_path == "." && i == 0)) {
			target_idx = i;
			break;
		}
	}
	if (target_idx < 0) {
		return _make_result(vformat("Node '%s' not found in %s.", node_path, scene_path), true);
	}

	// Modify properties in the block's lines.
	AIToolNodeBlock &block = blocks.write[target_idx];
	Array keys = properties.keys();
	int modified_count = 0;
	int added_count = 0;

	for (int k = 0; k < keys.size(); k++) {
		const String prop_name = String(keys[k]).strip_edges();
		const String prop_value = String(properties[keys[k]]).strip_edges();
		if (prop_name.is_empty()) {
			continue;
		}

		bool found = false;
		for (int l = 1; l < block.lines.size(); l++) {
			String line = block.lines[l];
			const String stripped = line.strip_edges();
			const int eq = stripped.find("=");
			if (eq < 0) {
				continue;
			}
			const String key = stripped.substr(0, eq).strip_edges();
			if (key == prop_name) {
				block.lines.write[l] = prop_name + " = " + prop_value;
				found = true;
				modified_count++;
				break;
			}
		}
		if (!found) {
			block.lines.push_back(prop_name + " = " + prop_value);
			added_count++;
		}
	}

	// Rebuild content.
	PackedStringArray all_lines = content.split("\n");
	StringBuilder result_content;

	// Add header lines.
	for (int i = 0; i < all_lines.size(); i++) {
		const String line = String(all_lines[i]).strip_edges();
		if (line.begins_with("[node ")) {
			break;
		}
		result_content += String(all_lines[i]) + "\n";
	}

	// Add all node blocks (with modified target).
	for (int i = 0; i < blocks.size(); i++) {
		for (int j = 0; j < blocks[i].lines.size(); j++) {
			result_content += blocks[i].lines[j] + "\n";
		}
	}

	Dictionary write_args;
	write_args["path"] = scene_path;
	write_args["content"] = result_content.as_string();
	Dictionary write_result = _write_file(write_args);
	if (write_result.has("is_error")) {
		return write_result;
	}
	return _make_result(String(write_result.get("content", String())) + vformat("\nModified node '%s': %d properties updated, %d properties added.", node_path, modified_count, added_count));
}

// ---------------------------------------------------------------------------
// connect_signal: Add a signal connection to a .tscn scene.
// ---------------------------------------------------------------------------
Dictionary AIToolExecutor::_connect_signal(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("connect_signal is only available in PROJECT mode.", true);
	}

	const String scene_path = String(p_args.get("scene_path", String())).strip_edges();
	const String source_node = String(p_args.get("source_node", String())).strip_edges();
	const String signal_name = String(p_args.get("signal_name", String())).strip_edges();
	const String target_node = String(p_args.get("target_node", String())).strip_edges();
	const String method_name = String(p_args.get("method_name", String())).strip_edges();
	if (scene_path.is_empty() || source_node.is_empty() || signal_name.is_empty() || target_node.is_empty() || method_name.is_empty()) {
		return _make_result("connect_signal rejected: scene_path, source_node, signal_name, target_node, and method_name are required.", true);
	}

	const String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result("No open project root is available.", true);
	}
	String full_path;
	String path_error;
	if (!_resolve_tool_file_path(project_root, scene_path, full_path, path_error)) {
		return _make_result(path_error + "\nPath: " + scene_path, true);
	}
	if (!full_path.to_lower().ends_with(".tscn") || !FileAccess::exists(full_path)) {
		return _make_result("connect_signal requires an existing .tscn scene file.\nPath: " + scene_path, true);
	}

	Error err = OK;
	String content = FileAccess::get_file_as_string(full_path, &err);
	if (err != OK) {
		return _make_result(vformat("Failed to read scene (err=%d).\nPath: %s", (int)err, scene_path), true);
	}

	// Verify source and target nodes exist.
	Vector<AIToolNodeBlock> blocks = _parse_tscn_node_blocks(content);
	bool source_found = false;
	bool target_found = false;
	for (int i = 0; i < blocks.size(); i++) {
		if (blocks[i].full_path == source_node || (source_node == "." && i == 0)) {
			source_found = true;
		}
		if (blocks[i].full_path == target_node || (target_node == "." && i == 0)) {
			target_found = true;
		}
	}
	if (!source_found) {
		return _make_result(vformat("Source node '%s' not found in %s.", source_node, scene_path), true);
	}
	if (!target_found) {
		return _make_result(vformat("Target node '%s' not found in %s.", target_node, scene_path), true);
	}

	// Build the connection line.
	const int flags = int(p_args.get("flags", 0));
	String connection_line = vformat("[connection signal=\"%s\" from=\"%s\" to=\"%s\" method=\"%s\"",
			signal_name, source_node, target_node, method_name);
	if (flags > 0) {
		connection_line += vformat(" flags=%d", flags);
	}
	connection_line += "]\n";

	// Append at end of file.
	content = content.strip_edges() + "\n\n" + connection_line;

	Dictionary write_args;
	write_args["path"] = scene_path;
	write_args["content"] = content;
	Dictionary result = _write_file(write_args);
	if (result.has("is_error")) {
		return result;
	}
	return _make_result(String(result.get("content", String())) + vformat("\nConnected signal '%s' from '%s' to '%s.%s'.", signal_name, source_node, target_node, method_name));
}

// ---------------------------------------------------------------------------
// duplicate_node: Duplicate a node and its children in a .tscn scene.
// ---------------------------------------------------------------------------
Dictionary AIToolExecutor::_duplicate_node(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("duplicate_node is only available in PROJECT mode.", true);
	}

	const String scene_path = String(p_args.get("scene_path", String())).strip_edges();
	const String node_path = String(p_args.get("node_path", String())).strip_edges();
	if (scene_path.is_empty() || node_path.is_empty()) {
		return _make_result("duplicate_node rejected: scene_path and node_path are required.", true);
	}

	const String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result("No open project root is available.", true);
	}
	String full_path;
	String path_error;
	if (!_resolve_tool_file_path(project_root, scene_path, full_path, path_error)) {
		return _make_result(path_error + "\nPath: " + scene_path, true);
	}
	if (!full_path.to_lower().ends_with(".tscn") || !FileAccess::exists(full_path)) {
		return _make_result("duplicate_node requires an existing .tscn scene file.\nPath: " + scene_path, true);
	}

	Error err = OK;
	String content = FileAccess::get_file_as_string(full_path, &err);
	if (err != OK) {
		return _make_result(vformat("Failed to read scene (err=%d).\nPath: %s", (int)err, scene_path), true);
	}

	Vector<AIToolNodeBlock> blocks = _parse_tscn_node_blocks(content);
	int target_idx = -1;
	for (int i = 0; i < blocks.size(); i++) {
		if (blocks[i].full_path == node_path || (node_path == "." && i == 0)) {
			target_idx = i;
			break;
		}
	}
	if (target_idx < 0) {
		return _make_result(vformat("Node '%s' not found in %s.", node_path, scene_path), true);
	}

	const String original_name = blocks[target_idx].name;
	const String original_parent = blocks[target_idx].parent;
	const String new_name = _sanitize_scene_node_name(
			String(p_args.get("new_name", original_name + "Copy")).strip_edges(),
			original_name + "Copy");
	const String new_parent = String(p_args.get("new_parent", original_parent)).strip_edges().is_empty()
			? original_parent
			: String(p_args.get("new_parent", original_parent)).strip_edges();

	// Parse optional position offset.
	const Vector3 offset = _parse_vector3_arg(p_args, "position_offset", Vector3());

	// Collect the subtree blocks (target + descendants).
	const String target_full_path = blocks[target_idx].full_path;
	Vector<int> subtree_indices;
	for (int i = target_idx; i < blocks.size(); i++) {
		if (_is_descendant_path(target_full_path, blocks[i].full_path)) {
			subtree_indices.push_back(i);
		}
	}

	// Build duplicated node blocks.
	StringBuilder dup_text;
	for (int s = 0; s < subtree_indices.size(); s++) {
		const AIToolNodeBlock &orig = blocks[subtree_indices[s]];

		// Compute new node name and parent.
		String dup_name;
		String dup_parent;
		if (s == 0) {
			// Root of the duplicated subtree.
			dup_name = new_name;
			dup_parent = new_parent;
		} else {
			// Descendant: replace the original root prefix with new root.
			const String orig_rel = orig.full_path.substr(target_full_path.length());
			dup_name = orig.name;
			if (new_parent.is_empty() || new_parent == ".") {
				dup_parent = new_name + orig_rel.substr(0, orig_rel.rfind("/"));
			} else {
				dup_parent = new_parent + "/" + new_name + orig_rel.substr(0, orig_rel.rfind("/"));
			}
			if (dup_parent.ends_with("/")) {
				dup_parent = dup_parent.substr(0, dup_parent.length() - 1);
			}
		}

		// Build the node line.
		dup_text += vformat("\n[node name=\"%s\" type=\"%s\" parent=\"%s\"]\n", dup_name, orig.type, dup_parent);

		// Copy properties, applying offset to position if this is the root duplicate.
		for (int l = 1; l < orig.lines.size(); l++) {
			String line = orig.lines[l].strip_edges();
			if (line.is_empty() || line.begins_with("[")) {
				continue;
			}
			if (s == 0 && offset != Vector3() && line.begins_with("position =")) {
				// Apply offset to position.
				// Re-parse from the line.
				const int eq = line.find("=");
				if (eq >= 0) {
					const String val = line.substr(eq + 1).strip_edges();
					const Vector<double> vals = _parse_number_list(val);
					if (vals.size() >= 3) {
						Vector3 pos(vals[0], vals[1], vals[2]);
						pos += offset;
						dup_text += "position = " + _format_vector3(pos) + "\n";
						continue;
					}
				}
			}
			// Remap SubResource IDs for the duplicate (append "_copy" suffix).
			if (line.contains("SubResource(\"")) {
				line = line.replace("SubResource(\"", "SubResource(\"dup_");
			}
			dup_text += line + "\n";
		}
	}

	// Also duplicate sub_resources referenced by the subtree.
	// Collect all SubResource IDs referenced in the subtree.
	StringBuilder dup_resources;
	PackedStringArray all_lines = content.split("\n");
	bool in_sub_resource = false;
	String current_sub_id;
	String current_sub_content;
	Vector<String> referenced_ids;

	// Find referenced SubResource IDs.
	for (int s = 0; s < subtree_indices.size(); s++) {
		for (int l = 0; l < blocks[subtree_indices[s]].lines.size(); l++) {
			const String line = blocks[subtree_indices[s]].lines[l];
			int search_from = 0;
			while (true) {
				const int pos = line.find("SubResource(\"", search_from);
				if (pos < 0) {
					break;
				}
				const int id_start = pos + String("SubResource(\"").length();
				const int id_end = line.find("\")", id_start);
				if (id_end >= 0) {
					const String ref_id = line.substr(id_start, id_end - id_start);
					if (!referenced_ids.has(ref_id)) {
						referenced_ids.push_back(ref_id);
					}
				}
				search_from = id_end + 2;
			}
		}
	}

	// Find and duplicate the sub_resources.
	for (int i = 0; i < all_lines.size(); i++) {
		const String line = String(all_lines[i]).strip_edges();
		if (line.begins_with("[sub_resource ")) {
			in_sub_resource = true;
			current_sub_id = _extract_tscn_quoted_attr(line, "id");
			current_sub_content = String(all_lines[i]) + "\n";
			continue;
		}
		if (in_sub_resource) {
			if (line.begins_with("[") && !line.begins_with("[sub_resource")) {
				in_sub_resource = false;
				current_sub_content = String();
				current_sub_id = String();
			} else {
				current_sub_content += String(all_lines[i]) + "\n";
			}
		}
		if (!in_sub_resource && !current_sub_id.is_empty() && referenced_ids.has(current_sub_id)) {
			// Duplicate this sub_resource with a new ID.
			String dup_content = current_sub_content;
			dup_content = dup_content.replace("id=\"" + current_sub_id + "\"", "id=\"dup_" + current_sub_id + "\"");
			dup_resources += dup_content + "\n";
			current_sub_id = String();
			current_sub_content = String();
		}
	}

	// Insert duplicated resources and nodes.
	String insert_resources = dup_resources.as_string();
	String insert_nodes = dup_text.as_string();

	// Insert resources before first [node].
	const int first_node = content.find("\n[node ");
	if (first_node >= 0) {
		content = content.substr(0, first_node + 1) + insert_resources.strip_edges() + "\n\n" + content.substr(first_node + 1);
	} else {
		content = content.strip_edges() + "\n\n" + insert_resources.strip_edges();
	}

	// Append nodes at end.
	content = content.strip_edges() + "\n" + insert_nodes;

	Dictionary write_args;
	write_args["path"] = scene_path;
	write_args["content"] = content;
	Dictionary result = _write_file(write_args);
	if (result.has("is_error")) {
		return result;
	}
	return _make_result(String(result.get("content", String())) + vformat("\nDuplicated node '%s' as '%s' under parent '%s' (%d nodes in subtree).",
			node_path, new_name, new_parent, subtree_indices.size()));
}

// ---------------------------------------------------------------------------
// reparent_node: Move a node and its children to a different parent.
// ---------------------------------------------------------------------------
Dictionary AIToolExecutor::_reparent_node(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("reparent_node is only available in PROJECT mode.", true);
	}

	const String scene_path = String(p_args.get("scene_path", String())).strip_edges();
	const String node_path = String(p_args.get("node_path", String())).strip_edges();
	const String new_parent = String(p_args.get("new_parent", String())).strip_edges();
	if (scene_path.is_empty() || node_path.is_empty() || new_parent.is_empty()) {
		return _make_result("reparent_node rejected: scene_path, node_path, and new_parent are required.", true);
	}

	const String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result("No open project root is available.", true);
	}
	String full_path;
	String path_error;
	if (!_resolve_tool_file_path(project_root, scene_path, full_path, path_error)) {
		return _make_result(path_error + "\nPath: " + scene_path, true);
	}
	if (!full_path.to_lower().ends_with(".tscn") || !FileAccess::exists(full_path)) {
		return _make_result("reparent_node requires an existing .tscn scene file.\nPath: " + scene_path, true);
	}

	Error err = OK;
	String content = FileAccess::get_file_as_string(full_path, &err);
	if (err != OK) {
		return _make_result(vformat("Failed to read scene (err=%d).\nPath: %s", (int)err, scene_path), true);
	}

	Vector<AIToolNodeBlock> blocks = _parse_tscn_node_blocks(content);
	int target_idx = -1;
	for (int i = 0; i < blocks.size(); i++) {
		if (blocks[i].full_path == node_path) {
			target_idx = i;
			break;
		}
	}
	if (target_idx < 0) {
		return _make_result(vformat("Node '%s' not found in %s.", node_path, scene_path), true);
	}

	// Compute the new full path for the target.
	String new_target_full;
	if (new_parent == ".") {
		new_target_full = blocks[target_idx].name;
	} else {
		new_target_full = new_parent + "/" + blocks[target_idx].name;
	}

	// Prevent reparenting to self or descendant.
	if (_is_descendant_path(new_target_full, node_path)) {
		return _make_result(vformat("Cannot reparent '%s' under '%s': would create a circular hierarchy.", node_path, new_parent), true);
	}

	const String old_full_path = blocks[target_idx].full_path;
	int reparented_count = 0;

	// Update parent paths for the target and all descendants.
	for (int i = target_idx; i < blocks.size(); i++) {
		if (!_is_descendant_path(old_full_path, blocks[i].full_path)) {
			continue;
		}
		// Compute relative path from old root.
		String relative;
		if (blocks[i].full_path == old_full_path) {
			relative = blocks[i].name;
		} else {
			relative = blocks[i].full_path.substr(old_full_path.length() + 1); // Skip the '/'
		}

		// Compute new parent for this node.
		String node_new_parent;
		if (blocks[i].full_path == old_full_path) {
			node_new_parent = new_parent;
		} else {
			// The relative path contains intermediate segments.
			const String relative_parent = relative.substr(0, relative.rfind("/"));
			if (new_parent == ".") {
				node_new_parent = blocks[target_idx].name + "/" + relative_parent;
			} else {
				node_new_parent = new_parent + "/" + blocks[target_idx].name + "/" + relative_parent;
			}
		}

		// Update the parent attribute in the node header line.
		String &header = blocks.write[i].lines.write[0];
		const int parent_pos = header.find("parent=\"");
		if (parent_pos >= 0) {
			const int val_start = parent_pos + String("parent=\"").length();
			const int val_end = header.find("\"", val_start);
			if (val_end >= 0) {
				header = header.substr(0, val_start) + node_new_parent + header.substr(val_end);
			}
		}
		blocks.write[i].parent = node_new_parent;
		reparented_count++;
	}

	// Rebuild content.
	PackedStringArray all_lines = content.split("\n");
	StringBuilder result_content;

	// Add header lines.
	for (int i = 0; i < all_lines.size(); i++) {
		const String line = String(all_lines[i]).strip_edges();
		if (line.begins_with("[node ")) {
			break;
		}
		result_content += String(all_lines[i]) + "\n";
	}

	// Add all node blocks.
	for (int i = 0; i < blocks.size(); i++) {
		for (int j = 0; j < blocks[i].lines.size(); j++) {
			result_content += blocks[i].lines[j] + "\n";
		}
	}

	Dictionary write_args;
	write_args["path"] = scene_path;
	write_args["content"] = result_content.as_string();
	Dictionary result = _write_file(write_args);
	if (result.has("is_error")) {
		return result;
	}
	return _make_result(String(result.get("content", String())) + vformat("\nReparented '%s' to '%s' (%d nodes updated).", node_path, new_parent, reparented_count));
}

Dictionary AIToolExecutor::_batch_tools(const Dictionary &p_args) {
	Array operations = p_args.get("operations", Array());
	if (operations.is_empty()) {
		return _make_result("No batch operations provided.", true);
	}

	const int MAX_BATCH_OPERATIONS = 8;
	StringBuilder sb;
	sb += vformat("Batch executed %d operation(s):\n", MIN(operations.size(), MAX_BATCH_OPERATIONS));
	bool has_error = false;

	for (int i = 0; i < operations.size() && i < MAX_BATCH_OPERATIONS; i++) {
		if (operations[i].get_type() != Variant::DICTIONARY) {
			has_error = true;
			sb += vformat("\n--- Operation %d ---\nError: operation must be an object.\n", i + 1);
			continue;
		}

		Dictionary op = operations[i];
		const String name = String(op.get("name", String())).strip_edges();
		if (name.is_empty()) {
			has_error = true;
			sb += vformat("\n--- Operation %d ---\nError: missing tool name.\n", i + 1);
			continue;
		}
		if (name == AIToolNames::BATCH_TOOLS) {
			has_error = true;
			sb += vformat("\n--- Operation %d: %s ---\nError: nested batch_tools calls are not allowed.\n", i + 1, name);
			continue;
		}
		AISettingsData settings = AISettings::load();
		if (settings.context_mode != AIContextMode::ENGINE &&
				(name == AIToolNames::RUN_BUILD ||
						name == AIToolNames::READ_BUILD_LOG ||
						name == AIToolNames::CHECK_BUILD_STATUS ||
						name == AIToolNames::RESTART_ENGINE ||
						name == AIToolNames::FETCH_URL ||
						name == AIToolNames::UPLOAD_CODE ||
						name == AIToolNames::RETURN_TO_PROJECT_MODE)) {
			has_error = true;
			sb += vformat("\n--- Operation %d: %s ---\nError: this tool is only available in engine mode.\n", i + 1, name);
			continue;
		}
		if (settings.context_mode != AIContextMode::PROJECT &&
				(name == AIToolNames::SETUP_ENGINE_WORKSPACE ||
						name == AIToolNames::CHECK_PROJECT_SCRIPTS ||
						name == AIToolNames::CHECK_HTML_PROTOTYPE ||
						name == AIToolNames::CHECK_UI_LAYOUT ||
						name == AIToolNames::BUILD_PROJECT ||
						name == AIToolNames::PACKAGE_PROJECT ||
						name == AIToolNames::CHECK_PACKAGE_STATUS ||
						name == AIToolNames::TEST_PACKAGE ||
						name == AIToolNames::CAPTURE_PACKAGE_SCREENSHOT ||
						name == AIToolNames::REQUEST_ENGINE_CHANGE ||
						name == AIToolNames::ADD_PHYSICS ||
						name == AIToolNames::ADD_ANIMATION ||
						name == AIToolNames::ADD_PARTICLES ||
						name == AIToolNames::ADD_VFX ||
						name == AIToolNames::ADD_CHARACTER_CONTROLLER ||
						name == AIToolNames::REMOVE_NODE ||
						name == AIToolNames::MODIFY_NODE_PROPERTIES ||
						name == AIToolNames::CONNECT_SIGNAL ||
						name == AIToolNames::DUPLICATE_NODE ||
						name == AIToolNames::REPARENT_NODE)) {
			has_error = true;
			sb += vformat("\n--- Operation %d: %s ---\nError: this tool is only available in project mode.\n", i + 1, name);
			continue;
		}

		Dictionary op_args;
		Variant raw_args = op.get("arguments", op.get("args", Dictionary()));
		if (raw_args.get_type() == Variant::DICTIONARY) {
			op_args = raw_args;
		} else if (raw_args.get_type() == Variant::STRING) {
			Variant parsed_args = JSON::parse_string(String(raw_args));
			if (parsed_args.get_type() == Variant::DICTIONARY) {
				op_args = parsed_args;
			}
		}

		Dictionary fn;
		fn["name"] = name;
		fn["arguments"] = JSON::stringify(op_args);

		Dictionary tool_call;
		tool_call["id"] = vformat("batch_%d", i);
		tool_call["type"] = "function";
		tool_call["function"] = fn;

		Dictionary result = execute(tool_call);
		if (result.has("is_error")) {
			has_error = true;
		}

		sb += vformat("\n--- Operation %d: %s ---\n", i + 1, name);
		sb += String(result.get("content", String()));
		sb += "\n";
	}

	if (operations.size() > MAX_BATCH_OPERATIONS) {
		has_error = true;
		sb += vformat("\nBatch stopped after %d operations. Split remaining work into another batch.\n", MAX_BATCH_OPERATIONS);
	}

	return _make_result(sb.as_string(), has_error);
}

struct AIUILayoutNodeInfo {
	String name;
	String path;
	String parent;
	String type;
	int line = 0;
	int order = 0;
	Rect2 rect;
	bool has_rect = false;
	bool parent_is_container = false;
	int mouse_filter = -1;
	int z_index = 0;
};


static bool _parse_tscn_vector2(const String &p_value, Vector2 &r_value) {
	const int open = p_value.find("(");
	const int comma = p_value.find(",", open + 1);
	const int close = p_value.find(")", comma + 1);
	if (open < 0 || comma < 0 || close < 0) {
		return false;
	}
	r_value.x = p_value.substr(open + 1, comma - open - 1).strip_edges().to_float();
	r_value.y = p_value.substr(comma + 1, close - comma - 1).strip_edges().to_float();
	return true;
}

static bool _is_control_type(const String &p_type) {
	return !p_type.is_empty() && ClassDB::class_exists(StringName(p_type)) && ClassDB::is_parent_class(StringName(p_type), SNAME("Control"));
}

static bool _is_container_type(const String &p_type) {
	return !p_type.is_empty() && ClassDB::class_exists(StringName(p_type)) && ClassDB::is_parent_class(StringName(p_type), SNAME("Container"));
}

static bool _is_interactive_control_type(const String &p_type) {
	if (p_type.is_empty() || !ClassDB::class_exists(StringName(p_type))) {
		return false;
	}

	const StringName type_name(p_type);
	return ClassDB::is_parent_class(type_name, SNAME("BaseButton")) ||
			ClassDB::is_parent_class(type_name, SNAME("LineEdit")) ||
			ClassDB::is_parent_class(type_name, SNAME("TextEdit")) ||
			ClassDB::is_parent_class(type_name, SNAME("Slider")) ||
			ClassDB::is_parent_class(type_name, SNAME("SpinBox")) ||
			ClassDB::is_parent_class(type_name, SNAME("OptionButton")) ||
			ClassDB::is_parent_class(type_name, SNAME("MenuButton")) ||
			ClassDB::is_parent_class(type_name, SNAME("ItemList")) ||
			ClassDB::is_parent_class(type_name, SNAME("Tree")) ||
			ClassDB::is_parent_class(type_name, SNAME("GraphEdit")) ||
			ClassDB::is_parent_class(type_name, SNAME("ColorPicker")) ||
			ClassDB::is_parent_class(type_name, SNAME("FileDialog")) ||
			ClassDB::is_parent_class(type_name, SNAME("PopupMenu"));
}

static bool _has_blocking_mouse_filter(const AIUILayoutNodeInfo &p_node) {
	// Godot Control::MouseFilter: STOP = 0, PASS = 1, IGNORE = 2.
	// If absent from the scene file, treat it as potentially blocking so
	// generated decoration must opt out explicitly.
	return p_node.mouse_filter != 2;
}

static bool _is_pass_through_mouse_filter(const AIUILayoutNodeInfo &p_node) {
	return p_node.mouse_filter == 1 || p_node.mouse_filter == 2;
}

static bool _is_modal_surface_candidate(const AIUILayoutNodeInfo &p_node) {
	const String path = p_node.path.to_lower();
	const String type = p_node.type.to_lower();
	const bool modal_name =
			path.contains("settings") ||
			path.contains("options") ||
			path.contains("dialog") ||
			path.contains("popup") ||
			path.contains("modal") ||
			path.contains("pause") ||
			path.contains("inventory") ||
			path.contains("menu");
	const bool surface_type =
			type == "panel" ||
			type == "panelcontainer" ||
			type == "popup" ||
			type == "popupmenu" ||
			type == "popuppanel" ||
			type == "window";
	return modal_name && surface_type;
}

static bool _node_sorts_above(const AIUILayoutNodeInfo &p_a, const AIUILayoutNodeInfo &p_b) {
	if (p_a.z_index != p_b.z_index) {
		return p_a.z_index > p_b.z_index;
	}
	return p_a.order > p_b.order;
}


static bool _parse_tscn_ui_nodes(const String &p_content, Vector<AIUILayoutNodeInfo> &r_nodes, String &r_error) {
	PackedStringArray lines = p_content.split("\n");

	bool in_node = false;
	bool current_is_control = false;
	AIUILayoutNodeInfo current;
	bool has_position = false;
	bool has_size = false;
	bool has_minimum_size = false;
	Vector2 position;
	Vector2 size;
	Vector2 minimum_size;
	bool has_offset_left = false;
	bool has_offset_top = false;
	bool has_offset_right = false;
	bool has_offset_bottom = false;
	real_t offset_left = 0.0;
	real_t offset_top = 0.0;
	real_t offset_right = 0.0;
	real_t offset_bottom = 0.0;

	HashMap<String, String> node_types;
	String root_type;

	auto flush_current = [&]() {
		if (!in_node) {
			return;
		}
		if (current.parent.is_empty() && root_type.is_empty()) {
			root_type = current.type;
		}
		node_types[current.path] = current.type;
		if (!current_is_control) {
			return;
		}

		const real_t left = has_offset_left ? offset_left : (has_position ? position.x : 0.0);
		const real_t top = has_offset_top ? offset_top : (has_position ? position.y : 0.0);
		real_t width = 0.0;
		real_t height = 0.0;

		if (has_size) {
			width = size.x;
			height = size.y;
		} else {
			if (has_offset_right) {
				width = offset_right - left;
			}
			if (has_offset_bottom) {
				height = offset_bottom - top;
			}
		}

		if (has_minimum_size) {
			width = MAX(width, minimum_size.x);
			height = MAX(height, minimum_size.y);
		}

		if (width > 0.0 && height > 0.0) {
			current.rect = Rect2(left, top, width, height);
			current.has_rect = true;
		}
		r_nodes.push_back(current);
	};

	for (int i = 0; i < lines.size(); i++) {
		const String line = String(lines[i]).strip_edges();
		if (line.begins_with("[node ")) {
			flush_current();

			in_node = true;
			current_is_control = false;
			current = AIUILayoutNodeInfo();
			has_position = false;
			has_size = false;
			has_minimum_size = false;
			position = Vector2();
			size = Vector2();
			minimum_size = Vector2();
			has_offset_left = false;
			has_offset_top = false;
			has_offset_right = false;
			has_offset_bottom = false;
			offset_left = 0.0;
			offset_top = 0.0;
			offset_right = 0.0;
			offset_bottom = 0.0;

			const String name = _extract_tscn_quoted_attr(line, "name");
			current.name = name;
			current.type = _extract_tscn_quoted_attr(line, "type");
			current.parent = _extract_tscn_quoted_attr(line, "parent");
			current.path = _scene_node_path(current.parent, name);
			current.line = i + 1;
			current.order = i;
			current_is_control = _is_control_type(current.type);
			if (name.is_empty()) {
				r_error = vformat("Could not parse node name at line %d.", i + 1);
				return false;
			}
			continue;
		}

		if (!in_node || !current_is_control) {
			continue;
		}

		const int eq = line.find("=");
		if (eq < 0) {
			continue;
		}
		const String key = line.substr(0, eq).strip_edges();
		const String value = line.substr(eq + 1).strip_edges();

		if (key == "position") {
			has_position = _parse_tscn_vector2(value, position);
		} else if (key == "size") {
			has_size = _parse_tscn_vector2(value, size);
		} else if (key == "custom_minimum_size") {
			has_minimum_size = _parse_tscn_vector2(value, minimum_size);
		} else if (key == "offset_left") {
			has_offset_left = true;
			offset_left = value.to_float();
		} else if (key == "offset_top") {
			has_offset_top = true;
			offset_top = value.to_float();
		} else if (key == "offset_right") {
			has_offset_right = true;
			offset_right = value.to_float();
		} else if (key == "offset_bottom") {
			has_offset_bottom = true;
			offset_bottom = value.to_float();
		} else if (key == "mouse_filter") {
			if (value.is_valid_int()) {
				current.mouse_filter = value.to_int();
			} else if (value.contains("MOUSE_FILTER_IGNORE")) {
				current.mouse_filter = 2;
			} else if (value.contains("MOUSE_FILTER_PASS")) {
				current.mouse_filter = 1;
			} else if (value.contains("MOUSE_FILTER_STOP")) {
				current.mouse_filter = 0;
			}
		} else if (key == "z_index") {
			current.z_index = value.to_int();
		}
	}

	flush_current();

	for (int i = 0; i < r_nodes.size(); i++) {
		if (r_nodes[i].parent == ".") {
			r_nodes.write[i].parent_is_container = _is_container_type(root_type);
			continue;
		}
		if (!node_types.has(r_nodes[i].parent)) {
			continue;
		}
		r_nodes.write[i].parent_is_container = _is_container_type(node_types[r_nodes[i].parent]);
	}

	return true;
}

Dictionary AIToolExecutor::_check_ui_layout(const Dictionary &p_args) {
	AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("check_ui_layout is only available in PROJECT mode.", true);
	}

	Array paths = p_args.get("paths", Array());
	if (paths.is_empty() && p_args.has("path")) {
		paths.push_back(String(p_args.get("path", String())));
	}
	if (paths.is_empty() && p_args.has("paths") && p_args["paths"].get_type() == Variant::STRING) {
		paths.push_back(String(p_args["paths"]));
	}
	if (paths.is_empty()) {
		return _make_result("No scene paths provided.", true);
	}

	const String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result("No open project root is available for UI layout checking.", true);
	}

	StringBuilder result;
	bool has_error = false;
	bool has_warning = false;
	const int MAX_WARNINGS_PER_SCENE = 20;

	for (int p = 0; p < paths.size(); p++) {
		String rel_path = paths[p];
		String full_path;
		String path_error;
		result += vformat("=== %s ===\n", rel_path);

		if (!_resolve_tool_file_path(project_root, rel_path, full_path, path_error)) {
			has_error = true;
			result += "Error: " + path_error + "\n";
			continue;
		}
		if (!full_path.ends_with(".tscn")) {
			has_error = true;
			result += "Error: check_ui_layout only inspects .tscn scene files.\n";
			continue;
		}
		if (!FileAccess::exists(full_path)) {
			has_error = true;
			result += "Error: file not found.\n";
			continue;
		}

		Error err = OK;
		const String content = FileAccess::get_file_as_string(full_path, &err);
		if (err != OK) {
			has_error = true;
			result += vformat("Error: failed to read file (err=%d).\n", (int)err);
			continue;
		}

		Vector<AIUILayoutNodeInfo> nodes;
		String parse_error;
		if (!_parse_tscn_ui_nodes(content, nodes, parse_error)) {
			has_error = true;
			result += "Error: " + parse_error + "\n";
			continue;
		}

		int warnings = 0;
		int skipped_managed = 0;
		int skipped_without_rect = 0;
		for (int i = 0; i < nodes.size(); i++) {
			if (!nodes[i].has_rect) {
				skipped_without_rect++;
				continue;
			}
			if (_is_modal_surface_candidate(nodes[i]) && _is_pass_through_mouse_filter(nodes[i])) {
				has_warning = true;
				warnings++;
				result += vformat(
						"Possible click-through modal %d: node '%s' (line %d, %s) looks like a settings/dialog/menu surface but uses mouse_filter=%d, so clicks may pass through to UI underneath.\n",
						warnings,
						nodes[i].path,
						nodes[i].line,
						nodes[i].type,
						nodes[i].mouse_filter);
				result += "  Suggestion: set the modal surface or its full-screen blocker to mouse_filter = 0 (Stop), keep decorative children at mouse_filter = 2 (Ignore), and put the modal content above the blocker with a higher z_index or later scene order.\n";
				if (warnings >= MAX_WARNINGS_PER_SCENE) {
					result += vformat("Stopped after %d warnings for this scene; fix these first, then run check_ui_layout again.\n", MAX_WARNINGS_PER_SCENE);
					break;
				}
			}
			if (nodes[i].parent_is_container) {
				skipped_managed++;
				continue;
			}
			for (int j = i + 1; j < nodes.size(); j++) {
				if (nodes[i].parent != nodes[j].parent) {
					continue;
				}
				if (!nodes[j].has_rect) {
					continue;
				}
				if (nodes[j].parent_is_container) {
					continue;
				}
				if (!nodes[i].rect.intersects(nodes[j].rect, false)) {
					continue;
				}

				has_warning = true;
				warnings++;
				const AIUILayoutNodeInfo &upper = _node_sorts_above(nodes[i], nodes[j]) ? nodes[i] : nodes[j];
				const AIUILayoutNodeInfo &lower = _node_sorts_above(nodes[i], nodes[j]) ? nodes[j] : nodes[i];
				const bool possible_click_blocker = _is_interactive_control_type(lower.type) && !_is_interactive_control_type(upper.type) && _has_blocking_mouse_filter(upper);
				if (possible_click_blocker) {
					result += vformat(
							"Possible click blocker %d: parent='%s', upper node '%s' (line %d, %s) overlaps interactive node '%s' (line %d, %s) and may intercept mouse clicks.\n",
							warnings,
							lower.parent.is_empty() ? "." : lower.parent,
							upper.path,
							upper.line,
							upper.type,
							lower.path,
							lower.line,
							lower.type);
				} else {
					result += vformat(
							"Possible overlap %d: parent='%s', nodes='%s' (line %d, %s) and '%s' (line %d, %s).\n",
							warnings,
							nodes[i].parent.is_empty() ? "." : nodes[i].parent,
							nodes[i].path,
							nodes[i].line,
							nodes[i].type,
							nodes[j].path,
							nodes[j].line,
							nodes[j].type);
				}
				result += vformat(
						"  Rects: %s at (%.1f, %.1f, %.1f x %.1f), %s at (%.1f, %.1f, %.1f x %.1f).\n",
						nodes[i].path,
						nodes[i].rect.position.x,
						nodes[i].rect.position.y,
						nodes[i].rect.size.x,
						nodes[i].rect.size.y,
						nodes[j].path,
						nodes[j].rect.position.x,
						nodes[j].rect.position.y,
						nodes[j].rect.size.x,
						nodes[j].rect.size.y);
				if (possible_click_blocker) {
					result += "  Suggestion: move decorative/background controls behind interactive controls, put them in a separate lower CanvasLayer, reduce their z_index, or set mouse_filter = 2 (Ignore) on non-interactive overlay controls.\n";
				} else {
					result += "  Suggestion: put these controls under a VBoxContainer/HBoxContainer/GridContainer, adjust z_index intentionally, or split the parent into non-overlapping MarginContainer/PanelContainer regions.\n";
				}
				if (warnings >= MAX_WARNINGS_PER_SCENE) {
					result += vformat("Stopped after %d warnings for this scene; fix these first, then run check_ui_layout again.\n", MAX_WARNINGS_PER_SCENE);
					break;
				}
			}
			if (warnings >= MAX_WARNINGS_PER_SCENE) {
				break;
			}
		}

		if (warnings == 0) {
			result += "No obvious fixed-rectangle sibling Control overlaps found.\n";
		}
		result += vformat("Inspected %d Control node(s). Skipped %d container-managed node(s) and %d node(s) without enough static rectangle data.\n\n",
				nodes.size(), skipped_managed, skipped_without_rect);
	}

	if (has_warning) {
		result += "UI layout check found likely overlap or click-blocking risks. Read the warnings, adjust the scene layout or mouse_filter values, then run check_ui_layout again before finishing the UI task.";
	}

	return _make_result(result.as_string(), has_error);
}

struct AI3DSceneNodeInfo {
	String name;
	String path;
	String parent;
	String type;
	int line = 0;
	bool has_position = false;
	bool has_transform = false;
	bool has_mesh = false;
	bool has_shape = false;
};


static bool _parse_tscn_3d_nodes(const String &p_content, Vector<AI3DSceneNodeInfo> &r_nodes, String &r_error) {
	PackedStringArray lines = p_content.split("\n");
	bool in_node = false;
	AI3DSceneNodeInfo current;

	auto flush_current = [&]() {
		if (in_node) {
			r_nodes.push_back(current);
		}
	};

	for (int i = 0; i < lines.size(); i++) {
		const String line = String(lines[i]).strip_edges();
		if (line.begins_with("[node ")) {
			flush_current();
			in_node = true;
			current = AI3DSceneNodeInfo();
			current.name = _extract_tscn_quoted_attr(line, "name");
			current.type = _extract_tscn_quoted_attr(line, "type");
			current.parent = _extract_tscn_quoted_attr(line, "parent");
			current.path = _scene_node_path(current.parent, current.name);
			current.line = i + 1;
			if (current.name.is_empty()) {
				r_error = vformat("Could not parse node name at line %d.", i + 1);
				return false;
			}
			continue;
		}
		if (!in_node) {
			continue;
		}
		if (line.begins_with("position =")) {
			current.has_position = true;
		} else if (line.begins_with("transform =")) {
			current.has_transform = true;
		} else if (line.begins_with("mesh =")) {
			current.has_mesh = true;
		} else if (line.begins_with("shape =")) {
			current.has_shape = true;
		}
	}
	flush_current();
	return true;
}

Dictionary AIToolExecutor::_create_3d_scene(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("create_3d_scene is only available in PROJECT mode.", true);
	}

	const String path = String(p_args.get("path", String())).strip_edges();
	if (path.is_empty()) {
		return _make_result("create_3d_scene rejected: path is required.", true);
	}
	if (!path.to_lower().ends_with(".tscn")) {
		return _make_result("create_3d_scene only creates .tscn scene files.\nPath: " + path, true);
	}

	const String root_name = _sanitize_scene_node_name(String(p_args.get("root_name", "World")), "World");
	const bool include_camera = !p_args.has("include_camera") || bool(p_args.get("include_camera", true));
	const bool include_lighting = !p_args.has("include_lighting") || bool(p_args.get("include_lighting", true));
	const bool include_floor = !p_args.has("include_floor") || bool(p_args.get("include_floor", true));

	int load_steps = 1;
	if (include_floor) {
		load_steps += 2;
	}
	StringBuilder content;
	content += vformat("[gd_scene load_steps=%d format=3]\n\n", load_steps);
	if (include_floor) {
		content += "[sub_resource type=\"PlaneMesh\" id=\"PlaneMesh_floor\"]\n";
		content += "size = Vector2(20, 20)\n\n";
		content += "[sub_resource type=\"StandardMaterial3D\" id=\"StandardMaterial3D_floor\"]\n";
		content += "albedo_color = Color(0.45, 0.48, 0.42, 1)\n";
		content += "roughness = 0.85\n\n";
	}
	content += vformat("[node name=\"%s\" type=\"Node3D\"]\n\n", root_name);
	if (include_lighting) {
		content += "[node name=\"Sun\" type=\"DirectionalLight3D\" parent=\".\"]\n";
		content += "rotation = Vector3(-0.8727, -0.5236, 0)\n";
		content += "light_energy = 1.8\n";
		content += "shadow_enabled = true\n\n";
	}
	if (include_camera) {
		content += "[node name=\"Camera3D\" type=\"Camera3D\" parent=\".\"]\n";
		content += "position = Vector3(0, 4, 8)\n";
		content += "rotation = Vector3(-0.4636, 0, 0)\n";
		content += "current = true\n\n";
	}
	if (include_floor) {
		content += "[node name=\"Floor\" type=\"MeshInstance3D\" parent=\".\"]\n";
		content += "mesh = SubResource(\"PlaneMesh_floor\")\n";
		content += "surface_material_override/0 = SubResource(\"StandardMaterial3D_floor\")\n";
	}

	Dictionary write_args;
	write_args["path"] = path;
	write_args["content"] = content.as_string();
	Dictionary result = _write_file(write_args);
	if (result.has("is_error")) {
		return result;
	}
	String message = String(result.get("content", String()));
	message += "\nCreated starter 3D scene. Next recommended step: call check_3d_scene on this path after adding gameplay objects.";
	return _make_result(message);
}

Dictionary AIToolExecutor::_add_3d_object(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("add_3d_object is only available in PROJECT mode.", true);
	}

	const String scene_path = String(p_args.get("scene_path", String())).strip_edges();
	const String node_name = _sanitize_scene_node_name(String(p_args.get("name", String())), "MeshInstance3D");
	if (scene_path.is_empty() || String(p_args.get("name", String())).strip_edges().is_empty()) {
		return _make_result("add_3d_object rejected: scene_path and name are required.", true);
	}

	const String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result("No open project root is available for 3D scene editing.", true);
	}
	String full_path;
	String path_error;
	if (!_resolve_tool_file_path(project_root, scene_path, full_path, path_error)) {
		return _make_result(path_error + "\nPath: " + scene_path, true);
	}
	if (!full_path.to_lower().ends_with(".tscn") || !FileAccess::exists(full_path)) {
		return _make_result("add_3d_object requires an existing .tscn scene file.\nPath: " + scene_path, true);
	}

	Error err = OK;
	String content = FileAccess::get_file_as_string(full_path, &err);
	if (err != OK) {
		return _make_result(vformat("Failed to read scene before adding 3D object (err=%d).\nPath: %s", (int)err, scene_path), true);
	}

	const String mesh_type = String(p_args.get("mesh_type", "box")).strip_edges().to_lower();
	const String mesh_id = _unique_tscn_id("Mesh", node_name);
	const String material_id = _unique_tscn_id("Material", node_name);
	String resources = _mesh_resource_for_type(mesh_type, mesh_id, p_args);
	resources += "\n";
	resources += vformat("[sub_resource type=\"StandardMaterial3D\" id=\"%s\"]\n", material_id);
	resources += "albedo_color = " + _format_color_arg(p_args, "color", "Color(0.72, 0.72, 0.68, 1)") + "\n";
	resources += "roughness = 0.65\n";

	const String parent = String(p_args.get("parent", ".")).strip_edges().is_empty() ? "." : String(p_args.get("parent", ".")).strip_edges();
	const Vector3 position = _parse_vector3_arg(p_args, "position", Vector3());
	const Vector3 rotation = _parse_vector3_arg(p_args, "rotation_degrees", Vector3(), true);
	const Vector3 scale = _parse_vector3_arg(p_args, "scale", Vector3(1, 1, 1));

	String node_text;
	node_text += vformat("\n[node name=\"%s\" type=\"MeshInstance3D\" parent=\"%s\"]\n", node_name, parent);
	node_text += "position = " + _format_vector3(position) + "\n";
	if (!rotation.is_zero_approx()) {
		node_text += "rotation = " + _format_vector3(rotation) + "\n";
	}
	if (!scale.is_equal_approx(Vector3(1, 1, 1))) {
		node_text += "scale = " + _format_vector3(scale) + "\n";
	}
	node_text += vformat("mesh = SubResource(\"%s\")\n", mesh_id);
	node_text += vformat("surface_material_override/0 = SubResource(\"%s\")\n", material_id);

	content = _increment_tscn_load_steps(content, 2);
	content = _insert_tscn_subresources(content, resources);
	content = content.strip_edges() + "\n" + node_text;

	Dictionary write_args;
	write_args["path"] = scene_path;
	write_args["content"] = content;
	Dictionary result = _write_file(write_args);
	if (result.has("is_error")) {
		return result;
	}
	return _make_result(String(result.get("content", String())) + vformat("\nAdded MeshInstance3D '%s' (%s). Run check_3d_scene on %s after object edits.", node_name, mesh_type, scene_path));
}

Dictionary AIToolExecutor::_add_3d_light(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("add_3d_light is only available in PROJECT mode.", true);
	}

	const String scene_path = String(p_args.get("scene_path", String())).strip_edges();
	const String node_name = _sanitize_scene_node_name(String(p_args.get("name", String())), "Light3D");
	if (scene_path.is_empty() || String(p_args.get("name", String())).strip_edges().is_empty()) {
		return _make_result("add_3d_light rejected: scene_path and name are required.", true);
	}

	const String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result("No open project root is available for 3D light editing.", true);
	}
	String full_path;
	String path_error;
	if (!_resolve_tool_file_path(project_root, scene_path, full_path, path_error)) {
		return _make_result(path_error + "\nPath: " + scene_path, true);
	}
	if (!full_path.to_lower().ends_with(".tscn") || !FileAccess::exists(full_path)) {
		return _make_result("add_3d_light requires an existing .tscn scene file.\nPath: " + scene_path, true);
	}

	Error err = OK;
	String content = FileAccess::get_file_as_string(full_path, &err);
	if (err != OK) {
		return _make_result(vformat("Failed to read scene before adding 3D light (err=%d).\nPath: %s", (int)err, scene_path), true);
	}

	String light_type = String(p_args.get("light_type", "directional")).strip_edges().to_lower();
	String node_type = "DirectionalLight3D";
	if (light_type == "omni" || light_type == "point") {
		node_type = "OmniLight3D";
	} else if (light_type == "spot" || light_type == "spotlight") {
		node_type = "SpotLight3D";
	} else {
		light_type = "directional";
	}

	const String parent = String(p_args.get("parent", ".")).strip_edges().is_empty() ? "." : String(p_args.get("parent", ".")).strip_edges();
	const Vector3 position = _parse_vector3_arg(p_args, "position", light_type == "directional" ? Vector3() : Vector3(0, 4, 2));
	const Vector3 rotation = _parse_vector3_arg(p_args, "rotation_degrees", _vector3_degrees_to_radians(light_type == "directional" ? Vector3(-50, -30, 0) : Vector3(-55, 0, 0)), true);
	const double energy = double(p_args.get("energy", 1.5));
	const bool shadows = !p_args.has("shadows") || bool(p_args.get("shadows", true));

	String node_text;
	node_text += vformat("\n[node name=\"%s\" type=\"%s\" parent=\"%s\"]\n", node_name, node_type, parent);
	if (light_type != "directional" || !position.is_zero_approx()) {
		node_text += "position = " + _format_vector3(position) + "\n";
	}
	if (!rotation.is_zero_approx()) {
		node_text += "rotation = " + _format_vector3(rotation) + "\n";
	}
	node_text += "light_color = " + _format_color_arg(p_args, "color", "Color(1, 1, 1, 1)") + "\n";
	node_text += vformat("light_energy = %.4f\n", energy);
	node_text += vformat("shadow_enabled = %s\n", shadows ? "true" : "false");
	if (light_type == "omni") {
		node_text += vformat("omni_range = %.4f\n", double(p_args.get("range", 8.0)));
	} else if (light_type == "spot") {
		node_text += vformat("spot_range = %.4f\n", double(p_args.get("range", 12.0)));
		node_text += vformat("spot_angle = %.4f\n", double(p_args.get("spot_angle", 45.0)));
	}

	content = content.strip_edges() + "\n" + node_text;
	Dictionary write_args;
	write_args["path"] = scene_path;
	write_args["content"] = content;
	Dictionary result = _write_file(write_args);
	if (result.has("is_error")) {
		return result;
	}
	return _make_result(String(result.get("content", String())) + vformat("\nAdded %s '%s'. Run check_3d_scene on %s after lighting edits.", node_type, node_name, scene_path));
}

Dictionary AIToolExecutor::_check_3d_scene(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("check_3d_scene is only available in PROJECT mode.", true);
	}

	Array paths = p_args.get("paths", Array());
	if (paths.is_empty() && p_args.has("path")) {
		paths.push_back(String(p_args.get("path", String())));
	}
	if (paths.is_empty() && p_args.has("paths") && p_args["paths"].get_type() == Variant::STRING) {
		paths.push_back(String(p_args["paths"]));
	}
	if (paths.is_empty()) {
		return _make_result("No 3D scene paths provided.", true);
	}

	const String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result("No open project root is available for 3D scene checking.", true);
	}

	StringBuilder result;
	bool has_error = false;
	bool has_warning = false;
	for (int p = 0; p < paths.size(); p++) {
		const String rel_path = String(paths[p]);
		String full_path;
		String path_error;
		result += vformat("=== %s ===\n", rel_path);
		if (!_resolve_tool_file_path(project_root, rel_path, full_path, path_error)) {
			has_error = true;
			result += "Error: " + path_error + "\n";
			continue;
		}
		if (!full_path.to_lower().ends_with(".tscn")) {
			has_error = true;
			result += "Error: check_3d_scene only inspects .tscn scene files.\n";
			continue;
		}
		if (!FileAccess::exists(full_path)) {
			has_error = true;
			result += "Error: file not found.\n";
			continue;
		}

		Error err = OK;
		const String content = FileAccess::get_file_as_string(full_path, &err);
		if (err != OK) {
			has_error = true;
			result += vformat("Error: failed to read file (err=%d).\n", (int)err);
			continue;
		}

		Vector<AI3DSceneNodeInfo> nodes;
		String parse_error;
		if (!_parse_tscn_3d_nodes(content, nodes, parse_error)) {
			has_error = true;
			result += "Error: " + parse_error + "\n";
			continue;
		}

		int node3d_count = 0;
		int mesh_count = 0;
		int light_count = 0;
		int camera_count = 0;
		int current_camera_count = content.count("current = true");
		int collision_shape_count = 0;
		int physics_body_count = 0;
		bool has_world_environment = false;
		bool has_navigation = false;
		bool root_is_3d = false;

		for (int i = 0; i < nodes.size(); i++) {
			const String type = nodes[i].type;
			if (i == 0 && (type == "Node3D" || (ClassDB::class_exists(StringName(type)) && ClassDB::is_parent_class(StringName(type), SNAME("Node3D"))))) {
				root_is_3d = true;
			}
			if (ClassDB::class_exists(StringName(type)) && ClassDB::is_parent_class(StringName(type), SNAME("Node3D"))) {
				node3d_count++;
			}
			if (type == "MeshInstance3D") {
				mesh_count++;
			} else if (type == "Camera3D") {
				camera_count++;
			} else if (type == "DirectionalLight3D" || type == "OmniLight3D" || type == "SpotLight3D") {
				light_count++;
			} else if (type == "CollisionShape3D") {
				collision_shape_count++;
			} else if (type == "StaticBody3D" || type == "RigidBody3D" || type == "CharacterBody3D" || type == "Area3D") {
				physics_body_count++;
			} else if (type == "WorldEnvironment") {
				has_world_environment = true;
			} else if (type == "NavigationRegion3D") {
				has_navigation = true;
			}
		}

		result += vformat("Nodes: %d total, %d Node3D-derived, %d MeshInstance3D, %d light(s), %d Camera3D, %d CollisionShape3D, %d physics/area body node(s).\n",
				nodes.size(), node3d_count, mesh_count, light_count, camera_count, collision_shape_count, physics_body_count);

		bool scene_has_warning = false;
		if (!root_is_3d) {
			has_warning = true;
			scene_has_warning = true;
			result += "Warning: scene root does not appear to be Node3D-derived. For a 3D gameplay scene, prefer a Node3D root.\n";
		}
		if (mesh_count == 0) {
			has_warning = true;
			scene_has_warning = true;
			result += "Warning: no MeshInstance3D nodes found, so the scene may have no visible 3D geometry.\n";
		}
		if (camera_count == 0) {
			has_warning = true;
			scene_has_warning = true;
			result += "Warning: no Camera3D found. Add one or ensure another scene supplies the active camera.\n";
		} else if (current_camera_count == 0) {
			has_warning = true;
			scene_has_warning = true;
			result += "Warning: Camera3D exists but no `current = true` camera was detected.\n";
		}
		if (light_count == 0 && !has_world_environment) {
			has_warning = true;
			scene_has_warning = true;
			result += "Warning: no 3D light or WorldEnvironment found. Add lighting so generated objects are visible.\n";
		}
		if (mesh_count > 0 && collision_shape_count == 0 && physics_body_count == 0) {
			has_warning = true;
			scene_has_warning = true;
			result += "Warning: visible meshes exist but no CollisionShape3D or physics body nodes were found. Add collision for walkable floors, blockers, pickups, or interactable props when gameplay requires it.\n";
		}
		if (has_navigation) {
			result += "NavigationRegion3D detected; remember to assign/bake navigation data when NPC pathfinding is required.\n";
		}
		if (!scene_has_warning) {
			result += "No obvious 3D scene basics are missing.\n";
		}
		result += "\n";
	}

	if (has_warning) {
		result += "3D scene check found likely setup gaps. Fix the warnings, then run check_3d_scene again before considering the 3D scene ready.";
	}
	return _make_result(result.as_string(), has_error);
}

Dictionary AIToolExecutor::_build_project(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("build_project is only available in PROJECT mode. Use run_build/check_build_status for engine source builds.", true);
	}

	const String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result("No open project root was detected. Open a project with project.jundot before building project code.", true);
	}

	String project_path = String(p_args.get("project_path", String())).strip_edges().replace("\\", "/");
	if (project_path.is_empty() && p_args.has("path")) {
		project_path = String(p_args.get("path", String())).strip_edges().replace("\\", "/");
	}
	if (project_path.is_empty()) {
		bool ambiguous = false;
		project_path = _find_single_root_dotnet_project(project_root, ambiguous);
		if (ambiguous) {
			return _make_result("build_project found more than one .csproj/.sln in the project root. Call build_project again with project_path set to the intended file.", true);
		}
	}
	if (project_path.is_empty()) {
		return _make_result("build_project could not find a .csproj or .sln in the project root. Provide project_path relative to the open project root.", true);
	}

	String full_project_path;
	String path_error;
	if (!_resolve_tool_file_path(project_root, project_path, full_project_path, path_error)) {
		return _make_result(path_error + "\nProject path: " + project_path, true);
	}
	const String lower_project_path = full_project_path.to_lower();
	if (!lower_project_path.ends_with(".csproj") && !lower_project_path.ends_with(".sln")) {
		return _make_result("build_project only accepts .csproj or .sln files inside the open project root.\nProject path: " + project_path, true);
	}
	if (!FileAccess::exists(full_project_path)) {
		return _make_result("build_project project file not found: " + project_path, true);
	}

	String configuration = String(p_args.get("configuration", String())).strip_edges();
	if (!configuration.is_empty()) {
		const String lowered = configuration.to_lower();
		if (lowered != "debug" && lowered != "release") {
			return _make_result("build_project configuration must be Debug, Release, or omitted.", true);
		}
		configuration = lowered == "release" ? "Release" : "Debug";
	}

	String target = String(p_args.get("target", String())).strip_edges();
	if (target.is_empty()) {
		target = "Build";
	}
	const String target_lower = target.to_lower();
	if (target_lower != "build" && target_lower != "rebuild" && target_lower != "clean") {
		return _make_result("build_project target must be Build, Rebuild, Clean, or omitted.", true);
	}
	target = target_lower == "rebuild" ? "Rebuild" : (target_lower == "clean" ? "Clean" : "Build");

	List<String> dotnet_args;
	dotnet_args.push_back("build");
	dotnet_args.push_back(full_project_path);
	dotnet_args.push_back("--nologo");
	dotnet_args.push_back("-t:" + target);
	if (!configuration.is_empty()) {
		dotnet_args.push_back("-c");
		dotnet_args.push_back(configuration);
	}
	if ((bool)p_args.get("no_restore", false)) {
		dotnet_args.push_back("--no-restore");
	}

	String output;
	int exit_code = -1;
	const String build_root = full_project_path.get_base_dir();
	const Error err = _run_command_in_root(build_root, "dotnet", dotnet_args, output, exit_code);

	StringBuilder result;
	result += "Tool: build_project\n";
	result += "Program: dotnet build\n";
	result += "Project: " + project_path + "\n";
	result += "Workdir: " + build_root + "\n";
	result += "Target: " + target + "\n";
	if (!configuration.is_empty()) {
		result += "Configuration: " + configuration + "\n";
	}
	result += vformat("Exit code: %d\n", exit_code);
	if (err != OK) {
		result += vformat("Failed to start dotnet build (err=%d). Ensure the .NET SDK is installed and available on PATH.\n", (int)err);
	}
	if (!output.strip_edges().is_empty()) {
		result += "\n--- build output ---\n";
		result += _truncate_tool_output(output, 20000).strip_edges();
		result += "\n";
	}
	if (err == OK && exit_code == 0) {
		result += "\nProject build passed.";
	} else {
		result += "\nProject build failed. Read the compiler output above, edit the affected project files, then run build_project again.";
	}

	return _make_result(result.as_string(), err != OK || exit_code != 0);
}

Dictionary AIToolExecutor::_reload_cpp_hot_module(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("reload_cpp_hot_module is only available in PROJECT mode.", true);
	}

	const String extension_path_arg = String(p_args.get("extension_path", String())).strip_edges();
	if (extension_path_arg.is_empty()) {
		return _make_result("reload_cpp_hot_module rejected: extension_path is required.", true);
	}

	const String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result("No open project root was detected. Open a project with project.jundot before reloading a C++ hot module.", true);
	}

	String full_path;
	String path_error;
	if (!_resolve_tool_file_path(project_root, extension_path_arg, full_path, path_error)) {
		return _make_result(path_error + "\nExtension path: " + extension_path_arg, true);
	}
	if (!full_path.ends_with(".gdextension")) {
		return _make_result("reload_cpp_hot_module only accepts .gdextension files.\nExtension path: " + extension_path_arg, true);
	}
	if (!FileAccess::exists(full_path)) {
		return _make_result("GDExtension file not found: " + extension_path_arg, true);
	}

	String normalized_arg = extension_path_arg.replace("\\", "/").strip_edges();
	String resource_path = normalized_arg.begins_with("res://") ? normalized_arg.simplify_path() : "res://" + normalized_arg.trim_prefix("./").simplify_path();

	GDExtensionManager *manager = GDExtensionManager::get_singleton();
	if (!manager) {
		return _make_result("GDExtensionManager is not available in this editor session.", true);
	}

	const bool was_loaded = manager->is_extension_loaded(resource_path);
	GDExtensionManager::LoadStatus status = was_loaded ? manager->reload_extension(resource_path) : manager->load_extension(resource_path);

	String result = "Tool: reload_cpp_hot_module\n";
	result += "Extension: " + resource_path + "\n";
	result += String("Operation: ") + (was_loaded ? "reload" : "load") + "\n";

	switch (status) {
		case GDExtensionManager::LOAD_STATUS_OK:
			result += was_loaded ? "State: reloaded\nC++ hot module reloaded successfully." : "State: loaded\nC++ hot module loaded successfully.";
			return _make_result(result);
		case GDExtensionManager::LOAD_STATUS_ALREADY_LOADED:
			result += "State: already_loaded\nThe extension was already loaded. Call reload_cpp_hot_module again after rebuilding its native library.";
			return _make_result(result);
		case GDExtensionManager::LOAD_STATUS_NOT_LOADED:
			result += "State: not_loaded\nThe extension is not currently loaded. Check the .gdextension path and project settings, then retry.";
			return _make_result(result, true);
		case GDExtensionManager::LOAD_STATUS_NEEDS_RESTART:
			result += "State: needs_restart\nThe extension changed in a way that cannot be hot-reloaded. Restart the editor before validating this module.";
			return _make_result(result, true);
		case GDExtensionManager::LOAD_STATUS_FAILED:
		default:
			result += "State: failed\nHot module reload failed. Ensure the native library was rebuilt, the .gdextension file has configuration/reloadable=true, and the extension classes did not change parent type or incompatible layout.";
			return _make_result(result, true);
	}
}

static bool _hot_module_program_allowed(const String &p_program) {
	const String program = p_program.get_file().to_lower();
	return program == "scons" ||
			program == "scons.bat" ||
			program == "python" ||
			program == "python.exe" ||
			program == "python3" ||
			program == "cmake" ||
			program == "cmake.exe" ||
			program == "ninja" ||
			program == "ninja.exe" ||
			program == "msbuild" ||
			program == "msbuild.exe" ||
			program == "dotnet" ||
			program == "dotnet.exe";
}

static bool _hot_module_arg_allowed(const String &p_arg) {
	const String arg = p_arg.strip_edges().replace("\\", "/");
	if (arg.contains("&&") || arg.contains("||") || arg.contains(";") || arg.contains("|") || arg.contains(">") || arg.contains("<")) {
		return false;
	}
	if (arg == ".." || arg.begins_with("../") || arg.contains("/../")) {
		return false;
	}
	for (int i = 0; i + 2 < arg.length(); i++) {
		const char32_t c = arg[i];
		if (((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) && arg[i + 1] == ':' && arg[i + 2] == '/') {
			return false;
		}
	}
	return true;
}

Dictionary AIToolExecutor::_build_cpp_hot_module(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("build_cpp_hot_module is only available in PROJECT mode.", true);
	}

	const String extension_path = String(p_args.get("extension_path", String())).strip_edges();
	const String program = String(p_args.get("program", String())).strip_edges();
	if (extension_path.is_empty() || program.is_empty()) {
		return _make_result("build_cpp_hot_module rejected: extension_path and program are required.", true);
	}
	if (program.is_absolute_path() || !_hot_module_program_allowed(program)) {
		return _make_result("build_cpp_hot_module rejected: program must be a known build tool name such as scons, python, cmake, ninja, msbuild, or dotnet.", true);
	}

	const String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result("No open project root was detected. Open a project with project.jundot before building a C++ hot module.", true);
	}

	String workdir_arg = String(p_args.get("workdir", ".")).strip_edges();
	if (workdir_arg.is_empty()) {
		workdir_arg = ".";
	}
	String workdir = project_root;
	if (workdir_arg != ".") {
		String path_error;
		if (!_resolve_tool_file_path(project_root, workdir_arg, workdir, path_error)) {
			return _make_result(path_error + "\nWorkdir: " + workdir_arg, true);
		}
	}
	if (!DirAccess::dir_exists_absolute(workdir)) {
		return _make_result("build_cpp_hot_module workdir does not exist: " + workdir_arg, true);
	}

	Array raw_args = p_args.get("args", Array());
	List<String> command_args;
	for (int i = 0; i < raw_args.size(); i++) {
		const String arg = String(raw_args[i]).strip_edges();
		if (!_hot_module_arg_allowed(arg)) {
			return _make_result("build_cpp_hot_module rejected unsafe command argument: " + arg, true);
		}
		command_args.push_back(arg);
	}

	String build_output;
	int exit_code = -1;
	const Error err = _run_command_in_root(workdir, program, command_args, build_output, exit_code);

	String result = "Tool: build_cpp_hot_module\n";
	result += "Program: " + program + "\n";
	result += "Workdir: " + workdir + "\n";
	result += vformat("Exit code: %d\n", exit_code);
	if (err != OK) {
		result += vformat("Failed to start C++ hot module build (err=%d).\n", (int)err);
	}
	if (!build_output.is_empty()) {
		const int MAX_OUTPUT = 30000;
		const String visible_output = build_output.length() > MAX_OUTPUT ? build_output.substr(build_output.length() - MAX_OUTPUT) : build_output;
		result += "\n--- build output ---\n" + visible_output + "\n";
	}

	if (err != OK || exit_code != 0) {
		result += "\nC++ hot module build failed. Fix the compiler output above, then call build_cpp_hot_module again.";
		return _make_result(result, true);
	}

	Dictionary reload_args;
	reload_args["extension_path"] = extension_path;
	Dictionary reload_result = _reload_cpp_hot_module(reload_args);
	result += "\nC++ hot module build passed.\n\n";
	result += String(reload_result.get("content", String()));
	return _make_result(result, reload_result.has("is_error"));
}

Dictionary AIToolExecutor::_package_project(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("package_project is only available in PROJECT mode.", true);
	}

	Dictionary options;
	const String target = String(p_args.get("target", "editor")).strip_edges().to_lower();
	if (target != "editor" && target != "editor.dev" && target != "template_release" && target != "template_debug") {
		return _make_result("package_project target must be editor, editor.dev, template_release, or template_debug.", true);
	}
	const String platform = String(p_args.get("platform", "windows")).strip_edges().to_lower();
	if (platform != "windows" && platform != "android" && platform != "linuxbsd" && platform != "macos" && platform != "web" && platform != "ios") {
		return _make_result("package_project platform must be windows, android, linuxbsd, macos, web, or ios.", true);
	}
	const String arch = String(p_args.get("arch", "x86_64")).strip_edges().to_lower();
	if (arch != "x86_64" && arch != "x86_32" && arch != "arm64") {
		return _make_result("package_project arch must be x86_64, x86_32, or arm64.", true);
	}
	const int jobs = (int)p_args.get("jobs", 0);
	if (jobs < 0 || jobs > 1024) {
		return _make_result("package_project jobs must be between 0 and 1024.", true);
	}

	options["target"] = target;
	options["platform"] = platform;
	options["arch"] = arch;
	options["skip_build"] = (bool)p_args.get("skip_build", false);
	options["mono"] = (bool)p_args.get("mono", false);
	options["auto_update_version"] = (bool)p_args.get("auto_update_version", true);
	options["generate_update_manifest"] = (bool)p_args.get("generate_update_manifest", true);
	options["jobs"] = jobs;
	options["extra_scons_args"] = String(p_args.get("extra_scons_args", String())).strip_edges();

	const Error write_err = AIBuildBridge::write_build_request(options);
	if (write_err != OK) {
		return _make_result(vformat("package_project failed to write AI build request. Error: %d", (int)write_err), true);
	}

	const Error launch_err = AIBuildBridge::launch_package_builder();
	if (launch_err != OK) {
		return _make_result(vformat("package_project failed to launch PackageBuilder. Error: %d\nPackageBuilder path: %s", (int)launch_err, AIBuildBridge::detect_repo_root().path_join("tools/PackageBuilder")), true);
	}

	String result = "Tool: package_project\n";
	result += "State: started\n";
	result += "Target: " + target + "\n";
	result += "Platform: " + platform + "\n";
	result += "Arch: " + arch + "\n";
	result += String("Skip build: ") + ((bool)options["skip_build"] ? "true" : "false") + "\n";
	result += "PackageBuilder is running in unattended AI build mode.\n";
	result += "Call check_package_status until it returns success or failed before telling the user that packaging is complete.";
	return _make_result(result);
}

Dictionary AIToolExecutor::_check_package_status(const Dictionary &p_args) {
	(void)p_args;
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("check_package_status is only available in PROJECT mode.", true);
	}

	String state;
	String message;
	String zip_path;
	String manifest_path;
	String build_log_path;
	if (AIBuildBridge::get_ai_build_status(state, message, zip_path, manifest_path, build_log_path)) {
		String result = "Tool: check_package_status\n";
		result += "State: " + state + "\n";
		if (!message.is_empty()) {
			result += "Message: " + message + "\n";
		}
		if (!zip_path.is_empty()) {
			result += "Package zip: " + zip_path + "\n";
		}
		if (!manifest_path.is_empty()) {
			result += "Update manifest: " + manifest_path + "\n";
		}
		if (!build_log_path.is_empty()) {
			result += "Build log: " + build_log_path + "\n";
		}
		if (state == "queued") {
			result += "Packaging has been queued but PackageBuilder has not reported progress yet. Check again later.";
		} else if (state == "running") {
			result += "Packaging is still running. Check again later.";
		} else if (state == "failed") {
			result += "Packaging failed. Inspect the build log or reported message, fix the issue, then call package_project again.";
		}
		return _make_result(result, state == "failed");
	}

	String version;
	String zip;
	String manifest;
	String log;
	if (AIBuildBridge::get_latest_build_info(version, zip, manifest, log)) {
		String result = "Tool: check_package_status\n";
		result += "State: success\n";
		result += "Message: Latest package record found.\n";
		result += "Version: " + version + "\n";
		result += "Package zip: " + zip + "\n";
		if (!manifest.is_empty()) {
			result += "Update manifest: " + manifest + "\n";
		}
		if (!log.is_empty()) {
			result += "Build log: " + log + "\n";
		}
		return _make_result(result);
	}

	return _make_result("Tool: check_package_status\nState: not_started\nNo AI package status or package history was found. Call package_project after project validation passes.", true);
}

Dictionary AIToolExecutor::_test_package(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("test_package is only available in PROJECT mode.", true);
	}

	String version;
	String package_dir;
	String exe_path;
	String zip_path;
	String build_log_path;
	if (!AIBuildBridge::get_latest_package_launch_info(version, package_dir, exe_path, zip_path, build_log_path)) {
		return _make_result("test_package could not find a packaged executable in the latest PackageBuilder records. Run package_project and wait for check_package_status success first.", true);
	}

	const String exe_name = exe_path.get_file().to_lower();
	if (exe_name.contains("template_")) {
		return _make_result("test_package cannot directly launch template runtime packages; they require exported project data. Build/package the editor target or export a runnable game package first.", true);
	}

	String arg_text = String(p_args.get("args", "--version")).strip_edges();
	if (arg_text.is_empty()) {
		arg_text = "--version";
	}
	if (arg_text.find("&&") >= 0 || arg_text.find("||") >= 0 || arg_text.find(";") >= 0 || arg_text.find("|") >= 0) {
		return _make_result("test_package args must be simple executable arguments, not shell operators.", true);
	}

	List<String> args;
	Vector<String> split_args = arg_text.split(" ", false);
	for (int i = 0; i < split_args.size(); i++) {
		const String item = split_args[i].strip_edges();
		if (!item.is_empty()) {
			args.push_back(item);
		}
	}

	String output;
	int exit_code = -1;
	const String workdir = package_dir.is_empty() ? exe_path.get_base_dir() : package_dir;
	const Error err = _run_command_in_root(workdir, exe_path, args, output, exit_code);

	StringBuilder result;
	result += "Tool: test_package\n";
	result += "Package smoke test: packaged executable startup\n";
	result += "Version: " + version + "\n";
	result += "Executable: " + exe_path + "\n";
	if (!zip_path.is_empty()) {
		result += "Package zip: " + zip_path + "\n";
	}
	result += "Workdir: " + workdir + "\n";
	result += "Args: " + arg_text + "\n";
	result += vformat("Exit code: %d\n", exit_code);
	if (err != OK) {
		result += vformat("Failed to start packaged executable (err=%d).\n", (int)err);
	}
	if (!output.strip_edges().is_empty()) {
		result += "\n--- package test output ---\n";
		result += _truncate_tool_output(output, 12000).strip_edges();
		result += "\n";
	}
	if (err == OK && exit_code == 0) {
		result += "\nPackage smoke test passed. The package can now be handed to the user with the zip path and validation summary.";
	} else {
		result += "\nPackage smoke test failed. Inspect the output, fix the issue, rebuild/repackage, and run test_package again before handoff.";
	}
	if (!build_log_path.is_empty()) {
		result += "\nBuild log: " + build_log_path;
	}
	return _make_result(result.as_string(), err != OK || exit_code != 0);
}

Dictionary AIToolExecutor::_capture_package_screenshot(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("capture_package_screenshot is only available in PROJECT mode.", true);
	}

	String version;
	String package_dir;
	String exe_path;
	String zip_path;
	String build_log_path;
	if (!AIBuildBridge::get_latest_package_launch_info(version, package_dir, exe_path, zip_path, build_log_path)) {
		return _make_result("capture_package_screenshot could not find a packaged executable in the latest PackageBuilder records. Run package_project and wait for check_package_status success first.", true);
	}
	if (!FileAccess::exists(exe_path)) {
		return _make_result("capture_package_screenshot packaged executable was not found: " + exe_path, true);
	}

	const String exe_name = exe_path.get_file().to_lower();
	if (exe_name.contains("template_")) {
		return _make_result("capture_package_screenshot cannot directly capture template runtime packages; they require exported project data. Build/package a runnable executable first.", true);
	}

	const String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result("capture_package_screenshot failed: no open project root is available.", true);
	}

	const String out_dir = project_root.path_join(".JundotAI/package_screenshots");
	Error mkdir_err = DirAccess::make_dir_recursive_absolute(out_dir);
	if (mkdir_err != OK) {
		return _make_result(vformat("capture_package_screenshot failed: could not create screenshot directory (err=%d): %s", (int)mkdir_err, out_dir), true);
	}

	const String file_name = _safe_ai_screenshot_file_name(String(p_args.get("file_name", "")));
	String saved_path = out_dir.path_join(file_name);
	if (FileAccess::exists(saved_path)) {
		const String base = saved_path.get_basename();
		int suffix = 1;
		while (FileAccess::exists(saved_path)) {
			saved_path = base + "-" + itos(suffix++) + ".png";
		}
	}

	String arg_text = String(p_args.get("args", String())).strip_edges();
	if (arg_text.find("&&") >= 0 || arg_text.find("||") >= 0 || arg_text.find(";") >= 0 || arg_text.find("|") >= 0) {
		return _make_result("capture_package_screenshot args must be simple executable arguments, not shell operators.", true);
	}

	const int wait_ms = CLAMP((int)(double)p_args.get("wait_ms", 3000.0), 0, 60000);
	const bool close_after = bool(p_args.get("close_after", true));
	const String workdir = package_dir.is_empty() ? exe_path.get_base_dir() : package_dir;

#ifdef WINDOWS_ENABLED
	const String ps_path = out_dir.path_join("capture_package_screenshot.ps1");
	String ps_script = R"PS(
$ErrorActionPreference = 'Stop'
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Capture {
  [StructLayout(LayoutKind.Sequential)]
  public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
}
"@
Add-Type -AssemblyName System.Drawing

$ExePath = $args[0]
$Workdir = $args[1]
$OutPath = $args[2]
$WaitMs = [int]$args[3]
$CloseAfter = [bool]::Parse($args[4])
$ArgText = if ($args.Count -ge 6) { $args[5] } else { "" }

$startInfo = @{
  FilePath = $ExePath
  WorkingDirectory = $Workdir
  PassThru = $true
}
if ($ArgText.Trim().Length -gt 0) {
  $startInfo.ArgumentList = $ArgText
}
$proc = Start-Process @startInfo
$deadline = [DateTime]::UtcNow.AddMilliseconds([Math]::Max($WaitMs, 1000))
while ([DateTime]::UtcNow -lt $deadline) {
  Start-Sleep -Milliseconds 100
  $proc.Refresh()
  if ($proc.HasExited) { break }
  if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { break }
}
if ($WaitMs -gt 0) {
  Start-Sleep -Milliseconds ([Math]::Min($WaitMs, 2000))
  $proc.Refresh()
}
if ($proc.HasExited) {
  throw "Packaged executable exited before a window screenshot could be captured. ExitCode=$($proc.ExitCode)"
}
if ($proc.MainWindowHandle -eq [IntPtr]::Zero) {
  throw "No main window handle was available for the packaged executable."
}
[Win32Capture]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 250
$rect = New-Object Win32Capture+RECT
if (-not [Win32Capture]::GetWindowRect($proc.MainWindowHandle, [ref]$rect)) {
  throw "GetWindowRect failed."
}
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
if ($width -le 0 -or $height -le 0) {
  throw "Invalid window rectangle ${width}x${height}."
}
$bitmap = New-Object System.Drawing.Bitmap($width, $height)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
try {
  $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
  $bitmap.Save($OutPath, [System.Drawing.Imaging.ImageFormat]::Png)
} finally {
  $graphics.Dispose()
  $bitmap.Dispose()
}
if ($CloseAfter -and -not $proc.HasExited) {
  $proc.CloseMainWindow() | Out-Null
  if (-not $proc.WaitForExit(1500)) {
    $proc.Kill()
  }
}
[pscustomobject]@{
  status = "captured"
  executable = $ExePath
  output = $OutPath
  width = $width
  height = $height
  pid = $proc.Id
  closed = $CloseAfter
} | ConvertTo-Json -Depth 4
)PS";

	Ref<FileAccess> ps_file = FileAccess::open(ps_path, FileAccess::WRITE);
	if (ps_file.is_null()) {
		return _make_result("capture_package_screenshot failed to write PowerShell capture script: " + ps_path, true);
	}
	ps_file->store_string(ps_script);
	ps_file.unref();

	List<String> ps_args;
	ps_args.push_back("-NoProfile");
	ps_args.push_back("-ExecutionPolicy");
	ps_args.push_back("Bypass");
	ps_args.push_back("-File");
	ps_args.push_back(ps_path);
	ps_args.push_back(exe_path);
	ps_args.push_back(workdir);
	ps_args.push_back(saved_path);
	ps_args.push_back(itos(wait_ms));
	ps_args.push_back(close_after ? "true" : "false");
	ps_args.push_back(arg_text);

	String output;
	int exit_code = -1;
	const Error err = OS::get_singleton()->execute("powershell", ps_args, &output, &exit_code, true);
	if (err != OK || exit_code != 0 || !FileAccess::exists(saved_path)) {
		StringBuilder failed;
		failed += "Tool: capture_package_screenshot\n";
		failed += "Result: failed\n";
		failed += "Version: " + version + "\n";
		failed += "Executable: " + exe_path + "\n";
		failed += "Workdir: " + workdir + "\n";
		failed += vformat("Wait ms: %d\n", wait_ms);
		failed += vformat("PowerShell err: %d\nPowerShell exit code: %d\n", (int)err, exit_code);
		if (!output.strip_edges().is_empty()) {
			failed += "\n--- capture output ---\n";
			failed += _truncate_tool_output(output, 12000).strip_edges();
			failed += "\n";
		}
		return _make_result(failed.as_string(), true);
	}

	const String rel_path = saved_path.replace(project_root.replace("\\", "/") + "/", "").replace("\\", "/");
	StringBuilder result;
	result += "Tool: capture_package_screenshot\n";
	result += "Result: captured\n";
	result += "Version: " + version + "\n";
	result += "Executable: " + exe_path + "\n";
	if (!zip_path.is_empty()) {
		result += "Package zip: " + zip_path + "\n";
	}
	result += "Workdir: " + workdir + "\n";
	result += "Saved PNG: " + saved_path + "\n";
	result += "Project path: " + rel_path + "\n";
	result += vformat("Wait ms: %d\n", wait_ms);
	result += close_after ? "Closed launched process: true\n" : "Closed launched process: false\n";
	if (!output.strip_edges().is_empty()) {
		result += "\n--- capture output ---\n";
		result += _truncate_tool_output(output, 6000).strip_edges();
		result += "\n";
	}
	result += "This packaged-build screenshot will be attached as an image_url in the next AI tool-continuation request when the active backend supports multimodal message content. Inspect it for visual semantic errors such as entities rendered in the wrong gameplay region.";
	Dictionary tool_result = _make_result(result.as_string());
	tool_result["image_path"] = saved_path;
	tool_result["image_mime_type"] = "image/png";
	tool_result["image_description"] = "Packaged executable window screenshot captured by capture_package_screenshot.";
	return tool_result;
#else
	return _make_result("capture_package_screenshot is currently implemented for Windows packaged executables only.", true);
#endif
}

static MouseButton _parse_ai_mouse_button(const String &p_button) {
	const String button = p_button.strip_edges().to_lower();
	if (button == "right") {
		return MouseButton::RIGHT;
	}
	if (button == "middle") {
		return MouseButton::MIDDLE;
	}
	return MouseButton::LEFT;
}

static String _ai_mouse_button_name(MouseButton p_button) {
	return p_button == MouseButton::RIGHT ? "right" : (p_button == MouseButton::MIDDLE ? "middle" : "left");
}

static String _format_ai_runtime_action_result(const String &p_tool_name, const Dictionary &p_result) {
	StringBuilder output;
	output += "Tool: " + p_tool_name + "\n";
	output += String((bool)p_result.get("ok", false) ? "Result: passed\n" : "Result: failed\n");
	output += "Message: " + String(p_result.get("message", "")) + "\n";
	Dictionary details = p_result.get("details", Dictionary());
	if (!details.is_empty()) {
		output += "Details: " + JSON::stringify(details) + "\n";
	}
	return output.as_string().strip_edges();
}

static bool _wait_for_ai_runtime_action(GameViewDebugger *p_debugger, int64_t p_request_id, int p_wait_ms, Dictionary &r_result) {
	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + (uint64_t)MAX(p_wait_ms, 0);
	while (OS::get_singleton()->get_ticks_msec() <= deadline) {
		if (p_debugger->pop_ai_action_result(p_request_id, r_result)) {
			return true;
		}
		OS::get_singleton()->delay_usec(50 * 1000);
	}
	return p_debugger->pop_ai_action_result(p_request_id, r_result);
}

static bool _wait_for_ai_screenshot(GameViewDebugger *p_debugger, int64_t p_request_id, int p_wait_ms, Dictionary &r_result) {
	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + (uint64_t)MAX(p_wait_ms, 0);
	while (OS::get_singleton()->get_ticks_msec() <= deadline) {
		if (p_debugger->pop_ai_screenshot_result(p_request_id, r_result)) {
			return true;
		}
		OS::get_singleton()->delay_usec(50 * 1000);
	}
	return p_debugger->pop_ai_screenshot_result(p_request_id, r_result);
}

static String _safe_ai_screenshot_file_name(const String &p_name) {
	String file_name = p_name.strip_edges().get_file();
	if (file_name.is_empty()) {
		String datetime = Time::get_singleton()->get_datetime_string_from_system().remove_chars("-T:");
		file_name = "game-screenshot-" + datetime + "-" + itos(Time::get_singleton()->get_ticks_usec()) + ".png";
	}
	if (!file_name.to_lower().ends_with(".png")) {
		file_name += ".png";
	}
	file_name = file_name.replace("\\", "_").replace("/", "_").replace(":", "_");
	return file_name;
}

Dictionary AIToolExecutor::_play_scene(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("play_scene is only available in PROJECT mode.", true);
	}
	if (!EditorRunBar::get_singleton()) {
		return _make_result("play_scene failed: editor run bar is not available.", true);
	}

	const String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result("play_scene failed: no open project root is available.", true);
	}

	String scene_path = String(p_args.get("scene_path", String())).strip_edges().replace("\\", "/");
	if (scene_path.is_empty()) {
		EditorRunBar::get_singleton()->play_main_scene(false, Vector<String>());
		return _make_result("Started playing the project main scene. Wait briefly for the debugger session to connect before using click_ui_position.");
	}

	String full_path;
	String path_error;
	if (!_resolve_tool_file_path(project_root, scene_path, full_path, path_error)) {
		return _make_result("play_scene failed: " + path_error, true);
	}

	const String lower_path = full_path.to_lower();
	if (!lower_path.ends_with(".tscn") && !lower_path.ends_with(".scn")) {
		return _make_result("play_scene only accepts .tscn or .scn scene files inside the open project root.\nScene path: " + scene_path, true);
	}
	if (!FileAccess::exists(full_path)) {
		return _make_result("play_scene scene file not found: " + scene_path, true);
	}

	String resource_path = scene_path;
	if (!resource_path.begins_with("res://")) {
		resource_path = "res://" + resource_path.trim_prefix("./");
	}

	EditorRunBar::get_singleton()->play_custom_scene(resource_path, Vector<String>());
	return _make_result("Started playing scene: " + resource_path + "\nWait briefly for the debugger session to connect before using click_ui_position.");
}

Dictionary AIToolExecutor::_click_ui_position(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("click_ui_position is only available in PROJECT mode.", true);
	}
	if (!EditorRunBar::get_singleton() || !EditorRunBar::get_singleton()->is_playing()) {
		return _make_result("click_ui_position failed: no game scene is currently playing. Call play_scene first.", true);
	}

	GameViewDebugger *debugger = GameViewDebugger::get_singleton();
	if (!debugger) {
		return _make_result("click_ui_position failed: Game View debugger is not available.", true);
	}

	const double x = p_args.get("x", 0.0);
	const double y = p_args.get("y", 0.0);
	if (x < 0.0 || y < 0.0) {
		return _make_result("click_ui_position requires non-negative viewport coordinates.", true);
	}

	const MouseButton button = _parse_ai_mouse_button(String(p_args.get("button", "left")));
	const int sent_count = debugger->click_ui_position(Vector2(x, y), button);
	if (sent_count <= 0) {
		return _make_result("click_ui_position failed: the game is playing, but no active debugger session is connected yet. Wait a moment after play_scene, then try again.", true);
	}

	const int wait_ms = CLAMP((int)(double)p_args.get("wait_ms", 250.0), 0, 5000);
	if (wait_ms > 0) {
		OS::get_singleton()->delay_usec(wait_ms * 1000);
	}

	return _make_result(vformat("Sent %s mouse click to running game at viewport position (%.1f, %.1f). Active debugger session(s): %d.",
			_ai_mouse_button_name(button),
			x,
			y,
			sent_count));
}

Dictionary AIToolExecutor::_click_ui_node(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("click_ui_node is only available in PROJECT mode.", true);
	}
	if (!EditorRunBar::get_singleton() || !EditorRunBar::get_singleton()->is_playing()) {
		return _make_result("click_ui_node failed: no game scene is currently playing. Call play_scene first.", true);
	}

	GameViewDebugger *debugger = GameViewDebugger::get_singleton();
	if (!debugger) {
		return _make_result("click_ui_node failed: Game View debugger is not available.", true);
	}

	const String node_path_text = String(p_args.get("node_path", "")).strip_edges();
	if (node_path_text.is_empty()) {
		return _make_result("click_ui_node requires node_path.", true);
	}

	const MouseButton button = _parse_ai_mouse_button(String(p_args.get("button", "left")));
	const int64_t request_id = debugger->begin_ai_action();
	const int sent_count = debugger->click_ui_node(NodePath(node_path_text), button, request_id);
	if (sent_count <= 0) {
		return _make_result("click_ui_node failed: the game is playing, but no active debugger session is connected yet. Wait a moment after play_scene, then try again.", true);
	}

	const int wait_ms = CLAMP((int)(double)p_args.get("wait_ms", 500.0), 0, 5000);
	Dictionary action_result;
	if (_wait_for_ai_runtime_action(debugger, request_id, wait_ms, action_result)) {
		const bool ok = action_result.get("ok", false);
		return _make_result(_format_ai_runtime_action_result(AIToolNames::CLICK_UI_NODE, action_result), !ok);
	}

	return _make_result(vformat("Sent %s click request for runtime Control node '%s'. Active debugger session(s): %d.\nNo runtime acknowledgement was received within %d ms; call assert_no_runtime_errors and, if needed, retry after the debugger has processed messages.",
			_ai_mouse_button_name(button),
			node_path_text,
			sent_count,
			wait_ms));
}

Dictionary AIToolExecutor::_assert_node_visible(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("assert_node_visible is only available in PROJECT mode.", true);
	}
	if (!EditorRunBar::get_singleton() || !EditorRunBar::get_singleton()->is_playing()) {
		return _make_result("assert_node_visible failed: no game scene is currently playing. Call play_scene first.", true);
	}

	GameViewDebugger *debugger = GameViewDebugger::get_singleton();
	if (!debugger) {
		return _make_result("assert_node_visible failed: Game View debugger is not available.", true);
	}

	const String node_path_text = String(p_args.get("node_path", "")).strip_edges();
	if (node_path_text.is_empty()) {
		return _make_result("assert_node_visible requires node_path.", true);
	}

	const int64_t request_id = debugger->begin_ai_action();
	const int sent_count = debugger->assert_node_visible(NodePath(node_path_text), request_id);
	if (sent_count <= 0) {
		return _make_result("assert_node_visible failed: the game is playing, but no active debugger session is connected yet. Wait a moment after play_scene, then try again.", true);
	}

	const int wait_ms = CLAMP((int)(double)p_args.get("wait_ms", 500.0), 0, 5000);
	Dictionary action_result;
	if (_wait_for_ai_runtime_action(debugger, request_id, wait_ms, action_result)) {
		const bool ok = action_result.get("ok", false);
		return _make_result(_format_ai_runtime_action_result(AIToolNames::ASSERT_NODE_VISIBLE, action_result), !ok);
	}

	return _make_result(vformat("Sent visibility assertion request for runtime node '%s'. Active debugger session(s): %d.\nNo runtime acknowledgement was received within %d ms; retry after the debugger has processed messages.",
			node_path_text,
			sent_count,
			wait_ms));
}

Dictionary AIToolExecutor::_assert_no_runtime_errors(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("assert_no_runtime_errors is only available in PROJECT mode.", true);
	}
	if (!EditorRunBar::get_singleton() || !EditorRunBar::get_singleton()->is_playing()) {
		return _make_result("assert_no_runtime_errors failed: no game scene is currently playing. Call play_scene first.", true);
	}

	EditorDebuggerNode *debugger_node = EditorDebuggerNode::get_singleton();
	if (!debugger_node) {
		return _make_result("assert_no_runtime_errors failed: editor debugger node is not available.", true);
	}

	int session_count = 0;
	int active_session_count = 0;
	int error_count = 0;
	int warning_count = 0;
	for (int i = 0;; i++) {
		ScriptEditorDebugger *debugger = debugger_node->get_debugger(i);
		if (!debugger) {
			break;
		}
		session_count++;
		if (debugger->is_session_active()) {
			active_session_count++;
			error_count += debugger->get_error_count();
			warning_count += debugger->get_warning_count();
		}
	}

	if (active_session_count <= 0) {
		return _make_result("assert_no_runtime_errors failed: no active debugger session is connected yet. Wait a moment after play_scene, then try again.", true);
	}

	const bool allow_warnings = p_args.get("allow_warnings", true);
	const bool failed = error_count > 0 || (!allow_warnings && warning_count > 0);
	StringBuilder result;
	result += "Tool: assert_no_runtime_errors\n";
	result += failed ? "Result: failed\n" : "Result: passed\n";
	result += vformat("Debugger sessions: %d total, %d active\n", session_count, active_session_count);
	result += vformat("Runtime errors: %d\nRuntime warnings: %d\n", error_count, warning_count);
	if (failed) {
		result += "Runtime problems are present after the scene interaction. Inspect the Debugger Errors panel or reproduce the click, fix the reported script/runtime issue, then run the runtime assertion again.";
	} else {
		result += allow_warnings ? "No runtime errors are currently reported by the active debugger session(s)." : "No runtime errors or warnings are currently reported by the active debugger session(s).";
	}

	return _make_result(result.as_string(), failed);
}

Dictionary AIToolExecutor::_capture_game_screenshot(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("capture_game_screenshot is only available in PROJECT mode.", true);
	}
	if (!EditorRunBar::get_singleton() || !EditorRunBar::get_singleton()->is_playing()) {
		return _make_result("capture_game_screenshot failed: no game scene is currently playing. Call play_scene first.", true);
	}

	GameViewDebugger *debugger = GameViewDebugger::get_singleton();
	if (!debugger) {
		return _make_result("capture_game_screenshot failed: Game View debugger is not available.", true);
	}

	const String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result("capture_game_screenshot failed: no open project root is available.", true);
	}

	const int64_t request_id = debugger->request_ai_screenshot();
	if (request_id < 0) {
		return _make_result("capture_game_screenshot failed: the game is playing, but no active debugger session is connected yet. Wait a moment after play_scene, then try again.", true);
	}

	const int wait_ms = CLAMP((int)(double)p_args.get("wait_ms", 1000.0), 0, 10000);
	Dictionary screenshot_result;
	if (!_wait_for_ai_screenshot(debugger, request_id, wait_ms, screenshot_result)) {
		return _make_result(vformat("capture_game_screenshot timed out after %d ms waiting for the running game to return a screenshot. Wait a moment after play_scene, then try again.", wait_ms), true);
	}

	const String temp_path = String(screenshot_result.get("path", String()));
	if (temp_path.is_empty() || !FileAccess::exists(temp_path)) {
		return _make_result("capture_game_screenshot failed: the running game returned no readable PNG path.", true);
	}

	const String out_dir = project_root.path_join(".JundotAI/runtime_screenshots");
	Error mkdir_err = DirAccess::make_dir_recursive_absolute(out_dir);
	if (mkdir_err != OK) {
		return _make_result(vformat("capture_game_screenshot failed: could not create screenshot directory (err=%d): %s", (int)mkdir_err, out_dir), true);
	}

	const String file_name = _safe_ai_screenshot_file_name(String(p_args.get("file_name", "")));
	String saved_path = out_dir.path_join(file_name);
	if (FileAccess::exists(saved_path)) {
		const String base = saved_path.get_basename();
		int suffix = 1;
		while (FileAccess::exists(saved_path)) {
			saved_path = base + "-" + itos(suffix++) + ".png";
		}
	}

	Error copy_err = DirAccess::copy_absolute(temp_path, saved_path);
	if (copy_err != OK) {
		return _make_result(vformat("capture_game_screenshot failed: could not copy screenshot to project directory (err=%d).\nTemp path: %s\nTarget path: %s", (int)copy_err, temp_path, saved_path), true);
	}

	const String rel_path = saved_path.replace(project_root.replace("\\", "/") + "/", "").replace("\\", "/");
	StringBuilder result;
	result += "Tool: capture_game_screenshot\n";
	result += "Result: captured\n";
	result += "Saved PNG: " + saved_path + "\n";
	result += "Project path: " + rel_path + "\n";
	result += vformat("Size: %d x %d\n", (int)screenshot_result.get("width", 0), (int)screenshot_result.get("height", 0));
	result += "This screenshot will be attached as an image_url in the next AI tool-continuation request when the active backend supports multimodal message content.";
	Dictionary tool_result = _make_result(result.as_string());
	tool_result["image_path"] = saved_path;
	tool_result["image_mime_type"] = "image/png";
	tool_result["image_description"] = "Runtime game viewport screenshot captured by capture_game_screenshot.";
	return tool_result;
}

Dictionary AIToolExecutor::_capture_runtime_ui_snapshot(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("capture_runtime_ui_snapshot is only available in PROJECT mode.", true);
	}
	if (!EditorRunBar::get_singleton() || !EditorRunBar::get_singleton()->is_playing()) {
		return _make_result("capture_runtime_ui_snapshot failed: no game scene is currently playing. Call play_scene first.", true);
	}

	GameViewDebugger *debugger = GameViewDebugger::get_singleton();
	if (!debugger) {
		return _make_result("capture_runtime_ui_snapshot failed: Game View debugger is not available.", true);
	}

	const int max_nodes = CLAMP((int)(double)p_args.get("max_nodes", 200.0), 1, 1000);
	const bool include_invisible = p_args.get("include_invisible", false);
	const int64_t request_id = debugger->begin_ai_action();
	const int sent_count = debugger->capture_runtime_ui_snapshot(max_nodes, include_invisible, request_id);
	if (sent_count <= 0) {
		return _make_result("capture_runtime_ui_snapshot failed: the game is playing, but no active debugger session is connected yet. Wait a moment after play_scene, then try again.", true);
	}

	const int wait_ms = CLAMP((int)(double)p_args.get("wait_ms", 1000.0), 0, 10000);
	Dictionary action_result;
	if (_wait_for_ai_runtime_action(debugger, request_id, wait_ms, action_result)) {
		const bool ok = action_result.get("ok", false);
		String formatted = _format_ai_runtime_action_result(AIToolNames::CAPTURE_RUNTIME_UI_SNAPSHOT, action_result);
		if (formatted.length() > 60000) {
			formatted = formatted.substr(0, 60000) + "\n... [runtime UI snapshot truncated in tool output; reduce max_nodes or inspect a smaller UI section]";
		}
		return _make_result(formatted, !ok);
	}

	return _make_result(vformat("Sent runtime UI snapshot request. Active debugger session(s): %d.\nNo runtime acknowledgement was received within %d ms; retry after the debugger has processed messages.",
			sent_count,
			wait_ms),
			true);
}

Dictionary AIToolExecutor::_stop_play_scene(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("stop_play_scene is only available in PROJECT mode.", true);
	}
	if (!EditorRunBar::get_singleton()) {
		return _make_result("stop_play_scene failed: editor run bar is not available.", true);
	}
	if (!EditorRunBar::get_singleton()->is_playing()) {
		return _make_result("No game scene is currently playing.");
	}

	const String playing_scene = EditorRunBar::get_singleton()->get_playing_scene();
	EditorRunBar::get_singleton()->stop_playing();
	return _make_result(playing_scene.is_empty() ? "Stopped the running game scene." : "Stopped the running game scene: " + playing_scene);
}

Dictionary AIToolExecutor::_search_files(const Dictionary &p_args) {
	String pattern = p_args.get("pattern", String());
	if (pattern.is_empty()) {
		return _make_result("No search pattern provided.", true);
	}

	String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result(_source_root_missing_message(), true);
	}
	const String normalized_pattern = pattern.replace("\\", "/").simplify_path();
	if (AISettings::load().context_mode == AIContextMode::PROJECT &&
			(normalized_pattern.is_absolute_path() || normalized_pattern == ".." || normalized_pattern.begins_with("../") || normalized_pattern.contains("/../"))) {
		return _make_result("PROJECT mode search rejected: pattern must stay inside the open project root.", true);
	}

	// Use a simple recursive directory walk. Godot's DirAccess doesn't support
	// globs natively, so we do a manual pattern match.
	// For simplicity, we handle: **/*.cpp, **/*.h, src/**/*.cpp patterns.

	// Split pattern into base_dir and file_filter.
	String base_dir = project_root;
	String file_filter = pattern;

	// Extract base directory if pattern contains a path separator.
	int sep = pattern.find("/");
	if (sep >= 0) {
		String dir_part = pattern.substr(0, sep);
		if (dir_part == "**") {
			// ** pattern �?search from root.
			file_filter = pattern.substr(3).trim_prefix("/");
		} else {
			base_dir = project_root.path_join(dir_part);
			file_filter = pattern.substr(sep + 1);
		}
	}

	// Also handle src/**/*.cpp style.
	int star_slash = pattern.find("**/");
	if (star_slash >= 0) {
		String prefix = pattern.substr(0, star_slash);
		if (!prefix.is_empty()) {
			base_dir = project_root.path_join(prefix.trim_suffix("/"));
		}
		file_filter = pattern.substr(star_slash + 3);
		// If the filter starts with *, it's likely *.<ext>
		if (!file_filter.begins_with("*")) {
			file_filter = file_filter.trim_prefix("/");
		}
	}

	// Convert simple glob to suffix/prefix matching.
	String suffix;
	String prefix;
	if (file_filter.begins_with("*.")) {
		suffix = file_filter.substr(1); // e.g. ".cpp"
	} else if (file_filter.begins_with("*")) {
		suffix = file_filter.substr(1);
	} else if (file_filter.ends_with("*")) {
		prefix = file_filter.substr(0, file_filter.length() - 1);
	} else {
		// Exact match.
		suffix = file_filter;
	}

	// Recursively collect matching files.
	Vector<String> results;
	List<String> dirs;
	dirs.push_back(base_dir);

	const int MAX_RESULTS = 200;
	while (!dirs.is_empty() && results.size() < MAX_RESULTS) {
		String current_dir = dirs.front()->get();
		dirs.pop_front();

		Ref<DirAccess> da = DirAccess::open(current_dir);
		if (da.is_null()) {
			continue;
		}

		da->list_dir_begin();
		String name = da->get_next();
		while (!name.is_empty() && results.size() < MAX_RESULTS) {
			if (name == "." || name == "..") {
				name = da->get_next();
				continue;
			}

			String full = current_dir.path_join(name);
			if (da->current_is_dir()) {
				// Skip hidden directories and common build artifacts.
				if (!name.begins_with(".") && name != "bin" && name != "obj" && name != "__pycache__") {
					dirs.push_back(full);
				}
			} else {
				bool match = false;
				if (!suffix.is_empty() && name.ends_with(suffix)) {
					match = true;
				} else if (!prefix.is_empty() && name.begins_with(prefix)) {
					match = true;
				} else if (suffix.is_empty() && prefix.is_empty()) {
					match = (name == file_filter);
				}
				if (match) {
					// Convert to relative path.
					String rel = full.replace(project_root + "/", "");
					results.push_back(rel);
				}
			}

			name = da->get_next();
		}
		da->list_dir_end();
	}

	if (results.is_empty()) {
		return _make_result(vformat("No files matching '%s' found.", pattern));
	}

	StringBuilder sb;
	sb += vformat("Found %d files matching '%s':\n", results.size(), pattern);
	for (int i = 0; i < results.size(); i++) {
		sb += results[i] + "\n";
	}

	return _make_result(sb.as_string());
}

Dictionary AIToolExecutor::_list_files(const Dictionary &p_args) {
	String path = p_args.get("path", ".");
	if (path.is_empty()) {
		path = ".";
	}

	int max_depth = (int)p_args.get("depth", 1);
	if (max_depth < 0) {
		max_depth = 0;
	} else if (max_depth > 5) {
		max_depth = 5;
	}

	String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result(_source_root_missing_message(), true);
	}

	String rel_root = path.replace("\\", "/").simplify_path();
	if (rel_root == ".") {
		rel_root = "";
	}
	if (rel_root.is_absolute_path() || rel_root.begins_with("../") || rel_root == ".." || rel_root.contains("/../")) {
		return _make_result("Directory listing rejected: path must stay inside the project root.", true);
	}

	String root_dir = rel_root.is_empty() ? project_root : project_root.path_join(rel_root);
	Ref<DirAccess> root_da = DirAccess::open(root_dir);
	if (root_da.is_null()) {
		return _make_result("Directory not found: " + (rel_root.is_empty() ? String(".") : rel_root), true);
	}

	struct PendingDir {
		String abs_path;
		String rel_path;
		int depth = 0;
	};

	Vector<String> entries;
	List<PendingDir> dirs;
	PendingDir root;
	root.abs_path = root_dir;
	root.rel_path = rel_root;
	root.depth = 0;
	dirs.push_back(root);

	const int MAX_RESULTS = 200;
	while (!dirs.is_empty() && entries.size() < MAX_RESULTS) {
		PendingDir current = dirs.front()->get();
		dirs.pop_front();

		Ref<DirAccess> da = DirAccess::open(current.abs_path);
		if (da.is_null()) {
			continue;
		}

		da->list_dir_begin();
		String name = da->get_next();
		while (!name.is_empty() && entries.size() < MAX_RESULTS) {
			if (name == "." || name == "..") {
				name = da->get_next();
				continue;
			}

			const bool is_dir = da->current_is_dir();
			const String rel = current.rel_path.is_empty() ? name : current.rel_path.path_join(name);
			if (is_dir) {
				entries.push_back(rel + "/");
				if (current.depth < max_depth && !name.begins_with(".") && name != "bin" && name != "obj" && name != "__pycache__") {
					PendingDir child;
					child.abs_path = current.abs_path.path_join(name);
					child.rel_path = rel;
					child.depth = current.depth + 1;
					dirs.push_back(child);
				}
			} else {
				entries.push_back(rel);
			}

			name = da->get_next();
		}
		da->list_dir_end();
	}

	StringBuilder sb;
	sb += vformat("Listed %d item(s) under '%s' (depth %d):\n", entries.size(), rel_root.is_empty() ? String(".") : rel_root, max_depth);
	for (int i = 0; i < entries.size(); i++) {
		sb += entries[i] + "\n";
	}
	if (entries.size() >= MAX_RESULTS) {
		sb += "... truncated at 200 items. Use a narrower path or lower depth.\n";
	}

	return _make_result(sb.as_string());
}

Dictionary AIToolExecutor::_grep_code(const Dictionary &p_args) {
	String pattern = p_args.get("pattern", String());
	String glob = p_args.get("glob", String());

	if (pattern.is_empty()) {
		return _make_result("No search pattern provided.", true);
	}

	String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result(_source_root_missing_message(), true);
	}

	// Search the whole game project in PROJECT mode. ENGINE mode keeps the
	// focused source-directory list to avoid scanning generated artifacts.
	Vector<String> search_dirs;
	if (AISettings::load().context_mode == AIContextMode::PROJECT) {
		search_dirs.push_back(project_root);
	} else {
		search_dirs.push_back(project_root.path_join("editor"));
		search_dirs.push_back(project_root.path_join("modules"));
		search_dirs.push_back(project_root.path_join("scene"));
		search_dirs.push_back(project_root.path_join("servers"));
		search_dirs.push_back(project_root.path_join("core"));
		search_dirs.push_back(project_root.path_join("drivers"));
		search_dirs.push_back(project_root.path_join("main"));
		search_dirs.push_back(project_root.path_join("platform"));
	}

	// Use the glob to filter file extension.
	String suffix_filter;
	if (!glob.is_empty()) {
		if (glob.begins_with("*.")) {
			suffix_filter = glob.substr(1);
		} else if (glob.begins_with("*")) {
			suffix_filter = glob.substr(1);
		} else {
			suffix_filter = glob;
		}
	}

	StringBuilder result;
	int total_matches = 0;
	const int MAX_MATCHES = 200;

	for (int d = 0; d < search_dirs.size() && total_matches < MAX_MATCHES; d++) {
		List<String> dirs;
		dirs.push_back(search_dirs[d]);

		while (!dirs.is_empty() && total_matches < MAX_MATCHES) {
			String current_dir = dirs.front()->get();
			dirs.pop_front();

			Ref<DirAccess> da = DirAccess::open(current_dir);
			if (da.is_null()) {
				continue;
			}

			da->list_dir_begin();
			String name = da->get_next();
			while (!name.is_empty() && total_matches < MAX_MATCHES) {
				if (name == "." || name == "..") {
					name = da->get_next();
					continue;
				}

				String full_path = current_dir.path_join(name);
				if (da->current_is_dir()) {
					if (!name.begins_with(".") && name != "bin" && name != "obj" && name != "__pycache__") {
						dirs.push_back(full_path);
					}
				} else {
					// Apply glob filter.
					if (!suffix_filter.is_empty() && !name.ends_with(suffix_filter)) {
						name = da->get_next();
						continue;
					}

					// Skip large binary-like files.
					if (name.ends_with(".obj") || name.ends_with(".lib") || name.ends_with(".dll") || name.ends_with(".exe")) {
						name = da->get_next();
						continue;
					}

					Error err = OK;
					String content = FileAccess::get_file_as_string(full_path, &err);
					if (err != OK) {
						name = da->get_next();
						continue;
					}

					// Simple line-by-line search.
					Vector<String> lines = content.split("\n");
					String rel_path = full_path.replace(project_root + "/", "");
					for (int ln = 0; ln < lines.size() && total_matches < MAX_MATCHES; ln++) {
						if (lines[ln].findn(pattern) >= 0) {
							result += vformat("%s:%d: %s\n", rel_path, ln + 1, lines[ln].strip_edges());
							total_matches++;
						}
					}
				}

				name = da->get_next();
			}
			da->list_dir_end();
		}
	}

	if (total_matches == 0) {
		return _make_result(vformat("No matches found for '%s'.", pattern));
	}

	String header = vformat("Found %d matches for '%s':\n", total_matches, pattern);
	return _make_result(header + result.as_string());
}


Dictionary AIToolExecutor::_check_project_scripts(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (settings.context_mode != AIContextMode::PROJECT) {
		return _make_result("check_project_scripts is only available in PROJECT mode. Use run_build/check_build_status for engine source changes.", true);
	}

	const String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result("No open project root was detected. Open a project with project.jundot before validating project scripts.", true);
	}

	Vector<String> script_paths;
	Array paths = p_args.get("paths", Array());
	if (paths.is_empty() && p_args.has("path")) {
		paths.push_back(p_args.get("path", String()));
	}
	if (paths.is_empty() && p_args.has("paths") && p_args["paths"].get_type() == Variant::STRING) {
		paths.push_back(p_args["paths"]);
	}

	bool has_bad_path = false;
	StringBuilder bad_paths;
	for (int i = 0; i < paths.size(); i++) {
		String rel = String(paths[i]).replace("\\", "/").strip_edges();
		if (rel.is_empty()) {
			continue;
		}
		if (_is_script_check_path_rejected(rel)) {
			has_bad_path = true;
			bad_paths += "Rejected path outside project root: " + rel + "\n";
			continue;
		}
		rel = rel.trim_prefix("res://").simplify_path();
		if (rel.to_lower().ends_with(".gd")) {
			script_paths.push_back(rel);
		}
	}
	if (has_bad_path) {
		return _make_result(bad_paths.as_string(), true);
	}
	if (script_paths.is_empty() && paths.is_empty()) {
		const int MAX_GDSCRIPT_CHECKS = 100;
		_collect_gdscript_files(project_root, String(), script_paths, MAX_GDSCRIPT_CHECKS);
	}

	StringBuilder result;
	bool failed = false;

	const String editor_exe = OS::get_singleton()->get_executable_path();
	if (script_paths.is_empty()) {
		result += "No GDScript files selected or found for syntax checking.\n";
	} else if (editor_exe.is_empty() || !FileAccess::exists(editor_exe)) {
		failed = true;
		result += "Could not locate the current editor executable for GDScript check-only validation.\n";
	} else {
		result += vformat("GDScript syntax check: %d file(s)\n", script_paths.size());
		for (int i = 0; i < script_paths.size(); i++) {
			const String rel = script_paths[i];
			String full_path;
			String path_error;
			if (!_resolve_tool_file_path(project_root, rel, full_path, path_error) || !FileAccess::exists(full_path)) {
				failed = true;
				result += vformat("\n[%s]\nError: %s%s\n", rel, path_error, FileAccess::exists(full_path) ? String() : " File not found.");
				continue;
			}

			List<String> args;
			args.push_back("--headless");
			args.push_back("--path");
			args.push_back(project_root);
			args.push_back("--check-only");
			args.push_back("--script");
			args.push_back("res://" + rel);

			String output;
			int exit_code = -1;
			const Error err = _run_command_in_root(project_root, editor_exe, args, output, exit_code);
			result += vformat("\n[%s]\nExit code: %d\n", rel, exit_code);
			if (err != OK) {
				failed = true;
				result += vformat("Failed to start GDScript validator (err=%d).\n", (int)err);
			}
			if (!output.strip_edges().is_empty()) {
				result += _truncate_tool_output(output, 12000).strip_edges() + "\n";
			}
			if (err != OK || exit_code != 0) {
				failed = true;
			}
		}
	}

	bool explicit_cs_path = false;
	for (int i = 0; i < paths.size(); i++) {
		if (String(paths[i]).to_lower().ends_with(".cs")) {
			explicit_cs_path = true;
			break;
		}
	}

	if (_project_has_dotnet_project(project_root) || explicit_cs_path) {
		List<String> dotnet_args;
		dotnet_args.push_back("build");
		dotnet_args.push_back("--nologo");
		String output;
		int exit_code = -1;
		const Error err = _run_command_in_root(project_root, "dotnet", dotnet_args, output, exit_code);
		result += vformat("\nC#/.NET project build\nExit code: %d\n", exit_code);
		if (err != OK) {
			failed = true;
			result += vformat("Failed to start dotnet build (err=%d). Ensure the .NET SDK is installed and available on PATH.\n", (int)err);
		}
		if (!output.strip_edges().is_empty()) {
			result += _truncate_tool_output(output, 20000).strip_edges() + "\n";
		}
		if (err != OK || exit_code != 0) {
			failed = true;
		}
	}

	if (!failed) {
		result += "\nProject script validation passed.";
	} else {
		result += "\nProject script validation failed. Read the errors above, edit the affected scripts, then run check_project_scripts again.";
	}
	return _make_result(result.as_string(), failed);
}

Dictionary AIToolExecutor::_run_build(const Dictionary &p_args) {
	String extra_args = p_args.get("extra_args", String());

	// Check if build is already running.
	if (build_state.is_running()) {
		return _make_result("A build is already running in the background. Use check_build_status to monitor progress.");
	}

	// Start the build in a background thread.
	build_state.start(extra_args);
	if (AISettings::load().develop_mode) {
		AIDevelopFlow::record_build_started();
	}
	String *args_copy = new String(extra_args);
	build_state.thread.start(_build_thread_callback, args_copy);

	return _make_result("Build started in background. Use check_build_status to check progress and get results when complete.");
}

Dictionary AIToolExecutor::_check_build_status(const Dictionary &p_args) {
	if (build_state.is_running()) {
		return _make_result("Build is still running in the background. Check again later.");
	}

	Dictionary r = build_state.get_result();
	String status = r["status"];

	if (status == "idle") {
		return _make_result("No build has been started yet. Use run_build to start one.");
	}

	// Build is done �?retrieve the result.
	String std_out = r["stdout"];
	int exit_code = r["exit_code"];

	if (AISettings::load().develop_mode) {
		AIDevelopFlow::record_build_result(exit_code == 0, exit_code == 0 ? "Build completed successfully." : "Build failed; inspect the build output.");
	}

	// Clean up the thread resource before returning.
	build_state.join_thread();

	StringBuilder result;
	result += vformat("Build status: %s\n", status);
	result += vformat("Exit code: %d\n", exit_code);
	if (!std_out.is_empty()) {
		result += "\n--- build output ---\n" + std_out;
	}

	if (result.as_string().length() > 10000) {
		String truncated = result.as_string().substr(0, 10000);
		truncated += vformat("\n\n[... truncated, full length = %d chars]\n", result.as_string().length());
		return _make_result(truncated);
	}

	return _make_result(result.as_string());
}

Dictionary AIToolExecutor::_read_build_log(const Dictionary &p_args) {
	Dictionary build_result = build_state.get_result();
	const String build_status = build_result.get("status", "idle");
	const String build_output = build_result.get("stdout", String());
	if (build_status == "running") {
		return _make_result("Build is still running. Use check_build_status to wait for completion.");
	}
	if (bool(build_result.get("has_result", false)) && !build_output.is_empty()) {
		const int MAX_LOG_CHARS = 30000;
		const String visible_output = build_output.length() > MAX_LOG_CHARS ? build_output.substr(build_output.length() - MAX_LOG_CHARS) : build_output;
		return _make_result(vformat("Latest in-memory build output (status: %s, exit code: %d):\n\n%s", build_status, int(build_result.get("exit_code", -1)), visible_output));
	}

	String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result(_source_root_missing_message(), true);
	}
	String log_dir = project_root.path_join("artifacts").path_join("logs");

	Ref<DirAccess> da = DirAccess::open(log_dir);
	if (da.is_null()) {
		return _make_result("Build log directory not found: " + log_dir, true);
	}

	// Find the most recent .log file.
	String latest_log;
	uint64_t latest_mtime = 0;

	da->list_dir_begin();
	String name = da->get_next();
	while (!name.is_empty()) {
		if (name.ends_with(".log")) {
			String full = log_dir.path_join(name);
			uint64_t mtime = FileAccess::get_modified_time(full);
			if (mtime > latest_mtime) {
				latest_mtime = mtime;
				latest_log = full;
			}
		}
		name = da->get_next();
	}
	da->list_dir_end();

	if (latest_log.is_empty()) {
		return _make_result("No build log files found in " + log_dir, true);
	}

	Error err = OK;
	String content = FileAccess::get_file_as_string(latest_log, &err);
	if (err != OK) {
		return _make_result(vformat("Failed to read log file: %s", latest_log), true);
	}

	// Truncate if too large.
	const int MAX_LOG_CHARS = 30000;
	if (content.length() > MAX_LOG_CHARS) {
		// Prefer the end of the log (build errors are at the end).
		String tail = content.substr(content.length() - MAX_LOG_CHARS);
		// Also include any error-like lines from the beginning.
		String result = vformat("Log file: %s\n(full length: %d chars, showing last %d chars)\n\n%s",
				latest_log, content.length(), MAX_LOG_CHARS, tail);
		return _make_result(result);
	}

	return _make_result(vformat("Log file: %s\n\n%s", latest_log, content));
}

Dictionary AIToolExecutor::_fetch_url(const Dictionary &p_args) {
	String url = p_args.get("url", String());
	String dest_path = p_args.get("dest_path", String());

	if (url.is_empty()) {
		return _make_result("No URL provided.", true);
	}
	if (dest_path.is_empty()) {
		return _make_result("No destination path provided.", true);
	}

	const AISettingsData settings = AISettings::load();
	if (settings.context_mode == AIContextMode::PROJECT) {
		const String lower_url = url.to_lower();
		const String normalized_dest = dest_path.replace("\\", "/").simplify_path();
		const bool official_store = lower_url.begins_with("https://store.steampowered.com/") || lower_url.begins_with("https://store.epicgames.com/");
		if (!official_store) {
			return _make_result("PROJECT mode research rejected: fetch_url is limited to official Steam and Epic Games Store pages.", true);
		}
		if (!normalized_dest.begins_with(".JundotAI/research/")) {
			return _make_result("PROJECT mode research rejected: downloads must be saved under .JundotAI/research/.", true);
		}
	}

	String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result(_source_root_missing_message(), true);
	}
	String source_update_message;
	if (_ensure_engine_source_updated_before_mutation(source_update_message) != OK) {
		return _make_result("Engine source update check failed before download: " + source_update_message, true);
	}

	String full_dest;
	String path_error;
	if (!_resolve_tool_file_path(project_root, dest_path, full_dest, path_error)) {
		return _make_result(path_error + "\nPath: " + dest_path, true);
	}

	String sha256;
	Error err = AICodeFetcher::fetch_file(url, full_dest, sha256);
	if (err != OK) {
		return _make_result(vformat("Failed to download %s to %s (err=%d)", url, dest_path, (int)err), true);
	}

	return _make_result(vformat("Successfully downloaded %s to %s (SHA256: %s)", url, dest_path, sha256));
}

Dictionary AIToolExecutor::_check_html_prototype(const Dictionary &p_args) {
	if (AISettings::load().context_mode != AIContextMode::PROJECT) {
		return _make_result("check_html_prototype is only available in PROJECT mode.", true);
	}

	String rel_path = String(p_args.get("path", String())).strip_edges().replace("\\", "/");
	if (rel_path.is_empty()) {
		return _make_result("check_html_prototype rejected: path is required.", true);
	}
	rel_path = rel_path.trim_prefix("res://").simplify_path();
	if (!rel_path.begins_with(".JundotAI/prototypes/") || rel_path.get_extension().to_lower() != "html") {
		return _make_result("check_html_prototype only accepts .html files under .JundotAI/prototypes/.", true);
	}

	String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result("No open project root was detected. Open a project with project.jundot before checking an HTML prototype.", true);
	}

	String html_path;
	String path_error;
	if (!_resolve_tool_file_path(project_root, rel_path, html_path, path_error)) {
		return _make_result(path_error + "\nPath: " + rel_path, true);
	}
	if (!FileAccess::exists(html_path)) {
		return _make_result("check_html_prototype file not found: " + rel_path, true);
	}

	const int wait_ms = CLAMP(int(p_args.get("wait_ms", 1500)), 0, 30000);
	const bool take_screenshot = bool(p_args.get("screenshot", true));
	const String out_dir = project_root.path_join(".JundotAI").path_join("browser_checks");
	const String runtime_dir = project_root.path_join(".JundotAI").path_join("browser_runtime");
	Error mkdir_err = DirAccess::make_dir_recursive_absolute(out_dir);
	if (mkdir_err != OK) {
		return _make_result(vformat("check_html_prototype failed to create browser check directory (err=%d): %s", (int)mkdir_err, out_dir), true);
	}
	mkdir_err = DirAccess::make_dir_recursive_absolute(runtime_dir);
	if (mkdir_err != OK) {
		return _make_result(vformat("check_html_prototype failed to create browser runtime directory (err=%d): %s", (int)mkdir_err, runtime_dir), true);
	}

	const String base_name = rel_path.get_file().get_basename().validate_filename();
	const String stamp = itos((int)OS::get_singleton()->get_unix_time()) + "_" + itos((int)(OS::get_singleton()->get_ticks_msec() % 100000));
	const String script_path = runtime_dir.path_join("check_html_prototype_runner.js");
	const String screenshot_path = take_screenshot ? out_dir.path_join(base_name + "_" + stamp + ".png") : String();

	Array click_array = p_args.get("click_selectors", Array());
	String click_json = JSON::stringify(click_array);

	String script = R"JS(
const fs = require('fs');
const { pathToFileURL } = require('url');

async function main() {
  const htmlPath = process.argv[2];
  const screenshotPath = process.argv[3] || '';
  const waitMs = Math.max(0, Number(process.argv[4] || 1500));
  let clickSelectors = [];
  try { clickSelectors = JSON.parse(process.argv[5] || '[]'); } catch (_) {}

  let playwright;
  try {
    playwright = require('playwright');
  } catch (firstError) {
    try {
      playwright = require('playwright-core');
    } catch (secondError) {
      console.log(JSON.stringify({
        status: 'unavailable',
        reason: 'Node is available, but the playwright or playwright-core package is not resolvable from this project/editor environment.',
        hint: 'Install Playwright for browser-backed HTML prototype validation, or run the generated HTML manually and paste browser console errors.'
      }, null, 2));
      process.exit(3);
    }
  }

  const issues = [];
  const events = [];
  let browser = null;
  const launchAttempts = [
    { channel: 'msedge' },
    { channel: 'chrome' },
    {}
  ];
  let launchError = null;
  for (const options of launchAttempts) {
    try {
      browser = await playwright.chromium.launch({ headless: true, ...options });
      break;
    } catch (error) {
      launchError = error;
    }
  }
  if (!browser) {
    console.log(JSON.stringify({
      status: 'unavailable',
      reason: 'Playwright is installed, but no Chromium/Chrome/Edge browser could be launched.',
      error: launchError ? String(launchError.message || launchError) : 'Unknown browser launch error'
    }, null, 2));
    process.exit(3);
  }

  const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
  page.on('console', msg => {
    const type = msg.type();
    const text = msg.text();
    events.push(`[console.${type}] ${text}`);
    if (type === 'error') {
      issues.push(`[console.error] ${text}`);
    }
  });
  page.on('pageerror', error => issues.push(`[pageerror] ${error.message || String(error)}`));
  page.on('requestfailed', request => issues.push(`[requestfailed] ${request.method()} ${request.url()} ${request.failure()?.errorText || ''}`));
  page.on('response', response => {
    if (response.status() >= 400) {
      issues.push(`[http ${response.status()}] ${response.url()}`);
    }
  });

  try {
    await page.goto(pathToFileURL(htmlPath).href, { waitUntil: 'domcontentloaded', timeout: 15000 });
    await page.waitForTimeout(waitMs);
    for (const selector of clickSelectors) {
      try {
        await page.locator(selector).first().click({ timeout: 3000 });
        events.push(`[click] ${selector}`);
        await page.waitForTimeout(250);
      } catch (error) {
        issues.push(`[click failed] ${selector}: ${error.message || String(error)}`);
      }
    }
    if (screenshotPath) {
      await page.screenshot({ path: screenshotPath, fullPage: true });
    }
  } catch (error) {
    issues.push(`[navigation] ${error.message || String(error)}`);
  } finally {
    await browser.close();
  }

  console.log(JSON.stringify({
    status: issues.length ? 'failed' : 'passed',
    html_path: htmlPath,
    screenshot_path: screenshotPath || null,
    wait_ms: waitMs,
    clicked_selectors: clickSelectors,
    issue_count: issues.length,
    issues,
    events: events.slice(-80)
  }, null, 2));
  process.exit(issues.length ? 2 : 0);
}

main().catch(error => {
  console.log(JSON.stringify({ status: 'failed', issues: [`[runner] ${error.message || String(error)}`] }, null, 2));
  process.exit(2);
});
)JS";

	Ref<FileAccess> script_file = FileAccess::open(script_path, FileAccess::WRITE);
	if (script_file.is_null()) {
		return _make_result("check_html_prototype failed to write runner script: " + script_path, true);
	}
	script_file->store_string(script);
	script_file.unref();

	String node_program = "node";
	String npm_program = "npm";
	String bootstrap_log;
	{
		List<String> version_args;
		version_args.push_back("--version");
		String version_output;
		int version_exit = -1;
		const Error node_err = OS::get_singleton()->execute(node_program, version_args, &version_output, &version_exit, true);
		if (node_err != OK || version_exit != 0) {
#ifdef WINDOWS_ENABLED
			const String bootstrap_script_path = runtime_dir.path_join("bootstrap_node_runtime.ps1");
			String ps_script = R"PS(
$ErrorActionPreference = 'Stop'
$Runtime = $args[0]
$NodeDir = Join-Path $Runtime 'node'
$NodeExe = Join-Path $NodeDir 'node.exe'
if (!(Test-Path $NodeExe)) {
  New-Item -ItemType Directory -Force -Path $Runtime | Out-Null
  [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
  $index = Invoke-RestMethod -Uri 'https://nodejs.org/dist/index.json'
  $release = $index | Where-Object { $_.lts -ne $false -and ($_.files -contains 'win-x64-zip') } | Select-Object -First 1
  if ($null -eq $release) {
    $release = $index | Where-Object { $_.files -contains 'win-x64-zip' } | Select-Object -First 1
  }
  if ($null -eq $release) { throw 'Could not find a Windows x64 Node.js release.' }
  $version = $release.version
  $zipName = "node-$version-win-x64.zip"
  $zipPath = Join-Path $Runtime $zipName
  $extractRoot = Join-Path $Runtime "node_extract"
  $url = "https://nodejs.org/dist/$version/$zipName"
  if (Test-Path $extractRoot) { Remove-Item -LiteralPath $extractRoot -Recurse -Force }
  Invoke-WebRequest -Uri $url -OutFile $zipPath
  Expand-Archive -LiteralPath $zipPath -DestinationPath $extractRoot -Force
  $expanded = Get-ChildItem -LiteralPath $extractRoot -Directory | Select-Object -First 1
  if ($null -eq $expanded) { throw 'Node.js archive did not contain an extracted directory.' }
  if (Test-Path $NodeDir) { Remove-Item -LiteralPath $NodeDir -Recurse -Force }
  Move-Item -LiteralPath $expanded.FullName -Destination $NodeDir
  Remove-Item -LiteralPath $extractRoot -Recurse -Force
}
& $NodeExe --version
)PS";
			Ref<FileAccess> ps_file = FileAccess::open(bootstrap_script_path, FileAccess::WRITE);
			if (ps_file.is_null()) {
				return _make_result("check_html_prototype could not write Node bootstrap script: " + bootstrap_script_path, true);
			}
			ps_file->store_string(ps_script);
			ps_file.unref();

			List<String> ps_args;
			ps_args.push_back("-NoProfile");
			ps_args.push_back("-ExecutionPolicy");
			ps_args.push_back("Bypass");
			ps_args.push_back("-File");
			ps_args.push_back(bootstrap_script_path);
			ps_args.push_back(runtime_dir);
			int bootstrap_exit = -1;
			const Error ps_err = OS::get_singleton()->execute("powershell", ps_args, &bootstrap_log, &bootstrap_exit, true);
			if (ps_err != OK || bootstrap_exit != 0) {
				return _make_result(vformat("check_html_prototype could not auto-install portable Node.js (err=%d, exit=%d).\nRuntime: %s\nOutput:\n%s", (int)ps_err, bootstrap_exit, runtime_dir, bootstrap_log), true);
			}
			node_program = runtime_dir.path_join("node").path_join("node.exe");
			npm_program = runtime_dir.path_join("node").path_join("npm.cmd");
#else
			return _make_result("check_html_prototype could not find Node.js. Automatic portable Node bootstrap is currently implemented on Windows only.", true);
#endif
		}
	}

	{
		List<String> resolve_args;
		resolve_args.push_back("-e");
		resolve_args.push_back("require.resolve('playwright')");
		String resolve_output;
		int resolve_exit = -1;
		String previous_cwd = OS::get_singleton()->get_cwd();
		OS::get_singleton()->set_cwd(runtime_dir);
		Error resolve_err = OS::get_singleton()->execute(node_program, resolve_args, &resolve_output, &resolve_exit, true);
		OS::get_singleton()->set_cwd(previous_cwd);
		if (resolve_err != OK || resolve_exit != 0) {
			List<String> npm_args;
			npm_args.push_back("install");
			npm_args.push_back("playwright");
			npm_args.push_back("--no-audit");
			npm_args.push_back("--fund=false");
			String npm_output;
			int npm_exit = -1;
			previous_cwd = OS::get_singleton()->get_cwd();
			OS::get_singleton()->set_cwd(runtime_dir);
			const Error npm_err = OS::get_singleton()->execute(npm_program, npm_args, &npm_output, &npm_exit, true);
			OS::get_singleton()->set_cwd(previous_cwd);
			bootstrap_log += "\n--- npm install playwright ---\n" + npm_output;
			if (npm_err != OK || npm_exit != 0) {
				return _make_result(vformat("check_html_prototype could not auto-install Playwright into the project browser runtime (err=%d, exit=%d).\nRuntime: %s\nOutput:\n%s", (int)npm_err, npm_exit, runtime_dir, bootstrap_log), true);
			}
		}
	}

	List<String> args;
	args.push_back(script_path);
	args.push_back(html_path);
	args.push_back(screenshot_path);
	args.push_back(itos(wait_ms));
	args.push_back(click_json);

	String output;
	int exit_code = -1;
	const String previous_cwd = OS::get_singleton()->get_cwd();
	if (OS::get_singleton()->set_cwd(runtime_dir) != OK) {
		return _make_result("check_html_prototype failed to switch to browser runtime directory: " + runtime_dir, true);
	}
	Error exec_err = OS::get_singleton()->execute(node_program, args, &output, &exit_code, true);
	OS::get_singleton()->set_cwd(previous_cwd);
	if (exec_err != OK) {
		return _make_result(vformat("check_html_prototype could not start Node.js after dependency bootstrap (err=%d).\nRuntime: %s\nHTML: %s", (int)exec_err, runtime_dir, rel_path), true);
	}

	StringBuilder result;
	result += "Tool: check_html_prototype\n";
	result += "HTML: " + rel_path + "\n";
	result += "Runtime: " + runtime_dir + "\n";
	result += "Runner: " + script_path + "\n";
	if (!screenshot_path.is_empty()) {
		result += "Screenshot: " + screenshot_path + "\n";
	}
	if (!bootstrap_log.strip_edges().is_empty()) {
		result += "\n--- dependency bootstrap ---\n" + bootstrap_log.strip_edges() + "\n";
	}
	result += vformat("Exit code: %d\n\n", exit_code);
	result += output;
	if (result.as_string().length() > 30000) {
		String truncated = result.as_string().substr(0, 30000);
		truncated += "\n\n[... truncated at 30000 chars]\n";
		return _make_result(truncated, true);
	}

	return _make_result(result.as_string(), exit_code != 0);
}

Dictionary AIToolExecutor::_shell_command(const Dictionary &p_args) {
	String command = p_args.get("command", String());
	String workdir = p_args.get("workdir", String());
	if (command.is_empty()) {
		return _make_result("No command provided.", true);
	}

	String tool_root = _get_project_root();
	if (tool_root.is_empty()) {
		tool_root = _get_configured_engine_source_cache_root(AISettings::load());
		if (tool_root.is_empty()) {
			return _make_result("Project root not detected.", true);
		}
		Ref<DirAccess> da = DirAccess::create_for_path(tool_root);
		if (da.is_null() || da->make_dir_recursive(tool_root) != OK) {
			return _make_result("Project root not detected and failed to create engine source cache directory.", true);
		}
	}

	String command_workdir = tool_root;
	if (!workdir.strip_edges().is_empty() && workdir.strip_edges() != ".") {
		String full_workdir;
		String path_error;
		if (!_resolve_tool_file_path(tool_root, workdir, full_workdir, path_error)) {
			return _make_result(path_error + "\nWorkdir: " + workdir, true);
		}
		if (!DirAccess::dir_exists_absolute(full_workdir)) {
			return _make_result("shell_command workdir does not exist: " + workdir, true);
		}
		command_workdir = full_workdir;
	}

	if (AISettings::load().context_mode == AIContextMode::PROJECT && !_project_shell_command_stays_in_root(command)) {
		return _make_result("PROJECT mode shell command rejected: commands must stay inside the open project and may not reference parent, absolute, or engine-source paths.", true);
	}
	String source_update_message;
	if (_ensure_engine_source_updated_before_mutation(source_update_message) != OK) {
		return _make_result("Engine source update check failed before shell command: " + source_update_message, true);
	}

	if (AISettings::load().develop_mode) {
		return _make_result("[Develop Mode] shell_command dry-run only; command was not executed: " + command);
	}

	// Split command into program and arguments.
	Vector<String> parts = command.split(" ", false);
	if (parts.is_empty()) {
		return _make_result("Empty command.", true);
	}

	String program = parts[0];
	List<String> shell_args;
	for (int i = 1; i < parts.size(); i++) {
		shell_args.push_back(parts[i]);
	}

	String std_out;
	int exit_code = -1;

	String previous_cwd = OS::get_singleton()->get_cwd();
	Error cwd_err = OS::get_singleton()->set_cwd(command_workdir);
	if (cwd_err != OK) {
		return _make_result("Failed to switch command working directory to: " + command_workdir, true);
	}

#ifdef WINDOWS_ENABLED
	// On Windows, use cmd /c for shell commands.
	List<String> cmd_args;
	cmd_args.push_back("/c");
	cmd_args.push_back(command);
	Error err = OS::get_singleton()->execute("cmd", cmd_args, &std_out, &exit_code, true);
#else
	Error err = OS::get_singleton()->execute(program, shell_args, &std_out, &exit_code, true);
#endif

	OS::get_singleton()->set_cwd(previous_cwd);

	if (err != OK) {
		return _make_result(vformat("Failed to execute command: %s (err=%d)", command, (int)err), true);
	}

	StringBuilder result;
	result += vformat("Command: %s\n", command);
	result += vformat("Workdir: %s\n", command_workdir);
	result += vformat("Exit code: %d\n", exit_code);
	if (!std_out.is_empty()) {
		result += "\n--- stdout ---\n" + std_out;
	}

	// Truncate very large output.
	if (result.as_string().length() > 30000) {
		String truncated = result.as_string().substr(0, 30000);
		truncated += vformat("\n\n[... truncated at 30000 chars]\n");
		return _make_result(truncated);
	}

	return _make_result(result.as_string());
}

static void _restart_editor_on_main_thread(const String &p_executable_path) {
	EditorNode *editor = EditorNode::get_singleton();
	if (!editor) {
		ERR_PRINT("AI engine restart failed: EditorNode is unavailable.");
		return;
	}

	const Error state_err = AIRestartHelper::save_state();
	if (state_err != OK) {
		ERR_PRINT(vformat("AI engine restart could not save editor state (err=%d). Restarting with the standard editor session restore path.", (int)state_err));
	}

	// restart_editor() touches editor UI and configures OS restart-on-exit, so
	// it must run on the main thread rather than the AI tool worker thread.
	OS::get_singleton()->set_restart_executable_path(p_executable_path);
	editor->restart_editor(false);
}

Dictionary AIToolExecutor::_restart_engine(const Dictionary &p_args) {
	const Dictionary build_result = build_state.get_result();
	if (String(build_result.get("status", "idle")) != "done" || int(build_result.get("exit_code", -1)) != 0) {
		return _make_result("Engine switch rejected: no successful AI build is available. Run run_build and wait for check_build_status to report success first.", true);
	}

	const String executable_path = build_result.get("executable_path", String());
	if (executable_path.is_empty() || !FileAccess::exists(executable_path)) {
		return _make_result("Engine switch rejected: the executable produced by the successful AI build could not be found.", true);
	}

	if (AISettings::load().develop_mode) {
		AIDevelopFlow::record_restart_requested();
	}
	callable_mp_static(&_restart_editor_on_main_thread).bind(executable_path).call_deferred();
	return _make_result("Engine switch scheduled. The current editor will close, then launch the AI-compiled editor: " + executable_path);
}

static Error _run_upload_check(const String &p_root, const String &p_program, const Vector<String> &p_args, String &r_output) {
	List<String> args;
	for (const String &arg : p_args) {
		args.push_back(arg);
	}
	const String previous_cwd = OS::get_singleton()->get_cwd();
	if (OS::get_singleton()->set_cwd(p_root) != OK) {
		r_output = "Could not switch to the engine source root.";
		return ERR_CANT_OPEN;
	}
	int exit_code = -1;
	r_output.clear();
	const Error err = OS::get_singleton()->execute(p_program, args, &r_output, &exit_code, true);
	OS::get_singleton()->set_cwd(previous_cwd);
	if (err != OK) {
		r_output = vformat("Could not start required quality tool '%s' (error %d).", p_program, (int)err);
		return err;
	}
	return exit_code == 0 ? OK : FAILED;
}

static Error _run_mutating_format_check(const String &p_root, const String &p_file_path, const Vector<String> &p_script_args, String &r_error) {
	const String full_path = p_root.path_join(p_file_path);
	Error read_err = OK;
	const String original = FileAccess::get_file_as_string(full_path, &read_err);
	if (read_err != OK) {
		r_error = "Could not read the file before format validation.";
		return read_err;
	}

	String output;
	const Error command_err = _run_upload_check(p_root, "python", p_script_args, output);
	Error after_err = OK;
	const String after = FileAccess::get_file_as_string(full_path, &after_err);
	if (after_err != OK) {
		r_error = "Format validation could not read the resulting file.";
		return after_err;
	}
	if (after != original) {
		Ref<FileAccess> restore = FileAccess::open(full_path, FileAccess::WRITE);
		if (restore.is_valid()) {
			restore->store_string(original);
		}
		r_error = "File formatting check failed. Run the repository formatter before uploading. " + output.strip_edges();
		return FAILED;
	}
	if (command_err != OK) {
		r_error = "Formatting check could not complete: " + output.strip_edges();
		return command_err;
	}
	return OK;
}

static Error _validate_upload_format_and_quality(const String &p_root, const String &p_file_path, const String &p_code, String &r_report) {
	if (p_code.contains("<<<<<<<") || p_code.contains("=======") || p_code.contains(">>>>>>>")) {
		r_report = "Code quality check failed: unresolved merge conflict markers were found.";
		return FAILED;
	}
	if (p_code.contains("C:/Users/") || p_code.contains("C:\\Users\\") || p_code.contains("/home/")) {
		r_report = "Code quality check failed: user-specific absolute paths were found.";
		return FAILED;
	}

	String output;
	if (_run_upload_check(p_root, "git", { "diff", "--check", "--", p_file_path }, output) != OK) {
		r_report = "Code quality check failed (git diff --check): " + output.strip_edges();
		return FAILED;
	}

	String format_error;
	if (_run_mutating_format_check(p_root, p_file_path, { "misc/scripts/file_format.py", p_file_path }, format_error) != OK) {
		r_report = format_error;
		return FAILED;
	}

	const String extension = p_file_path.get_extension().to_lower();
	const bool is_cpp = extension == "c" || extension == "h" || extension == "cpp" || extension == "hpp" || extension == "cc" || extension == "hh" || extension == "cxx" || extension == "hxx" || extension == "m" || extension == "mm" || extension == "inc";
	if (is_cpp) {
		if (_run_mutating_format_check(p_root, p_file_path, { "misc/scripts/validate_includes.py", p_file_path }, format_error) != OK ||
				_run_mutating_format_check(p_root, p_file_path, { "misc/scripts/copyright_headers.py", p_file_path }, format_error) != OK) {
			r_report = format_error;
			return FAILED;
		}
		if ((extension == "h" || extension == "hpp" || extension == "hh" || extension == "hxx") &&
				_run_mutating_format_check(p_root, p_file_path, { "misc/scripts/header_guards.py", p_file_path }, format_error) != OK) {
			r_report = format_error;
			return FAILED;
		}
		if (_run_upload_check(p_root, "clang-format", { "--dry-run", "--Werror", "--style=file", p_file_path }, output) != OK) {
			r_report = "C/C++ style check failed. Run clang-format before uploading. " + output.strip_edges();
			return FAILED;
		}
	} else if (extension == "py") {
		if (_run_upload_check(p_root, "ruff", { "check", p_file_path }, output) != OK ||
				_run_upload_check(p_root, "ruff", { "format", "--check", p_file_path }, output) != OK) {
			r_report = "Python quality check failed. Run Ruff before uploading. " + output.strip_edges();
			return FAILED;
		}
	} else if (extension == "cs") {
		if (_run_mutating_format_check(p_root, p_file_path, { "misc/scripts/dotnet_format.py", p_file_path }, format_error) != OK) {
			r_report = format_error;
			return FAILED;
		}
	}

	r_report = "Format and repository quality checks passed.";
	return OK;
}

static double _estimate_code_universality(const String &p_file_path, const String &p_code, String &r_reason) {
	double score = 88.0;
	Vector<String> reasons;
	if (p_code.length() < 120) {
		score -= 18.0;
		reasons.push_back("very small change surface");
	}
	const String lower = p_code.to_lower();
	if (lower.contains("todo") || lower.contains("fixme")) {
		score -= 12.0;
		reasons.push_back("unfinished TODO/FIXME markers");
	}
	if (lower.contains("hardcoded") || lower.contains("workaround")) {
		score -= 10.0;
		reasons.push_back("hardcoded or workaround-specific behavior");
	}
	if (p_file_path.contains("test") || p_file_path.contains("_tmp") || p_file_path.contains("local")) {
		score -= 8.0;
		reasons.push_back("narrow test/local path");
	}
	if (p_file_path.begins_with("editor/") || p_file_path.begins_with("core/") || p_file_path.begins_with("scene/")) {
		score += 4.0;
		reasons.push_back("shared engine subsystem");
	}
	score = CLAMP(score, 0.0, 100.0);
	String reason_text;
	for (int i = 0; i < reasons.size(); i++) {
		if (!reason_text.is_empty()) {
			reason_text += ", ";
		}
		reason_text += reasons[i];
	}
	r_reason = reason_text.is_empty() ? "general reusable engine implementation" : reason_text;
	return score;
}

Dictionary AIToolExecutor::_upload_code(const Dictionary &p_args) {
	String file_path = p_args.get("file_path", String());
	String commit_message = p_args.get("commit_message", String());

	if (file_path.is_empty()) {
		return _make_result("file_path is required.", true);
	}
	if (commit_message.is_empty()) {
		return _make_result("commit_message is required.", true);
	}

	String project_root = _get_project_root();
	if (project_root.is_empty()) {
		return _make_result("Project root not detected. Make sure you're in ENGINE mode with a valid godot.creator.json or SConstruct in the working directory.", true);
	}

	String source_update_message;
	if (_ensure_engine_source_updated_before_mutation(source_update_message) != OK) {
		return _make_result("Engine source update check failed before upload: " + source_update_message, true);
	}

	String full_path = project_root.path_join(file_path);

	// Step 1: Verify file exists.
	if (!FileAccess::exists(full_path)) {
		return _make_result("File not found: " + full_path, true);
	}

	// Step 2: Read file content.
	Error read_err;
	String code = FileAccess::get_file_as_string(full_path, &read_err);
	if (read_err != OK) {
		return _make_result("Failed to read file: " + full_path, true);
	}

	// Step 3: Repository format and code-quality checks.
	String quality_report;
	if (_validate_upload_format_and_quality(project_root, file_path, code, quality_report) != OK) {
		return _make_result("Upload rejected: " + quality_report, true);
	}

	// Step 4: Security check - detect suspicious patterns.
	CodeSecurityReport security = AICodeSecurityChecker::check(code);
	if (!security.is_safe) {
		String warning_text = "Upload rejected: security check failed.\n";
		for (int i = 0; i < security.warnings.size(); i++) {
			warning_text += "  - " + security.warnings[i] + "\n";
		}
		warning_text += "\nPlease resolve the flagged code before uploading.";
		return _make_result(warning_text, true);
	}

	// Step 5: Estimate whether the change belongs in a shared engine fork.
	String universality_reason;
	const double universality_score = _estimate_code_universality(file_path, code, universality_reason);

	// Step 6: Check against the configured universality threshold.
	AISettingsData settings = AISettings::load();
	if (universality_score < settings.feature_universality_threshold) {
		return _make_result(
				"Upload rejected: estimated universality " + String::num(universality_score, 1) +
						"% below threshold " + String::num(settings.feature_universality_threshold, 1) +
						"%. Code should be more generally useful before committing.",
				true);
	}

	// Step 7: Perform git add / commit / push.
	if (settings.develop_mode) {
		if (AIDevelopFlow::get_stage() != AIDevelopFlow::READY_TO_UPLOAD) {
			return _make_result("[Develop Mode] Upload simulation is waiting for successful user verification and AI verification.", true);
		}
		AIDevelopFlow::record_simulated_upload(file_path);
		return _make_result(
				"[Develop Mode] Upload simulation successful: " + file_path +
						"\n  Format and quality: PASSED"
						"\n  Security: PASSED"
						"\n  Universality: " + String::num(universality_score, 1) + "% (" + universality_reason + ")"
						"\n  Git add/commit/push: SKIPPED (no GitHub files changed)");
	}

	String error_msg;
	Error upload_err = AICodeUploader::upload(file_path, commit_message, project_root, &error_msg);
	if (upload_err != OK) {
		return _make_result("Upload failed: " + error_msg, true);
	}

	return _make_result(
			"Upload successful: " + file_path +
					"\n  Commit message: " + commit_message +
					"\n  Format and quality: PASSED"
					"\n  Security: PASSED"
					"\n  Universality: " + String::num(universality_score, 1) + "% (" + universality_reason + ")");
}

Dictionary AIToolExecutor::_develop_ai_verify(const Dictionary &p_args) {
	const AISettingsData settings = AISettings::load();
	if (!settings.develop_mode) {
		return _make_result("Develop Mode is not enabled.", true);
	}
	if (AIDevelopFlow::get_stage() != AIDevelopFlow::AI_VERIFICATION) {
		return _make_result("AI verification is not ready. The user must verify the restarted feature first.", true);
	}
	const bool passed = p_args.get("passed", false);
	const String summary = p_args.get("summary", String());
	AIDevelopFlow::record_ai_verification(passed, summary);
	return _make_result(vformat("[Develop Mode] AI verification %s: %s", passed ? "PASSED" : "FAILED", summary), !passed);
}

Dictionary AIToolExecutor::_execute_mcp_tool(const String &p_server_name, const String &p_tool_name, const String &p_args_json) {
	if (AISettings::load().develop_mode) {
		return _make_result("[Develop Mode] External MCP tool execution is disabled during the safe workflow demonstration.", true);
	}
	// Look up the MCP server configuration.
	Vector<AISkillEntry> skills;
	Vector<AIMCPServerEntry> mcp_servers;
	if (AIToolRegistry::load(skills, mcp_servers) != OK) {
		return _make_result(vformat("MCP server '%s' not found (registry load failed).", p_server_name), true);
	}

	AIMCPServerEntry target_server;
	bool found = false;
	for (const AIMCPServerEntry &server : mcp_servers) {
		if (server.name == p_server_name && server.enabled) {
			target_server = server;
			found = true;
			break;
		}
	}

	if (!found) {
		return _make_result(vformat("MCP server '%s' is not configured or not enabled.", p_server_name), true);
	}

	if (target_server.command.is_empty() && target_server.url.is_empty()) {
		return _make_result(vformat("MCP server '%s' has no command or URL configured.", p_server_name), true);
	}

	// Use MCPServerRuntime for interactive MCP communication
	MCPServerRuntime *runtime = MCPServerRuntime::get_singleton();

	// Lazy start: if runtime not running for this server, start it
	if (!runtime->is_running_server(p_server_name)) {
		Error err = runtime->start(target_server);
		if (err != OK) {
			return _make_result(vformat("Failed to start MCP server '%s': %s", p_server_name, runtime->get_last_error()), true);
		}
	}

	// Parse arguments JSON
	Dictionary arguments;
	Variant parsed_args = JSON::parse_string(p_args_json);
	if (parsed_args.get_type() == Variant::DICTIONARY) {
		arguments = parsed_args;
	}

	// Call the tool via runtime
	Dictionary result = runtime->call_tool(p_tool_name, arguments);

	if (result.has("is_error") && result["is_error"]) {
		String error_content = result.get("content", "Unknown error");
		return _make_result(vformat("MCP tool '%s.%s' error: %s", p_server_name, p_tool_name, error_content), true);
	}

	String content = result.get("content", "");
	return _make_result(content);
}
