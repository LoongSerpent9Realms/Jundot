/**************************************************************************/
/*                         ai_code_uploader.h                             */
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

#pragma once

#include "core/error/error_list.h"
#include "core/string/ustring.h"

// Git-based code uploader. Encapsulates `git add/commit/push` operations
// via OS::execute. The caller must provide a project root directory that
// contains a valid git repository.
class AICodeUploader {
public:
	// Upload (git add + commit + push) a single file.
	// p_file_path: path relative to project_root
	// p_commit_message: commit message
	// p_project_root: absolute path to the project root (git repo root)
	// r_error_message: if non-null, populated with error string on failure
	static Error upload(const String &p_file_path,
			const String &p_commit_message,
			const String &p_project_root,
			String *r_error_message = nullptr);
};
