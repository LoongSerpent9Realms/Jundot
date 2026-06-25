/*  ai_source_manager.cpp                                                 */
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
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "ai_source_manager.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/string/translation_server.h"
#include "editor/ai/ai_settings.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/project_manager/multipart_downloader.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/scroll_bar.h"
#include "scene/gui/scroll_container.h"

#include "modules/zip/zip_reader.h"

#ifdef WINDOWS_ENABLED
#include <windows.h>
#undef FAILED
#endif

const char *JUNDOT_ENGINE_SOURCE_ZIP_URL = "https://github.com/LoongSerpent9Realms/Jundot/archive/refs/heads/master.zip";

void AISourceManager::_bind_methods() {
}

void AISourceManager::_cleanup_on_close() {
	if (is_downloading && downloader) {
		downloader->cancel();
		is_downloading = false;
		set_process(false);
	}
}

String AISourceManager::_get_default_cache_root() const {
	if (!OS::get_singleton()) {
		return String();
	}
#ifdef WINDOWS_ENABLED
	String appdata = OS::get_singleton()->get_environment("APPDATA");
	if (!appdata.is_empty()) {
		return appdata.path_join("Jundot").path_join("engine_source");
	}
#endif
	String user_data_dir = OS::get_singleton()->get_user_data_dir();
	if (user_data_dir.is_empty()) {
		return String();
	}
	return user_data_dir.path_join("engine_source");
}

static bool _is_engine_source_root(const String &p_path) {
	return !p_path.is_empty() && FileAccess::exists(p_path.path_join("SConstruct"));
}

static String _find_engine_source_root_in_cache(const String &p_cache_path) {
	if (_is_engine_source_root(p_cache_path)) {
		return p_cache_path;
	}

	Ref<DirAccess> dir = DirAccess::open(p_cache_path);
	if (dir.is_null()) {
		return String();
	}

	dir->list_dir_begin();
	String name = dir->get_next();
	while (!name.is_empty()) {
		if (dir->current_is_dir() && !name.begins_with(".")) {
			String candidate = p_cache_path.path_join(name);
			if (_is_engine_source_root(candidate)) {
				dir->list_dir_end();
				return candidate;
			}
		}
		name = dir->get_next();
	}
	dir->list_dir_end();
	return String();
}

static bool _dir_has_entries(const String &p_path) {
	Ref<DirAccess> dir = DirAccess::open(p_path);
	if (dir.is_null()) {
		return false;
	}

	dir->list_dir_begin();
	String name = dir->get_next();
	while (!name.is_empty()) {
		if (name != "." && name != "..") {
			dir->list_dir_end();
			return true;
		}
		name = dir->get_next();
	}
	dir->list_dir_end();
	return false;
}

static Error _remove_directory_recursive_absolute(const String &p_path, String *r_failed_path = nullptr) {
	if (p_path.is_empty()) {
		return ERR_INVALID_PARAMETER;
	}
	if (r_failed_path) {
		r_failed_path->clear();
	}

	Error open_error = OK;
	Ref<DirAccess> dir = DirAccess::open(p_path, &open_error);
	if (dir.is_null()) {
		FileAccess::set_read_only_attribute(p_path, false);
		const Error err = DirAccess::remove_absolute(p_path);
		if (err != OK && r_failed_path) {
			*r_failed_path = p_path;
		}
		return err;
	}

	List<String> dirs;
	List<String> files;

	dir->list_dir_begin();
	String name = dir->get_next();
	while (!name.is_empty()) {
		if (name != "." && name != "..") {
			if (dir->current_is_dir() && !dir->is_link(name)) {
				dirs.push_back(name);
			} else {
				files.push_back(name);
			}
		}
		name = dir->get_next();
	}
	dir->list_dir_end();

	for (const String &E : dirs) {
		const String child_path = p_path.path_join(E);
		Error err = _remove_directory_recursive_absolute(child_path, r_failed_path);
		if (err != OK) {
			return err;
		}
	}

	for (const String &E : files) {
		const String child_path = p_path.path_join(E);
		FileAccess::set_read_only_attribute(child_path, false);
		Error err = DirAccess::remove_absolute(child_path);
		if (err != OK) {
			if (r_failed_path) {
				*r_failed_path = child_path;
			}
			return err;
		}
	}

	FileAccess::set_read_only_attribute(p_path, false);
	const Error err = DirAccess::remove_absolute(p_path);
	if (err != OK && r_failed_path) {
		*r_failed_path = p_path;
	}
	return err;
}

static Error _collect_encryption_entries(const String &p_path, List<String> &r_dirs, List<String> &r_files) {
	Error open_error = OK;
	Ref<DirAccess> dir = DirAccess::open(p_path, &open_error);
	if (dir.is_null()) {
		if (FileAccess::exists(p_path)) {
			r_files.push_back(p_path);
			return OK;
		}
		return open_error != OK ? open_error : ERR_FILE_NOT_FOUND;
	}

	r_dirs.push_back(p_path);

	dir->list_dir_begin();
	String name = dir->get_next();
	while (!name.is_empty()) {
		if (name != "." && name != "..") {
			const String child_path = p_path.path_join(name);
			if (dir->current_is_dir() && !dir->is_link(name)) {
				Error err = _collect_encryption_entries(child_path, r_dirs, r_files);
				if (err != OK) {
					dir->list_dir_end();
					return err;
				}
			} else {
				r_files.push_back(child_path);
			}
		}
		name = dir->get_next();
	}
	dir->list_dir_end();
	return OK;
}

AISourceManager::SourceStatus AISourceManager::_get_current_status() const {
	if (is_downloading) {
		return SourceStatus::DOWNLOADING;
	}
	if (is_processing) {
		return SourceStatus::EXTRACTING;
	}
	if (!current_error.is_empty()) {
		return SourceStatus::ERROR;
	}

	AISettingsData settings = AISettings::load();
	String cache_path = settings.engine_source_cache_root.strip_edges();
	if (cache_path.is_empty()) {
		cache_path = _get_default_cache_root();
	}

	String source_root = _find_engine_source_root_in_cache(cache_path);
	if (!source_root.is_empty()) {
		return SourceStatus::DOWNLOADED;
	}

	return SourceStatus::NOT_DOWNLOADED;
}

String AISourceManager::_get_source_root() const {
	AISettingsData settings = AISettings::load();
	String cache_path = settings.engine_source_cache_root.strip_edges();
	if (cache_path.is_empty()) {
		cache_path = _get_default_cache_root();
	}
	return _find_engine_source_root_in_cache(cache_path);
}

