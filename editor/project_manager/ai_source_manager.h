/*  ai_source_manager.h                                                    */
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

#pragma once

#include "core/templates/safe_refcount.h"
#include "scene/gui/dialogs.h"
#include "core/os/thread.h"
#include "scene/gui/progress_bar.h"

class Button;
class EditorFileDialog;
class Label;
class LineEdit;
class MultiPartDownloader;
class ScrollContainer;
class VBoxContainer;

extern const char *JUNDOT_ENGINE_SOURCE_ZIP_URL;

class AISourceManager : public AcceptDialog {
	GDCLASS(AISourceManager, AcceptDialog);

public:
	enum class SourceStatus {
		NOT_DOWNLOADED,
		DOWNLOADING,
		DOWNLOADED,
		EXTRACTING,
		ENCRYPTING,
		ERROR
	};

private:
	Label *status_label = nullptr;
	LineEdit *cache_path_edit = nullptr;
	LineEdit *repository_url_edit = nullptr;
	Button *browse_button = nullptr;
	Button *download_button = nullptr;
	Button *delete_button = nullptr;
	ProgressBar *download_progress = nullptr;
	Label *download_status_label = nullptr;
	ScrollContainer *git_log_scroll = nullptr;
	Label *git_log_label = nullptr;
	VBoxContainer *main_vbox = nullptr;
	MultiPartDownloader *downloader = nullptr;
	EditorFileDialog *cache_dir_dialog = nullptr;
	ConfirmationDialog *git_install_prompt_dialog = nullptr;

	String download_zip_path;
	String pending_zip_cache_path;
	String pending_zip_url;
	bool is_downloading = false;
	bool is_processing = false;
	String current_error;

	Thread *worker_thread = nullptr;
	String worker_cache_path;
	String worker_repository_url;
	String worker_source_root;
	String worker_git_log;
	Error worker_error = OK;
	String worker_error_msg;
	bool worker_git_mode = false;
	SafeFlag shutdown_requested;

	void _update_ui();
	void _fit_to_contents();
	void _on_browse_button_pressed();
	void _on_reset_url_button_pressed();
	void _on_cache_dir_selected(const String &p_path);
	void _on_download_button_pressed();
	void _on_delete_button_pressed();
	void _on_download_progress(int64_t p_downloaded, int64_t p_total);
	void _on_download_finished(const String &p_output_path);
	void _on_download_failed(const String &p_reason);
	void _update_download_progress();
	void _start_git_update(const String &p_cache_path, const String &p_repository_url);
	void _start_zip_fallback(const String &p_cache_path, const String &p_repository_url);
	void _start_post_download_processing(const String &p_cache_path);
	void _on_git_install_confirmed();
	void _on_git_install_declined();
	static void _worker_thread_static_func(void *p_userdata);
	void _worker_thread_func();
	void _set_processing_progress(int p_step, const String &p_status);
	void _set_processing_progress_units(int p_current, int p_total, const String &p_status);
	void _set_processing_indeterminate(const String &p_status);
	void _append_git_log(const String &p_log);
	void _defer_processing_progress(int p_step, const String &p_status);
	void _defer_processing_progress_units(int p_current, int p_total, const String &p_status);
	void _defer_processing_indeterminate(const String &p_status);
	void _defer_git_log(const String &p_command, const String &p_output, Error p_error);
	void _defer_processing_completed();
	void _on_processing_completed();
	Error _extract_zip(const String &p_zip_path, const String &p_cache_path, String &r_source_root);
	Error _encrypt_cache(const String &p_cache_path, String &r_error);
	bool _is_git_available() const;
	Error _run_git_process(const List<String> &p_args, const String &p_command, String &r_output, int *r_exit_code = nullptr);
	Error _run_git_command(const String &p_working_dir, const List<String> &p_args, String &r_output, int *r_exit_code = nullptr);
	Error _update_git_cache(const String &p_cache_path, const String &p_repository_url, String &r_source_root, String &r_error);
	String _get_default_cache_root() const;
	SourceStatus _get_current_status() const;
	String _get_source_root() const;

protected:
	void _notification(int p_what);
	static void _bind_methods();
	void _cleanup_on_close();

public:
	void popup_centered_on_parent(const Window *p_parent);

	AISourceManager();
};
