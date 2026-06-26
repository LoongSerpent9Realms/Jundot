/*  ai_source_update_service.cpp                                          */
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

#include "ai_source_update_service.h"

#include "editor/ai/ai_settings.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/os/time.h"

static Mutex source_update_mutex;
static AISourceUpdateStatus cached_source_status;
static uint64_t last_edit_update_check_msec = 0;
static String last_edit_update_root;

static Error _run_source_git(const String &p_root, const Vector<String> &p_args, String &r_output, int &r_exit_code) {
	List<String> args;
	args.push_back("-C");
	args.push_back(p_root);
	for (const String &arg : p_args) {
		args.push_back(arg);
	}
	r_output.clear();
	r_exit_code = -1;
	const Error err = OS::get_singleton()->execute("git", args, &r_output, &r_exit_code, true);
	if (err != OK) {
		return err;
	}
	return r_exit_code == 0 ? OK : FAILED;
}

static bool _is_source_root(const String &p_path) {
	return !p_path.is_empty() && FileAccess::exists(p_path.path_join("SConstruct"));
}

static bool _is_git_checkout(const String &p_path) {
	const String git_path = p_path.path_join(".git");
	return DirAccess::dir_exists_absolute(git_path) || FileAccess::exists(git_path);
}

static String _find_source_in_cache(const String &p_cache_root) {
	if (_is_source_root(p_cache_root)) {
		return p_cache_root;
	}
	Ref<DirAccess> dir = DirAccess::open(p_cache_root);
	if (dir.is_null()) {
		return String();
	}
	dir->list_dir_begin();
	String name = dir->get_next();
	while (!name.is_empty()) {
		if (dir->current_is_dir() && !name.begins_with(".")) {
			const String candidate = p_cache_root.path_join(name);
			if (_is_source_root(candidate)) {
				dir->list_dir_end();
				return candidate;
			}
		}
		name = dir->get_next();
	}
	dir->list_dir_end();
	return String();
}

String AISourceUpdateService::resolve_source_root() {
	const AISettingsData settings = AISettings::load();
	if (_is_source_root(settings.engine_source_root)) {
		return settings.engine_source_root;
	}
	const String detected_root = AISettings::get_engine_source_root(settings);
	if (_is_source_root(detected_root)) {
		return detected_root;
	}
	String cache_root = settings.engine_source_cache_root.strip_edges();
	if (cache_root.is_empty()) {
		cache_root = OS::get_singleton()->get_user_data_dir().path_join("engine_source");
	}
	return _find_source_in_cache(cache_root);
}

AISourceUpdateStatus AISourceUpdateService::get_cached_status() {
	MutexLock lock(source_update_mutex);
	return cached_source_status;
}

AISourceUpdateStatus AISourceUpdateService::check_for_updates(bool p_fetch_remote) {
	MutexLock lock(source_update_mutex);
	AISourceUpdateStatus status;
	status.state = AISourceUpdateStatus::CHECKING;
	status.source_root = resolve_source_root();
	if (status.source_root.is_empty()) {
		status.state = AISourceUpdateStatus::NOT_AVAILABLE;
		status.message = "Engine source is not downloaded or configured.";
		cached_source_status = status;
		return status;
	}
	if (!_is_git_checkout(status.source_root)) {
		status.state = AISourceUpdateStatus::NOT_AVAILABLE;
		status.message = "Engine source is not a Git checkout; automatic updates are unavailable.";
		cached_source_status = status;
		return status;
	}

	String output;
	int exit_code = -1;
	if (p_fetch_remote && _run_source_git(status.source_root, { "fetch", "--prune" }, output, exit_code) != OK) {
		status.state = AISourceUpdateStatus::ERROR;
		status.message = "Could not fetch the source repository: " + output.strip_edges();
		cached_source_status = status;
		return status;
	}
	if (_run_source_git(status.source_root, { "rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{upstream}" }, output, exit_code) != OK) {
		status.state = AISourceUpdateStatus::NOT_AVAILABLE;
		status.message = "The current source branch has no upstream branch.";
		cached_source_status = status;
		return status;
	}
	status.upstream = output.strip_edges();
	if (_run_source_git(status.source_root, { "rev-list", "--count", "HEAD.." + status.upstream }, output, exit_code) != OK) {
		status.state = AISourceUpdateStatus::ERROR;
		status.message = "Could not compare local and upstream source revisions.";
		cached_source_status = status;
		return status;
	}
	status.behind_count = output.strip_edges().to_int();
	status.checked_at_unix = Time::get_singleton()->get_unix_time_from_system();
	if (status.behind_count > 0) {
		status.state = AISourceUpdateStatus::UPDATE_AVAILABLE;
		status.message = vformat("Engine source update available: %d commit(s) behind %s.", status.behind_count, status.upstream);
	} else {
		status.state = AISourceUpdateStatus::UP_TO_DATE;
		status.message = "Engine source is up to date.";
	}
	cached_source_status = status;
	return status;
}