bool AISourceManager::_is_git_available() const {
	List<String> args;
	args.push_back("--version");

	String output;
	int exit_code = 0;
	const Error err = OS::get_singleton()->execute("git", args, &output, &exit_code, true);
	return err == OK && exit_code == 0;
}

bool AISourceManager::_run_git_status_command(const String &p_working_dir, const List<String> &p_args, String &r_output) const {
	r_output.clear();
	if (p_working_dir.is_empty()) {
		return false;
	}

	List<String> args;
	args.push_back("-C");
	args.push_back(p_working_dir);
	for (const List<String>::Element *E = p_args.front(); E; E = E->next()) {
		args.push_back(E->get());
	}

	int exit_code = 0;
	const Error err = OS::get_singleton()->execute("git", args, &r_output, &exit_code, true);
	if (err != OK || exit_code != 0) {
		r_output.clear();
		return false;
	}

	r_output = r_output.strip_edges();
	return !r_output.is_empty();
}

String AISourceManager::_get_git_update_status_text(const String &p_source_root, const String &p_repository_url, bool &r_update_available) {
	r_update_available = false;

	if (p_source_root.is_empty() || !FileAccess::exists(p_source_root.path_join(".git"))) {
		return TTR("Update Check: ZIP cache or non-Git source. Use Re-download to refresh.");
	}
	if (!_is_git_available()) {
		return TTR("Update Check: Git is not available. Install Git to check and update the source cache.");
	}

	String repo_url = p_repository_url.strip_edges();
	if (repo_url.is_empty()) {
		repo_url = JUNDOT_ENGINE_SOURCE_REPOSITORY_URL;
	}

	const uint64_t now = OS::get_singleton()->get_ticks_msec();
	if (cached_update_check_source_root == p_source_root && cached_update_check_repository_url == repo_url && !cached_update_check_status_text.is_empty() && now - cached_update_check_msec < 300000) {
		r_update_available = cached_update_check_available;
		return cached_update_check_status_text;
	}

	String local_head;
	List<String> local_args;
	local_args.push_back("rev-parse");
	local_args.push_back("HEAD");
	if (!_run_git_status_command(p_source_root, local_args, local_head)) {
		return TTR("Update Check: Could not read local Git revision.");
	}

	String remote_head;
	List<String> remote_args;
	remote_args.push_back("ls-remote");
	remote_args.push_back(repo_url);
	remote_args.push_back("refs/heads/master");
	if (!_run_git_status_command(p_source_root, remote_args, remote_head)) {
		return TTR("Update Check: Could not contact the source repository. Click Update to retry.");
	}
	remote_head = remote_head.get_slice("\t", 0).strip_edges();
	if (remote_head.is_empty()) {
		return TTR("Update Check: Could not read the remote source revision. Click Update to retry.");
	}

	String status_text;
	if (local_head != remote_head) {
		r_update_available = true;
		status_text = TTR("Update Check: Source update available. Click Update to refresh the engine source cache.");
	} else {
		status_text = TTR("Update Check: Engine source cache is up to date.");
	}

	cached_update_check_source_root = p_source_root;
	cached_update_check_repository_url = repo_url;
	cached_update_check_status_text = status_text;
	cached_update_check_available = r_update_available;
	cached_update_check_msec = now;
	return status_text;
}

void AISourceManager::_clear_update_check_cache() {
	cached_update_check_source_root.clear();
	cached_update_check_repository_url.clear();
	cached_update_check_status_text.clear();
	cached_update_check_available = false;
	cached_update_check_msec = 0;
}

Error AISourceManager::_run_git_process(const List<String> &p_args, const String &p_command, String &r_output, int *r_exit_code) {
	r_output.clear();
	if (r_exit_code) {
		*r_exit_code = -1;
	}

	_defer_git_log(p_command, String(), OK);

	Dictionary pipe_info = OS::get_singleton()->execute_with_pipe("git", p_args, false);
	if (pipe_info.is_empty() || !pipe_info.has("pid")) {
		_defer_git_log(String(), TTR("Failed to start Git process."), FAILED);
		return ERR_CANT_FORK;
	}

	const ProcessID pid = pipe_info["pid"];
	Ref<FileAccess> stdio_pipe = pipe_info.get("stdio", Ref<FileAccess>());
	Ref<FileAccess> stderr_pipe = pipe_info.get("stderr", Ref<FileAccess>());
	if (pid == 0 || stdio_pipe.is_null() || stderr_pipe.is_null()) {
		_defer_git_log(String(), TTR("Git process returned invalid pipe handles."), FAILED);
		return ERR_CANT_OPEN;
	}

	while (!shutdown_requested.is_set()) {
		bool read_any = false;

		const uint64_t stdout_available = stdio_pipe->get_length();
		if (stdout_available > 0) {
			PackedByteArray buffer;
			buffer.resize(stdout_available);
			const uint64_t bytes_read = stdio_pipe->get_buffer(buffer.ptrw(), buffer.size());
			if (bytes_read > 0) {
				String chunk;
				chunk.append_utf8((const char *)buffer.ptr(), bytes_read);
				r_output += chunk;
				_defer_git_log(String(), chunk, OK);
				read_any = true;
			}
		}

		const uint64_t stderr_available = stderr_pipe->get_length();
		if (stderr_available > 0) {
			PackedByteArray buffer;
			buffer.resize(stderr_available);
			const uint64_t bytes_read = stderr_pipe->get_buffer(buffer.ptrw(), buffer.size());
			if (bytes_read > 0) {
				String chunk;
				chunk.append_utf8((const char *)buffer.ptr(), bytes_read);
				r_output += chunk;
				_defer_git_log(String(), chunk, OK);
				read_any = true;
			}
		}

		if (!OS::get_singleton()->is_process_running(pid)) {
			break;
		}
		if (!read_any) {
			OS::get_singleton()->delay_usec(50000);
		}
	}

	// Drain any remaining buffered output after the process exits.
	for (int i = 0; i < 8; i++) {
		bool read_any = false;

		const uint64_t stdout_available = stdio_pipe->get_length();
		if (stdout_available > 0) {
			PackedByteArray buffer;
			buffer.resize(stdout_available);
			const uint64_t bytes_read = stdio_pipe->get_buffer(buffer.ptrw(), buffer.size());
			if (bytes_read > 0) {
				String chunk;
				chunk.append_utf8((const char *)buffer.ptr(), bytes_read);
				r_output += chunk;
				_defer_git_log(String(), chunk, OK);
				read_any = true;
			}
		}

		const uint64_t stderr_available = stderr_pipe->get_length();
		if (stderr_available > 0) {
			PackedByteArray buffer;
			buffer.resize(stderr_available);
			const uint64_t bytes_read = stderr_pipe->get_buffer(buffer.ptrw(), buffer.size());
			if (bytes_read > 0) {
				String chunk;
				chunk.append_utf8((const char *)buffer.ptr(), bytes_read);
				r_output += chunk;
				_defer_git_log(String(), chunk, OK);
				read_any = true;
			}
		}

		if (!read_any) {
			break;
		}
	}

	const int exit_code = OS::get_singleton()->get_process_exit_code(pid);
	if (r_exit_code) {
		*r_exit_code = exit_code;
	}
	if (shutdown_requested.is_set()) {
		OS::get_singleton()->kill(pid);
		return ERR_SKIP;
	}
	if (exit_code != 0) {
		_defer_git_log(String(), vformat("[exit: %d]", exit_code), OK);
		return FAILED;
	}
	return OK;
}

