/**************************************************************************/
/*                         ai_code_uploader.cpp                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                               JunDot                                   */
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
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE        */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "ai_code_uploader.h"

#include "editor/ai/ai_settings.h"

#include "core/crypto/crypto_core.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

static void _add_git_auth_headers(List<String> &r_args) {
	AISettingsData settings = AISettings::load();
	bool has_token = false;

	if (!settings.github_token.access_token.is_empty()) {
		const String basic = "x-access-token:" + settings.github_token.access_token;
		const CharString basic_utf8 = basic.utf8();
		r_args.push_back("-c");
		r_args.push_back("http.https://github.com/.extraHeader=Authorization: Basic " + CryptoCore::b64_encode_str((const uint8_t *)basic_utf8.get_data(), basic_utf8.length()));
		has_token = true;
	}

	if (!settings.gitee_token.access_token.is_empty()) {
		r_args.push_back("-c");
		r_args.push_back(vformat("http.https://gitee.com/.extraHeader=Authorization: Bearer %s", settings.gitee_token.access_token));
		has_token = true;
	}

	if (has_token) {
		r_args.push_back("-c");
		r_args.push_back("credential.helper=");
	}
}

static Error _run_git_cmd(const String &p_cwd,
		const Vector<String> &p_args,
		String *r_stdout = nullptr,
		int *r_exit_code = nullptr) {
	List<String> arg_list;
	arg_list.push_back("-c");
	arg_list.push_back("http.sslBackend=openssl");
	_add_git_auth_headers(arg_list);
	arg_list.push_back("-C");
	arg_list.push_back(p_cwd);
	for (int i = 0; i < p_args.size(); i++) {
		arg_list.push_back(p_args[i]);
	}

	String output;
	int exit_code = 0;
	Error err = OS::get_singleton()->execute("git", arg_list, &output, &exit_code, false);

	if (r_stdout) {
		*r_stdout = output;
	}
	if (r_exit_code) {
		*r_exit_code = exit_code;
	}

	return err;
}

Error AICodeUploader::upload(const String &p_file_path,
		const String &p_commit_message,
		const String &p_project_root,
		String *r_error_message) {

	if (AISettings::load().develop_mode) {
		if (r_error_message) *r_error_message = "Develop Mode blocks git add, commit, and push.";
		return ERR_UNAUTHORIZED;
	}

	if (p_file_path.is_empty()) {
		if (r_error_message) *r_error_message = "File path is empty.";
		return ERR_INVALID_PARAMETER;
	}

	if (p_commit_message.is_empty()) {
		if (r_error_message) *r_error_message = "Commit message is empty.";
		return ERR_INVALID_PARAMETER;
	}

	// Verify project_root exists.
	if (!DirAccess::exists(p_project_root)) {
		if (r_error_message) *r_error_message = "Project directory not found: " + p_project_root;
		return ERR_FILE_NOT_FOUND;
	}

	// Verify .git directory exists inside project root.
	String git_dir = p_project_root.path_join(".git");
	if (!DirAccess::exists(git_dir)) {
		if (r_error_message) *r_error_message = "Not a git repository: " + p_project_root;
		return ERR_FILE_NOT_FOUND;
	}

	// Verify target file exists.
	String full_path = p_project_root.path_join(p_file_path);
	if (!FileAccess::exists(full_path)) {
		if (r_error_message) *r_error_message = "File not found: " + full_path;
		return ERR_FILE_NOT_FOUND;
	}

	// Step 1: Determine current branch.
	String current_branch;
	int branch_exit = -1;
	Error err = _run_git_cmd(p_project_root, { "rev-parse", "--abbrev-ref", "HEAD" }, &current_branch, &branch_exit);
	if (err != OK || branch_exit != 0) {
		if (r_error_message) *r_error_message = "Failed to determine git branch. Output: " + current_branch;
		return ERR_CANT_FORK;
	}
	current_branch = current_branch.strip_edges();

	// Step 2: git add <file>
	String add_output;
	int add_exit = -1;
	err = _run_git_cmd(p_project_root, { "add", p_file_path }, &add_output, &add_exit);
	if (err != OK || add_exit != 0) {
		if (r_error_message) *r_error_message = "git add failed: " + add_output;
		return ERR_FILE_CANT_WRITE;
	}

	// Step 3: git commit -m <message>
	String commit_output;
	int commit_exit = -1;
	err = _run_git_cmd(p_project_root, { "commit", "-m", p_commit_message, "--", p_file_path }, &commit_output, &commit_exit);
	if (err != OK || commit_exit != 0) {
		if (r_error_message) *r_error_message = "git commit failed: " + commit_output;
		return ERR_FILE_CANT_WRITE;
	}

	// Step 4: git push origin <branch>
	String push_output;
	int push_exit = -1;
	err = _run_git_cmd(p_project_root, { "push", "origin", current_branch }, &push_output, &push_exit);
	if (err != OK || push_exit != 0) {
		if (r_error_message) *r_error_message = "git push failed: " + push_output;
		return ERR_FILE_CANT_WRITE;
	}

	return OK;
}
