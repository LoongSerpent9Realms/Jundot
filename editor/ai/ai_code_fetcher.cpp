/*  ai_code_fetcher.cpp                                                    */
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

#include "ai_code_fetcher.h"

#include "core/crypto/crypto_core.h"
#include "core/error/error_macros.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/string/ustring.h"

// ---- Internal helpers ----

// Locate curl binary. Returns empty string if not found.
static String _find_curl() {
#ifdef WINDOWS_ENABLED
	List<String> args;
	args.push_back("curl.exe");
	String output;
	int exit_code = -1;
	OS::get_singleton()->execute("where", args, &output, nullptr, &exit_code);
	if (exit_code == 0 && !output.is_empty()) {
		Vector<String> lines = output.split("\n");
		if (lines.size() > 0) {
			return lines[0].strip_edges();
		}
	}
	return String();
#else
	return "curl";
#endif
}

// Download a file using curl subprocess.
static Error _curl_fetch(const String &p_url, const String &p_dest_path, String &r_output) {
	String curl = _find_curl();
	if (curl.is_empty()) {
		ERR_FAIL_V_MSG(ERR_UNAVAILABLE, "AICodeFetcher: curl not found on system PATH.");
	}

	// Remove target if it exists so curl -o doesn't refuse to overwrite.
	if (FileAccess::exists(p_dest_path)) {
		Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		if (da.is_valid()) {
			da->remove(p_dest_path);
		}
	}

	List<String> args;
	args.push_back("-sS");          // silent but show errors
	args.push_back("-L");           // follow redirects
	args.push_back("-A"); args.push_back("Mozilla/5.0 (JunDot AI game-reference research)");
	args.push_back("--connect-timeout"); args.push_back("15");
	args.push_back("--max-time"); args.push_back("60");
	args.push_back("-o"); args.push_back(p_dest_path);
	args.push_back(p_url);

	int exit_code = -1;
	String output;
	Error exec_err = OS::get_singleton()->execute(curl, args, &output, nullptr, &exit_code);

	r_output = output;

	if (exec_err != OK) {
		return ERR_CANT_OPEN;
	}

	if (exit_code != 0) {
		return ERR_CANT_OPEN;
	}

	if (!FileAccess::exists(p_dest_path)) {
		return ERR_FILE_NOT_FOUND;
	}

	return OK;
}

// ---- Public API ----

Error AICodeFetcher::fetch_file(const String &p_url, const String &p_dest_path, String &r_sha256) {
	String dest_dir = p_dest_path.get_base_dir();
	Ref<DirAccess> da = DirAccess::create_for_path(dest_dir);
	if (da.is_valid() && !da->dir_exists(dest_dir)) {
		Error mkdir_err = da->make_dir_recursive(dest_dir);
		ERR_FAIL_COND_V(mkdir_err != OK, mkdir_err);
	}

	String output;
	Error err = _curl_fetch(p_url, p_dest_path, output);
	ERR_FAIL_COND_V_MSG(err != OK, err, vformat("AICodeFetcher: Failed to fetch %s. curl output: %s", p_url, output));

	r_sha256 = compute_sha256(p_dest_path);

	return OK;
}

Error AICodeFetcher::fetch_files(const Vector<String> &p_urls, const Vector<String> &p_dest_paths, Vector<String> &r_errors) {
	ERR_FAIL_COND_V_MSG(p_urls.size() != p_dest_paths.size(), ERR_INVALID_PARAMETER, "URL and dest arrays must match in size.");

	bool all_ok = true;
	for (int i = 0; i < p_urls.size(); i++) {
		String sha256;
		Error err = fetch_file(p_urls[i], p_dest_paths[i], sha256);
		if (err != OK) {
			r_errors.push_back(vformat("URL: %s — %s", p_urls[i], error_names[err]));
			all_ok = false;
		}
	}

	return all_ok ? OK : ERR_CANT_OPEN;
}

String AICodeFetcher::infer_raw_url(const String &p_repo_path, const String &p_file_path, const String &p_branch) {
	List<String> args;
	args.push_back("remote");
	args.push_back("get-url");
	args.push_back("origin");

	String remote_url;
	int exit_code = -1;
	Error err = OS::get_singleton()->execute("git", args, &remote_url, nullptr, &exit_code);
	if (err != OK || exit_code != 0 || remote_url.is_empty()) {
		ERR_FAIL_V_MSG(String(), "AICodeFetcher: git remote get-url origin failed.");
	}

	remote_url = remote_url.strip_edges();

	// SSH format: git@github.com:user/repo.git
	if (remote_url.begins_with("git@")) {
		int colon = remote_url.find_char(':');
		if (colon < 0) {
			ERR_FAIL_V_MSG(String(), "AICodeFetcher: Unable to parse SSH remote URL.");
		}
		String repo_path = remote_url.substr(colon + 1);
		repo_path = repo_path.replace(".git", "");
		return "https://raw.githubusercontent.com/" + repo_path + "/" + p_branch + "/" + p_file_path;
	}

	// HTTPS format: https://github.com/user/repo.git
	if (remote_url.begins_with("https://") && remote_url.find("github.com") >= 0) {
		remote_url = remote_url.replace(".git", "");
		// Find repo path after github.com/
		int start = remote_url.find("github.com/");
		String repo_path = remote_url.substr(start + 11);
		return "https://raw.githubusercontent.com/" + repo_path + "/" + p_branch + "/" + p_file_path;
	}

	ERR_FAIL_V_MSG(String(), "AICodeFetcher: Unsupported remote URL format: " + remote_url);
}

String AICodeFetcher::compute_sha256(const String &p_path) {
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ);
	ERR_FAIL_COND_V_MSG(f.is_null(), String(), vformat("AICodeFetcher: Cannot open file for SHA256: %s", p_path));

	CryptoCore::SHA256Context ctx;
	ctx.start();

	const int BUFFER_SIZE = 8192;
	uint8_t buffer[BUFFER_SIZE];

	while (!f->eof_reached()) {
		int read = f->get_buffer(buffer, BUFFER_SIZE);
		if (read > 0) {
			ctx.update(buffer, read);
		}
	}

	unsigned char hash[32];
	ctx.finish(hash);

	// Convert raw bytes to hex string.
	String hex;
	for (int i = 0; i < 32; i++) {
		hex += String::num_uint64(hash[i], 16, false).lpad(2, "0");
	}
	return hex;
}