Error AISourceManager::_run_git_command(const String &p_working_dir, const List<String> &p_args, String &r_output, int *r_exit_code) {
	List<String> args;
	args.push_back("-C");
	args.push_back(p_working_dir);
	String command = "git -C " + p_working_dir;
	for (const List<String>::Element *E = p_args.front(); E; E = E->next()) {
		args.push_back(E->get());
		command += " " + E->get();
	}

	int exit_code = 0;
	const Error err = _run_git_process(args, command, r_output, &exit_code);
	if (r_exit_code) {
		*r_exit_code = exit_code;
	}
	if (err != OK) {
		return err;
	}
	return exit_code == 0 ? OK : FAILED;
}

Error AISourceManager::_update_git_cache(const String &p_cache_path, const String &p_repository_url, String &r_source_root, String &r_error) {
	_defer_processing_progress(0, TTR("Checking Git availability..."));
	if (!_is_git_available()) {
		r_error = TTR("Git is not available.");
		return ERR_UNAVAILABLE;
	}

	_defer_processing_progress(1, TTR("Preparing Git source cache directory..."));
	Error err = DirAccess::make_dir_recursive_absolute(p_cache_path);
	if (err != OK) {
		r_error = TTR("Could not create cache directory.");
		return err;
	}

	String repo_url = p_repository_url.strip_edges();
	if (repo_url.is_empty()) {
		repo_url = JUNDOT_ENGINE_SOURCE_REPOSITORY_URL;
	}

	String repo_path = p_cache_path;
	if (!FileAccess::exists(repo_path.path_join(".git")) && _dir_has_entries(repo_path)) {
		repo_path = p_cache_path.path_join("Jundot");
		DirAccess::make_dir_recursive_absolute(repo_path);
	}

	// Shallow clone + partial-blob filter reduces initial download while
	// still producing a complete working-tree checkout.
	//   --depth=1:         only the latest commit; no history needed.
	//   --filter=blob:none: blob content streamed on-demand during checkout
	//                       (trees are small and fetched up front).
	String output;
	bool requires_full_clone = !FileAccess::exists(repo_path.path_join(".git"));
	if (!requires_full_clone) {
		// Existing cache: check whether the configured repository URL
		// still matches the local origin. If not, treat this as a
		// "first-time clone" and wipe the old cache to start fresh.
		String current_origin;
		{
			List<String> get_url_args;
			get_url_args.push_back("remote");
			get_url_args.push_back("get-url");
			get_url_args.push_back("origin");
			int url_exit = 0;
			_run_git_command(repo_path, get_url_args, output, &url_exit);
			if (url_exit == 0) {
				current_origin = output.strip_edges();
			}
		}

		if (!current_origin.is_empty() && current_origin != repo_url) {
			_defer_processing_indeterminate(vformat(TTR("Repository URL changed. Removing old cache and cloning from %s..."), repo_url));
			DirAccess::remove_absolute(repo_path);
			// Re-create an empty cache directory so the clone target exists.
			DirAccess::make_dir_recursive_absolute(repo_path);
			requires_full_clone = true;
		}
	}

	if (requires_full_clone) {
		_defer_processing_indeterminate(TTR("Cloning engine source with Git... This may take several minutes."));
		List<String> clone_args;
		clone_args.push_back("-c");
		clone_args.push_back("protocol.version=2");
		clone_args.push_back("-c");
		clone_args.push_back("core.compression=1");
		clone_args.push_back("clone");
		clone_args.push_back("--depth=1");
		clone_args.push_back("--single-branch");
		clone_args.push_back("--branch");
		clone_args.push_back("master");
		clone_args.push_back("--no-tags");
		clone_args.push_back("--filter=blob:none");
		clone_args.push_back(repo_url);
		clone_args.push_back(repo_path);

		int exit_code = 0;
		err = _run_git_process(clone_args, "git -c protocol.version=2 -c core.compression=1 clone --depth=1 --single-branch --branch master --no-tags --filter=blob:none " + repo_url + " " + repo_path, output, &exit_code);
		if (err != OK || exit_code != 0) {
			r_error = output.strip_edges();
			if (r_error.is_empty()) {
				r_error = TTR("Git clone failed.");
			}
			return err != OK ? err : FAILED;
		}
	} else {
		// Ensure the local origin remote matches the configured repository URL.
		{
			List<String> set_url_args;
			set_url_args.push_back("remote");
			set_url_args.push_back("set-url");
			set_url_args.push_back("origin");
			set_url_args.push_back(repo_url);
			int set_url_exit = 0;
			_run_git_command(repo_path, set_url_args, output, &set_url_exit);
			// Non-fatal: if 'origin' doesn't exist yet, add it.
			if (set_url_exit != 0) {
				List<String> add_url_args;
				add_url_args.push_back("remote");
				add_url_args.push_back("add");
				add_url_args.push_back("origin");
				add_url_args.push_back(repo_url);
				_run_git_command(repo_path, add_url_args, output, &set_url_exit);
			}
		}

		_defer_processing_indeterminate(TTR("Fetching latest JunDot engine source with Git..."));
		List<String> fetch_args;
		fetch_args.push_back("-c");
		fetch_args.push_back("protocol.version=2");
		fetch_args.push_back("fetch");
		fetch_args.push_back("--depth=1");
		fetch_args.push_back("--no-tags");
		fetch_args.push_back("--prune");
		fetch_args.push_back("--filter=blob:none");
		fetch_args.push_back("origin");
		fetch_args.push_back("master");
		err = _run_git_command(repo_path, fetch_args, output);
		if (err != OK) {
			r_error = output.strip_edges();
			if (r_error.is_empty()) {
				r_error = TTR("Git fetch failed.");
			}
			return err;
		}

		_defer_processing_indeterminate(TTR("Resetting Git source cache to the latest revision..."));
		List<String> reset_args;
		reset_args.push_back("reset");
		reset_args.push_back("--hard");
		reset_args.push_back("origin/master");
		err = _run_git_command(repo_path, reset_args, output);
		if (err != OK) {
			r_error = output.strip_edges();
			if (r_error.is_empty()) {
				r_error = TTR("Git reset failed.");
			}
			return err;
		}
	}

	r_source_root = _is_engine_source_root(repo_path) ? repo_path : _find_engine_source_root_in_cache(repo_path);
	if (r_source_root.is_empty()) {
		r_error = TTR("Git cache does not contain a valid engine source root.");
		return ERR_FILE_NOT_FOUND;
	}
	_defer_processing_progress(3, TTR("Git source cache update complete."));
	_defer_processing_progress(4, TTR("Git source cache update complete. Encrypting cache..."));
	return OK;
}