Error AISourceUpdateService::update_source(AISourceUpdateStatus &r_status) {
	MutexLock lock(source_update_mutex);
	String root = r_status.source_root;
	if (root.is_empty()) {
		root = resolve_source_root();
	}
	if (root.is_empty() || !_is_git_checkout(root)) {
		r_status.state = AISourceUpdateStatus::NOT_AVAILABLE;
		r_status.message = "A Git engine source checkout is required for automatic updates.";
		cached_source_status = r_status;
		return ERR_UNAVAILABLE;
	}

	String output;
	int exit_code = -1;
	String upstream = r_status.upstream;
	if (upstream.is_empty() && _run_source_git(root, { "rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{upstream}" }, output, exit_code) == OK) {
		upstream = output.strip_edges();
	}
	if (upstream.is_empty()) {
		r_status.state = AISourceUpdateStatus::NOT_AVAILABLE;
		r_status.message = "The current source branch has no upstream branch.";
		cached_source_status = r_status;
		return ERR_UNAVAILABLE;
	}

	r_status.state = AISourceUpdateStatus::UPDATING;
	r_status.source_root = root;
	r_status.upstream = upstream;
	cached_source_status = r_status;

	if (_run_source_git(root, { "status", "--porcelain" }, output, exit_code) != OK) {
		r_status.state = AISourceUpdateStatus::ERROR;
		r_status.message = "Could not inspect local source changes.";
		cached_source_status = r_status;
		return FAILED;
	}
	const bool dirty = !output.strip_edges().is_empty();
	const String stash_ref = "stash@{0}";
	if (dirty && _run_source_git(root, { "stash", "push", "--include-untracked", "--message", "jundot-ai-source-update" }, output, exit_code) != OK) {
		r_status.state = AISourceUpdateStatus::ERROR;
		r_status.message = "Could not preserve local source changes before updating: " + output.strip_edges();
		cached_source_status = r_status;
		return FAILED;
	}

	Error merge_err = _run_source_git(root, { "merge", "--no-edit", "-X", "ours", upstream }, output, exit_code);
	if (merge_err != OK) {
		String conflicts;
		_run_source_git(root, { "diff", "--name-only", "--diff-filter=U" }, conflicts, exit_code);
		if (!conflicts.strip_edges().is_empty()) {
			_run_source_git(root, { "checkout", "--ours", "--", "." }, output, exit_code);
			_run_source_git(root, { "add", "-A" }, output, exit_code);
			merge_err = _run_source_git(root, { "-c", "user.name=Jundot AI", "-c", "user.email=jundot-ai@local", "commit", "--no-edit" }, output, exit_code);
		}
	}
	if (merge_err != OK) {
		_run_source_git(root, { "merge", "--abort" }, output, exit_code);
		if (dirty) {
			_run_source_git(root, { "stash", "apply", "--index", stash_ref }, output, exit_code);
		}
		r_status.state = AISourceUpdateStatus::ERROR;
		r_status.message = "Automatic source update failed; the edit was cancelled. " + output.strip_edges();
		cached_source_status = r_status;
		return FAILED;
	}

	if (dirty) {
		Error apply_err = _run_source_git(root, { "stash", "apply", "--index", stash_ref }, output, exit_code);
		if (apply_err != OK) {
			String conflicts;
			_run_source_git(root, { "diff", "--name-only", "--diff-filter=U" }, conflicts, exit_code);
			if (!conflicts.strip_edges().is_empty()) {
				_run_source_git(root, { "checkout", "--theirs", "--", "." }, output, exit_code);
				_run_source_git(root, { "add", "-A" }, output, exit_code);
				apply_err = OK;
			}
		}
		if (apply_err != OK) {
			r_status.state = AISourceUpdateStatus::ERROR;
			r_status.message = "Source updated, but local changes could not be restored automatically. They remain in " + stash_ref + ".";
			cached_source_status = r_status;
			return FAILED;
		}
		_run_source_git(root, { "stash", "drop", stash_ref }, output, exit_code);
	}

	r_status.state = AISourceUpdateStatus::UPDATED;
	r_status.behind_count = 0;
	r_status.checked_at_unix = Time::get_singleton()->get_unix_time_from_system();
	r_status.message = "Engine source updated successfully; local changes were preserved.";
	cached_source_status = r_status;
	last_edit_update_root = root;
	last_edit_update_check_msec = OS::get_singleton()->get_ticks_msec();
	return OK;
}

Error AISourceUpdateService::ensure_updated_before_edit(String &r_message) {
	const String root = resolve_source_root();
	if (root.is_empty()) {
		r_message = "Engine source is not configured.";
		return ERR_UNAVAILABLE;
	}
	{
		MutexLock lock(source_update_mutex);
		const uint64_t now = OS::get_singleton()->get_ticks_msec();
		if (last_edit_update_root == root && now - last_edit_update_check_msec < 300000) {
			r_message = cached_source_status.message;
			return cached_source_status.state == AISourceUpdateStatus::UP_TO_DATE || cached_source_status.state == AISourceUpdateStatus::UPDATED ? OK : FAILED;
		}
	}

	AISourceUpdateStatus status = check_for_updates(true);
	if (status.state == AISourceUpdateStatus::UPDATE_AVAILABLE) {
		const Error err = update_source(status);
		r_message = status.message;
		return err;
	}
	r_message = status.message;
	if (status.state != AISourceUpdateStatus::UP_TO_DATE && status.state != AISourceUpdateStatus::UPDATED) {
		return status.state == AISourceUpdateStatus::NOT_AVAILABLE ? ERR_UNAVAILABLE : FAILED;
	}
	{
		MutexLock lock(source_update_mutex);
		last_edit_update_root = root;
		last_edit_update_check_msec = OS::get_singleton()->get_ticks_msec();
	}
	return OK;
}
