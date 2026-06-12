/*  ai_source_manager.cpp                                                  */
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
#include "modules/zip/zip_reader.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/scroll_container.h"

const char *JUNDOT_ENGINE_SOURCE_ZIP_URL = "https://github.com/LoongSerpent9Realms/Jundot/archive/refs/heads/master.zip";

void AISourceManager::_bind_methods() {
}

void AISourceManager::_cleanup_on_close() {
	if (is_downloading && downloader) {
		downloader->cancel();
		is_downloading = false;
		set_process(false);
	}

	if (is_processing && worker_thread) {
		is_processing = false;
		worker_thread->wait_to_finish();
		memdelete(worker_thread);
		worker_thread = nullptr;
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

AISourceManager::SourceStatus AISourceManager::_get_current_status() const {
	if (is_downloading) {
		return SourceStatus::DOWNLOADING;
	}
	if (is_processing) {
		return SourceStatus::EXTRACTING;
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

Error AISourceManager::_run_git_command(const String &p_working_dir, const List<String> &p_args, String &r_output) const {
	List<String> args;
	args.push_back("-C");
	args.push_back(p_working_dir);
	for (const List<String>::Element *E = p_args.front(); E; E = E->next()) {
		args.push_back(E->get());
	}

	int exit_code = 0;
	const Error err = OS::get_singleton()->execute("git", args, &r_output, &exit_code, true);
	if (err != OK) {
		return err;
	}
	return exit_code == 0 ? OK : FAILED;
}

Error AISourceManager::_update_git_cache(const String &p_cache_path, String &r_source_root, String &r_error) {
	callable_mp(this, &AISourceManager::_set_processing_progress).call_deferred(0, TTR("Checking Git availability..."));
	if (!_is_git_available()) {
		r_error = TTR("Git is not available.");
		return ERR_UNAVAILABLE;
	}

	callable_mp(this, &AISourceManager::_set_processing_progress).call_deferred(1, TTR("Preparing Git source cache directory..."));
	Error err = DirAccess::make_dir_recursive_absolute(p_cache_path);
	if (err != OK) {
		r_error = TTR("Could not create cache directory.");
		return err;
	}

	String repo_path = p_cache_path;
	if (!FileAccess::exists(repo_path.path_join(".git")) && _dir_has_entries(repo_path)) {
		repo_path = p_cache_path.path_join("Jundot");
		DirAccess::make_dir_recursive_absolute(repo_path);
	}

	String output;
	if (!FileAccess::exists(repo_path.path_join(".git"))) {
		callable_mp(this, &AISourceManager::_set_processing_progress).call_deferred(2, TTR("Cloning JunDot engine source with Git..."));
		List<String> clone_args;
		clone_args.push_back("clone");
		clone_args.push_back("--depth=1");
		clone_args.push_back(JUNDOT_ENGINE_SOURCE_REPOSITORY_URL);
		clone_args.push_back(repo_path);

		int exit_code = 0;
		err = OS::get_singleton()->execute("git", clone_args, &output, &exit_code, true);
		if (err != OK || exit_code != 0) {
			r_error = output.strip_edges();
			if (r_error.is_empty()) {
				r_error = TTR("Git clone failed.");
			}
			return err != OK ? err : FAILED;
		}
	} else {
		callable_mp(this, &AISourceManager::_set_processing_progress).call_deferred(2, TTR("Fetching latest JunDot engine source with Git..."));
		List<String> fetch_args;
		fetch_args.push_back("fetch");
		fetch_args.push_back("--depth=1");
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

		callable_mp(this, &AISourceManager::_set_processing_progress).call_deferred(3, TTR("Resetting Git source cache to the latest revision..."));
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
	callable_mp(this, &AISourceManager::_set_processing_progress).call_deferred(4, TTR("Git source cache update complete. Encrypting cache..."));
	return OK;
}

void AISourceManager::_update_ui() {
	SourceStatus status = _get_current_status();
	AISettingsData settings = AISettings::load();
	String cache_path = settings.engine_source_cache_root.strip_edges();
	if (cache_path.is_empty()) {
		cache_path = _get_default_cache_root();
	}

	cache_path_edit->set_text(cache_path);

	switch (status) {
		case SourceStatus::NOT_DOWNLOADED:
			status_label->set_text(TTR("Status: Not Downloaded"));
			download_button->set_text(TTR("Download / Update"));
			download_button->set_disabled(false);
			delete_button->set_disabled(true);
			browse_button->set_disabled(false);
			cache_path_edit->set_editable(true);
			download_progress->set_visible(false);
			download_status_label->set_visible(false);
			break;

		case SourceStatus::DOWNLOADING:
			status_label->set_text(TTR("Status: Downloading ZIP fallback..."));
			download_button->set_text(TTR("Cancel"));
			download_button->set_disabled(false);
			delete_button->set_disabled(true);
			browse_button->set_disabled(true);
			cache_path_edit->set_editable(false);
			download_progress->set_visible(true);
			download_status_label->set_visible(true);
			break;

		case SourceStatus::EXTRACTING:
		case SourceStatus::ENCRYPTING:
			status_label->set_text(TTR("Status: Processing source cache..."));
			download_button->set_text(TTR("Working..."));
			download_button->set_disabled(true);
			delete_button->set_disabled(true);
			browse_button->set_disabled(true);
			cache_path_edit->set_editable(false);
			download_progress->set_visible(true);
			download_status_label->set_visible(true);
			break;

		case SourceStatus::DOWNLOADED: {
			status_label->set_text(vformat(TTR("Status: Downloaded\nSource Root: %s"), _get_source_root()));
			download_button->set_text(TTR("Update"));
			download_button->set_disabled(false);
			delete_button->set_disabled(false);
			browse_button->set_disabled(false);
			cache_path_edit->set_editable(true);
			download_progress->set_visible(false);
			download_status_label->set_visible(false);
		} break;

		case SourceStatus::ERROR:
			status_label->set_text(vformat(TTR("Status: Error - %s"), current_error));
			download_button->set_text(TTR("Retry"));
			download_button->set_disabled(false);
			delete_button->set_disabled(false);
			browse_button->set_disabled(false);
			cache_path_edit->set_editable(true);
			download_progress->set_visible(false);
			download_status_label->set_visible(true);
			download_status_label->set_text(current_error);
			break;

		default:
			break;
	}
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
	}
}

void AISourceManager::_on_browse_button_pressed() {
	cache_dir_dialog->set_current_dir(cache_path_edit->get_text().strip_edges());
	cache_dir_dialog->popup_file_dialog();
}

void AISourceManager::_on_cache_dir_selected(const String &p_path) {
	cache_path_edit->set_text(p_path);
	AISettingsData settings = AISettings::load();
	settings.engine_source_cache_root = p_path;
	AISettings::save(settings);
	_update_ui();
}

void AISourceManager::_on_download_button_pressed() {
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

	Error err = DirAccess::make_dir_recursive_absolute(cache_path);
	if (err != OK) {
		current_error = TTR("Could not create cache directory.");
		_update_ui();
		return;
	}

	AISettingsData settings = AISettings::load();
	settings.engine_source_cache_root = cache_path;
	AISettings::save(settings);

	if (_is_git_available()) {
		_start_git_update(cache_path);
		return;
	}

	pending_zip_cache_path = cache_path;
	git_install_prompt_dialog->popup_centered(Size2(460, 140) * EDSCALE);
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
	_start_zip_fallback(pending_zip_cache_path);
}

void AISourceManager::_start_zip_fallback(const String &p_cache_path) {
	download_zip_path = p_cache_path.path_join("jundot_engine_source.zip");
	download_progress->set_min(0);
	download_progress->set_max(1);
	download_progress->set_value(0);
	download_progress->set_visible(true);
	download_status_label->set_text(TTR("Git was not found. Falling back to ZIP download..."));
	download_status_label->set_visible(true);

	const Error err = downloader->start(JUNDOT_ENGINE_SOURCE_ZIP_URL, download_zip_path, 6);
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
	String source_root = _get_source_root();
	if (source_root.is_empty()) {
		current_error = TTR("No source cache to delete.");
		_update_ui();
		return;
	}

	Error err = DirAccess::remove_absolute(source_root);
	if (err != OK) {
		current_error = vformat(TTR("Failed to delete cache: error %d"), err);
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

void AISourceManager::_start_git_update(const String &p_cache_path) {
	is_processing = true;
	worker_git_mode = true;
	worker_cache_path = p_cache_path;
	worker_error = OK;
	worker_error_msg = "";
	worker_source_root = "";

	status_label->set_text(TTR("Status: Updating with Git..."));
	download_progress->set_min(0);
	download_progress->set_max(5);
	download_progress->set_value(0);
	download_progress->set_visible(true);
	download_status_label->set_text(TTR("Preparing Git source cache update..."));
	download_status_label->set_visible(true);

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
	List<String> args;
	args.push_back("/E");
	args.push_back("/S:" + p_cache_path);

	String output;
	int exit_code = 0;
	Error err = OS::get_singleton()->execute("cipher", args, &output, &exit_code, true);
	if (err != OK || exit_code != 0) {
		r_error = output.strip_edges();
		if (r_error.is_empty()) {
			r_error = vformat("cipher exited with code %d.", exit_code);
		}
		return err != OK ? err : FAILED;
	}
	return OK;
#else
	r_error = "Automatic encrypted source cache is only implemented on Windows.";
	return ERR_UNAVAILABLE;
#endif
}

void AISourceManager::_start_post_download_processing(const String &p_cache_path) {
	is_processing = true;
	worker_git_mode = false;
	worker_cache_path = p_cache_path;
	worker_error = OK;
	worker_error_msg = "";
	worker_source_root = "";

	status_label->set_text(TTR("Status: Extracting ZIP fallback..."));
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
		err = _update_git_cache(worker_cache_path, source_root, git_error);
		if (err != OK || source_root.is_empty()) {
			worker_error = err != OK ? err : FAILED;
			worker_error_msg = git_error.is_empty() ? TTR("Git update failed.") : git_error;
			callable_mp(this, &AISourceManager::_on_processing_completed).call_deferred();
			return;
		}
	} else {
		err = _extract_zip(download_zip_path, worker_cache_path, source_root);
		if (err != OK || source_root.is_empty()) {
			worker_error = err != OK ? err : FAILED;
			worker_error_msg = TTR("ZIP extraction failed.");
			callable_mp(this, &AISourceManager::_on_processing_completed).call_deferred();
			return;
		}
		DirAccess::remove_absolute(download_zip_path);
	}

	String encryption_error;
	callable_mp(this, &AISourceManager::_set_processing_progress).call_deferred(worker_git_mode ? 4 : 2, TTR("Encrypting engine source cache..."));
	err = _encrypt_cache(worker_cache_path, encryption_error);
	if (err != OK) {
		worker_error = err;
		worker_error_msg = vformat(TTR("Source cache updated, but encryption failed: %s"), encryption_error);
		callable_mp(this, &AISourceManager::_on_processing_completed).call_deferred();
		return;
	}

	worker_source_root = source_root;
	callable_mp(this, &AISourceManager::_set_processing_progress).call_deferred(worker_git_mode ? 5 : 3, TTR("Engine source cache is ready."));
	callable_mp(this, &AISourceManager::_on_processing_completed).call_deferred();
}

void AISourceManager::_set_processing_progress(int p_step, const String &p_status) {
	if (!download_progress || !download_status_label) {
		return;
	}

	download_progress->set_visible(true);
	if (download_progress->get_max() < p_step) {
		download_progress->set_max(p_step);
	}
	download_progress->set_value(p_step);
	download_status_label->set_text(p_status);
	download_status_label->set_visible(true);
}

void AISourceManager::_on_processing_completed() {
	is_processing = false;

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
	settings.engine_source_repository_url = JUNDOT_ENGINE_SOURCE_REPOSITORY_URL;
	settings.encrypt_engine_source_cache = true;
	AISettings::save(settings);

	status_label->set_text(TTR("Status: Ready"));
	download_progress->set_visible(false);
	download_status_label->set_visible(false);
	_update_ui();
}

void AISourceManager::popup_centered_on_parent(const Window *p_parent) {
	popup_centered(Size2i(480, 240));
}

AISourceManager::AISourceManager() {
	set_title(TTR("AI Engine Source Manager"));
	set_ok_button_text(TTR("Close"));

	VBoxContainer *main_vbox = memnew(VBoxContainer);
	main_vbox->add_theme_constant_override("separation", 6);
	add_child(main_vbox);

	// Status section.
	{
		status_label = memnew(Label);
		status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		status_label->set_custom_minimum_size(Size2(0, 36));
		main_vbox->add_child(status_label);
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
		download_status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		download_status_label->set_visible(false);
		main_vbox->add_child(download_status_label);
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

	_update_ui();
}