void AISourceManager::_update_ui() {
	SourceStatus status = _get_current_status();
	AISettingsData settings = AISettings::load();
	String cache_path = settings.engine_source_cache_root.strip_edges();
	if (cache_path.is_empty()) {
		cache_path = _get_default_cache_root();
	}

	String repo_url = settings.engine_source_repository_url.strip_edges();
	if (repo_url.is_empty()) {
		repo_url = JUNDOT_ENGINE_SOURCE_REPOSITORY_URL;
	}

	cache_path_edit->set_text(cache_path);
	if (repository_url_edit) {
		repository_url_edit->set_text(repo_url);
	}

	switch (status) {
		case SourceStatus::NOT_DOWNLOADED:
			status_label->set_text(TTR("Status: Not Downloaded"));
			download_button->set_text(TTR("Clone Engine Source"));
			download_button->set_disabled(false);
			delete_button->set_disabled(true);
			browse_button->set_disabled(false);
			cache_path_edit->set_editable(true);
			if (repository_url_edit) {
				repository_url_edit->set_editable(true);
			}
			download_progress->set_indeterminate(false);
			download_progress->set_visible(false);
			download_status_label->set_visible(false);
			if (git_log_scroll) {
				git_log_scroll->set_visible(false);
			}
			break;

		case SourceStatus::DOWNLOADING:
			status_label->set_text(TTR("Status: Downloading ZIP fallback..."));
			download_button->set_text(TTR("Cancel"));
			download_button->set_disabled(false);
			delete_button->set_disabled(true);
			browse_button->set_disabled(true);
			cache_path_edit->set_editable(false);
			if (repository_url_edit) {
				repository_url_edit->set_editable(false);
			}
			download_progress->set_indeterminate(false);
			download_progress->set_visible(true);
			download_status_label->set_visible(true);
			if (git_log_scroll) {
				git_log_scroll->set_visible(false);
			}
			break;

		case SourceStatus::EXTRACTING:
		case SourceStatus::ENCRYPTING:
			status_label->set_text(TTR("Status: Processing source cache..."));
			download_button->set_text(TTR("Working..."));
			download_button->set_disabled(true);
			delete_button->set_disabled(true);
			browse_button->set_disabled(true);
			cache_path_edit->set_editable(false);
			if (repository_url_edit) {
				repository_url_edit->set_editable(false);
			}
			download_progress->set_visible(true);
			download_status_label->set_visible(true);
			if (git_log_scroll && !worker_git_log.is_empty()) {
				git_log_scroll->set_visible(true);
			}
			break;

		case SourceStatus::DOWNLOADED: {
			const String source_root = _get_source_root();
			bool update_available = false;
			const String update_status = _get_git_update_status_text(source_root, repo_url, update_available);
			status_label->set_text(vformat(TTR("Status: Downloaded\nSource Root: %s\n%s"), source_root, update_status));
			String saved_url = settings.engine_source_repository_url.strip_edges();
			if (saved_url.is_empty()) {
				saved_url = JUNDOT_ENGINE_SOURCE_REPOSITORY_URL;
			}
			// If the user changed the repository URL since the last clone,
			// the next "Update" will actually wipe and re-clone, so reflect
			// that in the button label.
			if (saved_url != repo_url) {
				download_button->set_text(TTR("Re-clone with New URL"));
			} else if (update_available) {
				download_button->set_text(TTR("Update Source"));
			} else {
				download_button->set_text(TTR("Update"));
			}
			download_button->set_disabled(false);
			delete_button->set_disabled(false);
			browse_button->set_disabled(false);
			cache_path_edit->set_editable(true);
			if (repository_url_edit) {
				repository_url_edit->set_editable(true);
			}
			download_progress->set_indeterminate(false);
			download_progress->set_visible(false);
			download_status_label->set_visible(false);
			if (git_log_scroll) {
				git_log_scroll->set_visible(false);
			}
		} break;

		case SourceStatus::ERROR:
			status_label->set_text(TTR("Status: Error"));
			download_button->set_text(TTR("Retry"));
			download_button->set_disabled(false);
			delete_button->set_disabled(false);
			browse_button->set_disabled(false);
			cache_path_edit->set_editable(true);
			if (repository_url_edit) {
				repository_url_edit->set_editable(true);
			}
			download_progress->set_indeterminate(false);
			download_progress->set_visible(false);
			download_status_label->set_visible(true);
			download_status_label->set_text(TTR("Operation failed. See details below."));
			if (git_log_label) {
				git_log_label->set_text(current_error);
			}
			if (git_log_scroll) {
				git_log_scroll->set_visible(true);
			}
			break;

		default:
			break;
	}

	_fit_to_contents();
}

void AISourceManager::_fit_to_contents() {
	if (!is_visible() || !main_vbox) {
		return;
	}

	const Size2i base_size = Size2(640, 360) * EDSCALE;
	const Size2i content_size = main_vbox->get_combined_minimum_size() + Size2(48, 96) * EDSCALE;
	Size2i desired_size = base_size.max(content_size);
	const Rect2i usable_parent_rect = get_usable_parent_rect();
	if (usable_parent_rect.size != Size2i()) {
		desired_size = desired_size.min(Size2i(usable_parent_rect.size * 0.9));
	}
	set_size(desired_size);
}

