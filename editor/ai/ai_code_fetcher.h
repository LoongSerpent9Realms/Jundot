/*  ai_code_fetcher.h                                                      */
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

#pragma once

#include "core/error/error_list.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"

// Downloads engine source files from remote GitHub repositories.
// Operates as a static utility: fetches file(s) by URL and writes them
// to the local working directory with SHA256 verification.
//
// Remote URL resolution:
//   Default: inferred from `git remote get-url origin` → raw.githubusercontent.com
//   Override: caller can pass an explicit base URL or full per-file URLs.
class AICodeFetcher {
public:
	// Download a single file from a URL and write it to p_dest_path.
	// Returns ERR_FILE_NOT_FOUND if curl is unavailable.
	// Returns ERR_DOWNLOAD_FAILED on HTTP errors or network failure.
	// On success, r_sha256 is set to the hex digest of the downloaded content.
	static Error fetch_file(const String &p_url, const String &p_dest_path, String &r_sha256);

	// Download multiple files (parallel if possible, sequential for safety).
	// Returns OK only if ALL files succeed; partial failures are reported via r_errors.
	static Error fetch_files(const Vector<String> &p_urls, const Vector<String> &p_dest_paths, Vector<String> &r_errors);

	// Infer a raw.githubusercontent.com URL from the git remote origin.
	// Handles both HTTPS (https://github.com/user/repo.git) and SSH
	// (git@github.com:user/repo.git) formats.
	// p_branch defaults to "main" if the remote HEAD cannot be determined.
	static String infer_raw_url(const String &p_repo_path, const String &p_file_path, const String &p_branch = "main");

	// Compute the SHA256 hex digest of a file on disk.
	static String compute_sha256(const String &p_path);
};
