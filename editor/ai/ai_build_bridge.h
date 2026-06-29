/*  ai_build_bridge.h                                                      */
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
#include "core/variant/dictionary.h"

// Lightweight bridge between AI panel and PackageBuilder.
// PackageBuilder is a separate C# GUI process; the bridge communicates
// via the filesystem (build config file / build history JSON).
class AIBuildBridge {
public:
	// Write a build request config that PackageBuilder can pick up.
	static Error write_build_request(const Dictionary &p_options = Dictionary());

	// Launch PackageBuilder.exe as a subprocess. Returns immediately.
	static Error launch_package_builder();

	// Check whether the latest build record exists and what its zip/manifest paths are.
	// Returns true if a build record was found, and fills the output params.
	static bool get_latest_build_info(String &r_version, String &r_zip_path, String &r_manifest_path, String &r_build_log_path);
	static bool get_latest_package_launch_info(String &r_version, String &r_package_dir, String &r_exe_path, String &r_zip_path, String &r_build_log_path);

	// Check whether the build request resulted in a successful build.
	static bool is_build_ready();

	// Read the status file written by an AI-triggered PackageBuilder run.
	static bool get_ai_build_status(String &r_state, String &r_message, String &r_zip_path, String &r_manifest_path, String &r_build_log_path);

	// Get the project root directory (where version.py lives).
	static String detect_repo_root();
};