void AISourceManager::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_VISIBILITY_CHANGED: {
			if (is_visible()) {
				_update_ui();
			} else {
				_cleanup_on_close();
			}
		} break;

		case NOTIFICATION_PROCESS: {
			_update_download_progress();
		} break;

		case NOTIFICATION_PREDELETE: {
			shutdown_requested.set();
			if (is_downloading && downloader) {
				downloader->cancel();
				is_downloading = false;
				set_process(false);
			}
			if (worker_thread) {
				worker_thread->wait_to_finish();
				memdelete(worker_thread);
				worker_thread = nullptr;
			}
		} break;
	}
}

void AISourceManager::_on_browse_button_pressed() {
	cache_dir_dialog->set_current_dir(cache_path_edit->get_text().strip_edges());
	cache_dir_dialog->popup_file_dialog();
}

void AISourceManager::_on_cache_dir_selected(const String &p_path) {
	current_error.clear();
	_clear_update_check_cache();
	cache_path_edit->set_text(p_path);
	AISettingsData settings = AISettings::load();
	settings.engine_source_cache_root = p_path;
	AISettings::save(settings);
	_update_ui();
}

void AISourceManager::_on_reset_url_button_pressed() {
	if (repository_url_edit) {
		repository_url_edit->set_text(JUNDOT_ENGINE_SOURCE_REPOSITORY_URL);
	}
}

void AISourceManager::_on_download_button_pressed() {
	current_error.clear();
	_clear_update_check_cache();

	if (is_downloading) {
		if (downloader) {
			downloader->cancel();
		}
		is_downloading = false;
		set_process(false);
		download_progress->set_visible(false);
		download_status_label->set_visible(false);
		_update_ui();
		return;
	}

	String cache_path = cache_path_edit->get_text().strip_edges();
	if (cache_path.is_empty()) {
		cache_path = _get_default_cache_root();
	}

	String repo_url = JUNDOT_ENGINE_SOURCE_REPOSITORY_URL;
	if (repository_url_edit) {
		String user_url = repository_url_edit->get_text().strip_edges();
		if (!user_url.is_empty()) {
			repo_url = user_url;
		}
	}

	Error err = DirAccess::make_dir_recursive_absolute(cache_path);
	if (err != OK) {
		current_error = TTR("Could not create cache directory.");
		_update_ui();
		return;
	}

	AISettingsData settings = AISettings::load();
	settings.engine_source_cache_root = cache_path;
	settings.engine_source_repository_url = repo_url;
	AISettings::save(settings);

	if (_is_git_available()) {
		_start_git_update(cache_path, repo_url);
		return;
	}

	pending_zip_cache_path = cache_path;
	pending_zip_url = repo_url;
	git_install_prompt_dialog->popup_centered_clamped(Size2(520, 180) * EDSCALE, 0.9);
}

void AISourceManager::_on_git_install_confirmed() {
	OS::get_singleton()->shell_open("https://git-scm.com/download/win");
	download_status_label->set_text(TTR("Git installer page opened. Install Git, restart JunDot, then try Download / Update again."));
	download_status_label->set_visible(true);
}

void AISourceManager::_on_git_install_declined() {
	if (pending_zip_cache_path.is_empty()) {
		pending_zip_cache_path = cache_path_edit->get_text().strip_edges();
	}
	if (pending_zip_cache_path.is_empty()) {
		pending_zip_cache_path = _get_default_cache_root();
	}
	if (pending_zip_url.is_empty()) {
		if (repository_url_edit) {
			pending_zip_url = repository_url_edit->get_text().strip_edges();
		}
		if (pending_zip_url.is_empty()) {
			pending_zip_url = JUNDOT_ENGINE_SOURCE_REPOSITORY_URL;
		}
	}
	_start_zip_fallback(pending_zip_cache_path, pending_zip_url);
}

void AISourceManager::_start_zip_fallback(const String &p_cache_path, const String &p_repository_url) {
	download_zip_path = p_cache_path.path_join("jundot_engine_source.zip");
	download_progress->set_indeterminate(false);
	download_progress->set_min(0);
	download_progress->set_max(1);
	download_progress->set_value(0);
	download_progress->set_visible(true);
	download_status_label->set_text(TTR("Git was not found. Falling back to ZIP download..."));
	download_status_label->set_visible(true);

	String zip_url = JUNDOT_ENGINE_SOURCE_ZIP_URL;
	String repo_url = p_repository_url.strip_edges();
	if (!repo_url.is_empty()) {
		// Derive ZIP archive URL from the Git URL (GitHub-style).
		String base = repo_url;
		if (base.ends_with(".git")) {
			base = base.substr(0, base.length() - 4);
		}
		zip_url = base + "/archive/refs/heads/master.zip";
	}

	const Error err = downloader->start(zip_url, download_zip_path, 16);
	if (err != OK) {
		current_error = TTR("Could not start ZIP download.");
		download_progress->set_visible(false);
		download_status_label->set_visible(false);
		_update_ui();
		return;
	}

	is_downloading = true;
	set_process(true);
	_update_ui();
}

void AISourceManager::_on_delete_button_pressed() {
	String cache_path = cache_path_edit ? cache_path_edit->get_text().strip_edges() : String();
	if (cache_path.is_empty()) {
		AISettingsData settings = AISettings::load();
		cache_path = settings.engine_source_cache_root.strip_edges();
	}
	if (cache_path.is_empty()) {
		cache_path = _get_default_cache_root();
	}

	String source_root = _find_engine_source_root_in_cache(cache_path);
	if (source_root.is_empty()) {
		current_error = TTR("No source cache to delete.");
		_update_ui();
		return;
	}

	String failed_path;
	Error err = _remove_directory_recursive_absolute(cache_path, &failed_path);
	if (err != OK) {
		if (failed_path.is_empty()) {
			current_error = vformat(TTR("Failed to delete cache: error %d"), err);
		} else {
			current_error = vformat(TTR("Failed to delete cache: error %d\nPath: %s"), err, failed_path);
		}
		_update_ui();
		return;
	}

	AISettingsData settings = AISettings::load();
	settings.engine_source_root = "";
	AISettings::save(settings);

	current_error.clear();
	_update_ui();
}

void AISourceManager::_update_download_progress() {
	if (!is_downloading || !downloader) {
		return;
	}

	const int64_t downloaded = (int64_t)downloader->get_downloaded_bytes();
	const int64_t total = (int64_t)downloader->get_total_bytes();

	if (!download_progress->is_visible()) {
		download_progress->set_visible(true);
		download_status_label->set_visible(true);
	}

	if (total > 0) {
		download_progress->set_max(total);
		download_progress->set_value(downloaded);
		download_status_label->set_text(vformat(TTR("Downloading ZIP fallback... %s / %s (%d connections)"),
				String::humanize_size(downloaded), String::humanize_size(total), 6));
	} else {
		download_progress->set_max(1);
		download_progress->set_value(0);
		download_status_label->set_text(vformat(TTR("Downloading ZIP fallback... %s (calculating...)"),
				String::humanize_size(downloaded)));
	}
}

void AISourceManager::_on_download_progress(int64_t p_downloaded, int64_t p_total) {
	if (!download_progress->is_visible()) {
		download_progress->set_visible(true);
		download_status_label->set_visible(true);
	}

	if (p_total > 0) {
		download_progress->set_max(p_total);
		download_progress->set_value(p_downloaded);
		download_status_label->set_text(vformat(TTR("Downloading ZIP fallback... %s / %s (%d connections)"),
				String::humanize_size(p_downloaded), String::humanize_size(p_total), 6));
	}
}

void AISourceManager::_on_download_finished(const String &p_output_path) {
	is_downloading = false;
	set_process(false);

	download_zip_path = p_output_path;
	String cache_path = cache_path_edit->get_text().strip_edges();
	if (cache_path.is_empty()) {
		cache_path = _get_default_cache_root();
	}

	download_progress->set_value(download_progress->get_max());
	download_status_label->set_text(TTR("ZIP download complete. Extracting..."));
	download_status_label->set_visible(true);

	_start_post_download_processing(cache_path);
}

void AISourceManager::_on_download_failed(const String &p_reason) {
	is_downloading = false;
	set_process(false);
	current_error = p_reason;
	download_progress->set_visible(false);
	download_status_label->set_visible(false);
	_update_ui();
}

void AISourceManager::_start_git_update(const String &p_cache_path, const String &p_repository_url) {
	shutdown_requested.clear();
	is_processing = true;
	worker_git_mode = true;
	worker_cache_path = p_cache_path;
	worker_repository_url = p_repository_url;
	worker_error = OK;
	worker_error_msg = "";
	worker_source_root = "";
	worker_git_log = "";

	status_label->set_text(TTR("Status: Updating with Git..."));
	download_progress->set_indeterminate(false);
	download_progress->set_min(0);
	download_progress->set_max(5);
	download_progress->set_value(0);
	download_progress->set_visible(true);
	download_status_label->set_text(TTR("Preparing Git source cache update..."));
	download_status_label->set_visible(true);
	if (git_log_scroll) {
		git_log_scroll->set_visible(false);
	}
	if (git_log_label) {
		git_log_label->set_text("");
	}

	worker_thread = memnew(Thread);
	worker_thread->start(_worker_thread_static_func, this);
	_update_ui();
}

Error AISourceManager::_extract_zip(const String &p_zip_path, const String &p_cache_path, String &r_source_root) {
	Ref<ZIPReader> zip;
	zip.instantiate();
	Error err = zip->open(p_zip_path);
	ERR_FAIL_COND_V(err != OK, err);

	const PackedStringArray files = zip->get_files();
	for (int i = 0; i < files.size(); i++) {
		String zip_path = files[i].replace("\\", "/");
		if (zip_path.is_empty() || zip_path.contains("..")) {
			continue;
		}

		const int slash = zip_path.find("/");
		if (slash < 0 || slash == zip_path.length() - 1) {
			continue;
		}

		const String relative_path = zip_path.substr(slash + 1);
		if (relative_path.is_empty()) {
			continue;
		}

		const String destination = p_cache_path.path_join(relative_path);
		if (zip_path.ends_with("/")) {
			DirAccess::make_dir_recursive_absolute(destination);
			continue;
		}

		err = DirAccess::make_dir_recursive_absolute(destination.get_base_dir());
		ERR_FAIL_COND_V(err != OK, err);

		Ref<FileAccess> file = FileAccess::open(destination, FileAccess::WRITE, &err);
		ERR_FAIL_COND_V(err != OK || file.is_null(), err != OK ? err : ERR_CANT_OPEN);

		const PackedByteArray bytes = zip->read_file(files[i], true);
		file->store_buffer(bytes);
	}

	zip->close();

	r_source_root = _is_engine_source_root(p_cache_path) ? p_cache_path : _find_engine_source_root_in_cache(p_cache_path);
	return r_source_root.is_empty() ? ERR_FILE_NOT_FOUND : OK;
}

Error AISourceManager::_encrypt_cache(const String &p_cache_path, String &r_error) {
#ifdef WINDOWS_ENABLED
	List<String> dirs;
	List<String> files;
	Error err = _collect_encryption_entries(p_cache_path, dirs, files);
	if (err != OK) {
		r_error = vformat(TTR("Could not scan source cache before encryption: error %d"), err);
		return err;
	}

	const int total = dirs.size() + files.size();
	if (total == 0) {
		return OK;
	}

	int processed = 0;
	uint64_t last_progress_msec = 0;
	_defer_processing_progress_units(processed, total, vformat(TTR("Encrypting engine source cache... %d / %d"), processed, total));

	auto encrypt_path = [&](const String &p_path) -> Error {
		if (shutdown_requested.is_set()) {
			return ERR_SKIP;
		}

		FileAccess::set_read_only_attribute(p_path, false);
		const Char16String path_utf16 = p_path.utf16();
		if (!EncryptFileW((LPCWSTR)path_utf16.get_data())) {
			const DWORD windows_error = GetLastError();
			r_error = vformat(TTR("Could not encrypt path: %s (Windows error %d)"), p_path, (int)windows_error);
			return FAILED;
		}

		processed++;
		const uint64_t now = OS::get_singleton()->get_ticks_msec();
		if (processed == total || processed % 25 == 0 || now - last_progress_msec >= 250) {
			last_progress_msec = now;
			_defer_processing_progress_units(processed, total, vformat(TTR("Encrypting engine source cache... %d / %d"), processed, total));
		}
		return OK;
	};

	for (const String &E : dirs) {
		err = encrypt_path(E);
		if (err != OK) {
			return err;
		}
	}
	for (const String &E : files) {
		err = encrypt_path(E);
		if (err != OK) {
			return err;
		}
	}

	return OK;
#else
	r_error = "Automatic encrypted source cache is only implemented on Windows.";
	return ERR_UNAVAILABLE;
#endif
}

void AISourceManager::_start_post_download_processing(const String &p_cache_path) {
	shutdown_requested.clear();
	is_processing = true;
	worker_git_mode = false;
	worker_cache_path = p_cache_path;
	worker_error = OK;
	worker_error_msg = "";
	worker_source_root = "";
	worker_git_log = "";

	status_label->set_text(TTR("Status: Extracting ZIP fallback..."));
	download_progress->set_indeterminate(false);
	download_progress->set_min(0);
	download_progress->set_max(3);
	download_progress->set_value(1);
	download_progress->set_visible(true);
	download_status_label->set_text(TTR("Extracting engine source ZIP..."));
	download_status_label->set_visible(true);

	worker_thread = memnew(Thread);
	worker_thread->start(_worker_thread_static_func, this);
}

void AISourceManager::_worker_thread_static_func(void *p_userdata) {
	AISourceManager *self = static_cast<AISourceManager *>(p_userdata);
	self->_worker_thread_func();
}

void AISourceManager::_worker_thread_func() {
	String source_root;
	Error err = OK;

	if (worker_git_mode) {
		String git_error;
		err = _update_git_cache(worker_cache_path, worker_repository_url, source_root, git_error);
		if (err != OK || source_root.is_empty()) {
			worker_error = err != OK ? err : FAILED;
			worker_error_msg = git_error.is_empty() ? TTR("Git update failed.") : git_error;
			_defer_processing_completed();
			return;
		}
	} else {
		err = _extract_zip(download_zip_path, worker_cache_path, source_root);
		if (err != OK || source_root.is_empty()) {
			worker_error = err != OK ? err : FAILED;
			worker_error_msg = TTR("ZIP extraction failed.");
			_defer_processing_completed();
			return;
		}
		DirAccess::remove_absolute(download_zip_path);
	}

	String encryption_error;
	_defer_processing_progress(worker_git_mode ? 4 : 2, TTR("Encrypting engine source cache..."));
	err = _encrypt_cache(worker_cache_path, encryption_error);
	if (err != OK) {
		worker_error = err;
		worker_error_msg = vformat(TTR("Source cache updated, but encryption failed: %s"), encryption_error);
		_defer_processing_completed();
		return;
	}

	worker_source_root = source_root;
	_defer_processing_progress(worker_git_mode ? 5 : 3, TTR("Engine source cache is ready."));
	_defer_processing_completed();
}

void AISourceManager::_set_processing_progress(int p_step, const String &p_status) {
	if (!download_progress || !download_status_label) {
		return;
	}

	download_progress->set_visible(true);
	download_progress->set_indeterminate(false);
	if (download_progress->get_max() < p_step) {
		download_progress->set_max(p_step);
	}
	download_progress->set_value(p_step);
	download_status_label->set_text(p_status);
	download_status_label->set_visible(true);
	_fit_to_contents();
}

void AISourceManager::_set_processing_progress_units(int p_current, int p_total, const String &p_status) {
	if (!download_progress || !download_status_label) {
		return;
	}

	download_progress->set_visible(true);
	download_progress->set_indeterminate(false);
	download_progress->set_min(0);
	download_progress->set_max(MAX(1, p_total));
	download_progress->set_value(CLAMP(p_current, 0, MAX(1, p_total)));
	download_status_label->set_text(p_status);
	download_status_label->set_visible(true);
	_fit_to_contents();
}

void AISourceManager::_set_processing_indeterminate(const String &p_status) {
	if (!download_progress || !download_status_label) {
		return;
	}

	download_progress->set_visible(true);
	download_progress->set_indeterminate(true);
	download_status_label->set_text(p_status);
	download_status_label->set_visible(true);
	_fit_to_contents();
}

void AISourceManager::_append_git_log(const String &p_log) {
	if (p_log.is_empty()) {
		return;
	}

	if (!worker_git_log.is_empty()) {
		worker_git_log += "\n";
	}
	worker_git_log += p_log;

	const int max_log_chars = 6000;
	if (worker_git_log.length() > max_log_chars) {
		worker_git_log = "... log truncated ...\n" + worker_git_log.right(max_log_chars);
	}

	if (git_log_label) {
		git_log_label->set_text(worker_git_log);
	}
	if (git_log_scroll) {
		const bool was_visible = git_log_scroll->is_visible();
		git_log_scroll->set_visible(true);
		git_log_scroll->set_deferred(SNAME("scroll_vertical"), git_log_scroll->get_v_scroll_bar()->get_max());
		if (!was_visible) {
			_fit_to_contents();
		}
	}
}

void AISourceManager::_defer_processing_progress(int p_step, const String &p_status) {
	if (shutdown_requested.is_set()) {
		return;
	}
	callable_mp(this, &AISourceManager::_set_processing_progress).call_deferred(p_step, p_status);
}

void AISourceManager::_defer_processing_progress_units(int p_current, int p_total, const String &p_status) {
	if (shutdown_requested.is_set()) {
		return;
	}
	callable_mp(this, &AISourceManager::_set_processing_progress_units).call_deferred(p_current, p_total, p_status);
}

void AISourceManager::_defer_processing_indeterminate(const String &p_status) {
	if (shutdown_requested.is_set()) {
		return;
	}
	callable_mp(this, &AISourceManager::_set_processing_indeterminate).call_deferred(p_status);
}

void AISourceManager::_defer_git_log(const String &p_command, const String &p_output, Error p_error) {
	if (shutdown_requested.is_set()) {
		return;
	}

	String log;
	if (!p_command.is_empty()) {
		log = "$ " + p_command;
	}
	if (!p_output.strip_edges().is_empty()) {
		if (!log.is_empty()) {
			log += "\n";
		}
		log += p_output.strip_edges();
	}
	if (p_error != OK) {
		if (!log.is_empty()) {
			log += "\n";
		}
		log += vformat("[error: %d]", (int)p_error);
	}
	if (log.is_empty()) {
		return;
	}
	callable_mp(this, &AISourceManager::_append_git_log).call_deferred(log);
}

void AISourceManager::_defer_processing_completed() {
	if (shutdown_requested.is_set()) {
		return;
	}
	callable_mp(this, &AISourceManager::_on_processing_completed).call_deferred();
}

void AISourceManager::_on_processing_completed() {
	if (shutdown_requested.is_set()) {
		return;
	}
	is_processing = false;
	_clear_update_check_cache();

	if (worker_thread) {
		worker_thread->wait_to_finish();
		memdelete(worker_thread);
		worker_thread = nullptr;
	}

	if (worker_error != OK) {
		current_error = worker_error_msg;
		download_progress->set_visible(false);
		download_status_label->set_visible(false);
		_update_ui();
		return;
	}

	AISettingsData settings = AISettings::load();
	settings.engine_source_root = worker_source_root;
	settings.engine_source_cache_root = worker_cache_path;
	String saved_url = worker_repository_url.strip_edges();
	if (saved_url.is_empty()) {
		saved_url = JUNDOT_ENGINE_SOURCE_REPOSITORY_URL;
	}
	settings.engine_source_repository_url = saved_url;
	settings.encrypt_engine_source_cache = true;
	AISettings::save(settings);

	current_error.clear();
	status_label->set_text(TTR("Status: Ready"));
	download_progress->set_indeterminate(false);
	download_progress->set_visible(false);
	download_status_label->set_visible(false);
	_update_ui();
}

void AISourceManager::popup_centered_on_parent(const Window *p_parent) {
	(void)p_parent;
	_update_ui();
	const Size2 base_size = Size2(640, 360) * EDSCALE;
	const Size2 content_size = main_vbox ? main_vbox->get_combined_minimum_size() + Size2(48, 96) * EDSCALE : Size2();
	popup_centered_clamped(base_size.max(content_size), 0.9);
}

AISourceManager::AISourceManager() {
	set_title(TTR("AI Engine Source Manager"));
	set_ok_button_text(TTR("Close"));

	main_vbox = memnew(VBoxContainer);
	main_vbox->add_theme_constant_override("separation", 6);
	add_child(main_vbox);

	// Status section.
	{
		status_label = memnew(Label);
		status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		status_label->set_custom_minimum_size(Size2(0, 36) * EDSCALE);
		main_vbox->add_child(status_label);
	}

	// Repository URL section (user-configurable, defaults to Jundot).
	{
		HBoxContainer *url_hbox = memnew(HBoxContainer);
		url_hbox->add_theme_constant_override("separation", 6);
		main_vbox->add_child(url_hbox);

		Label *url_label = memnew(Label(TTR("Repository URL:")));
		url_label->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
		url_hbox->add_child(url_label);

		repository_url_edit = memnew(LineEdit);
		repository_url_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		repository_url_edit->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
		repository_url_edit->set_placeholder(JUNDOT_ENGINE_SOURCE_REPOSITORY_URL);
		url_hbox->add_child(repository_url_edit);

		Button *reset_url_button = memnew(Button);
		reset_url_button->set_text(TTR("Reset"));
		reset_url_button->connect(SceneStringName(pressed), callable_mp(this, &AISourceManager::_on_reset_url_button_pressed));
		url_hbox->add_child(reset_url_button);
	}

	// Cache path section.
	{
		HBoxContainer *path_hbox = memnew(HBoxContainer);
		path_hbox->add_theme_constant_override("separation", 6);
		main_vbox->add_child(path_hbox);

		Label *path_label = memnew(Label(TTR("Cache Path:")));
		path_label->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
		path_hbox->add_child(path_label);

		cache_path_edit = memnew(LineEdit);
		cache_path_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		cache_path_edit->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
		path_hbox->add_child(cache_path_edit);

		browse_button = memnew(Button);
		browse_button->set_text(TTR("Browse"));
		browse_button->connect(SceneStringName(pressed), callable_mp(this, &AISourceManager::_on_browse_button_pressed));
		path_hbox->add_child(browse_button);
	}

	// Download progress section.
	{
		download_progress = memnew(ProgressBar);
		download_progress->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		download_progress->set_visible(false);
		main_vbox->add_child(download_progress);

		download_status_label = memnew(Label);
		download_status_label->set_custom_minimum_size(Size2(0, 24) * EDSCALE);
		download_status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		download_status_label->set_visible(false);
		main_vbox->add_child(download_status_label);

		git_log_scroll = memnew(ScrollContainer);
		git_log_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		git_log_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		git_log_scroll->set_custom_minimum_size(Size2(0, 140) * EDSCALE);
		git_log_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
		git_log_scroll->set_visible(false);
		main_vbox->add_child(git_log_scroll);

		git_log_label = memnew(Label);
		git_log_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		git_log_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		git_log_label->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
		git_log_scroll->add_child(git_log_label);
	}

	// Buttons section.
	{
		HBoxContainer *btn_hbox = memnew(HBoxContainer);
		btn_hbox->add_theme_constant_override("separation", 8);
		main_vbox->add_child(btn_hbox);

		download_button = memnew(Button);
		download_button->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
		download_button->connect(SceneStringName(pressed), callable_mp(this, &AISourceManager::_on_download_button_pressed));
		btn_hbox->add_child(download_button);

		delete_button = memnew(Button);
		delete_button->set_text(TTR("Delete Cache"));
		delete_button->connect(SceneStringName(pressed), callable_mp(this, &AISourceManager::_on_delete_button_pressed));
		btn_hbox->add_child(delete_button);
	}

	git_install_prompt_dialog = memnew(ConfirmationDialog);
	git_install_prompt_dialog->set_title(TTR("Git Not Found"));
	git_install_prompt_dialog->set_ok_button_text(TTR("Install Git"));
	git_install_prompt_dialog->set_cancel_button_text(TTR("Use ZIP"));
	git_install_prompt_dialog->set_text(TTR("Git is not installed or is not available in PATH. Install Git for faster engine source updates?\n\nIf you choose ZIP, JunDot will download the full source archive as a fallback."));
	git_install_prompt_dialog->connect(SNAME("confirmed"), callable_mp(this, &AISourceManager::_on_git_install_confirmed));
	git_install_prompt_dialog->connect(SNAME("canceled"), callable_mp(this, &AISourceManager::_on_git_install_declined));
	add_child(git_install_prompt_dialog);

	// File dialog for cache directory selection.
	cache_dir_dialog = memnew(EditorFileDialog);
	cache_dir_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	cache_dir_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_DIR);
	cache_dir_dialog->set_title(TTR("Select Engine Source Cache Folder"));
	cache_dir_dialog->connect("dir_selected", callable_mp(this, &AISourceManager::_on_cache_dir_selected));
	add_child(cache_dir_dialog);
}
