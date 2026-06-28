/*  ai_develop_flow.h                                                    */
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

#include "core/string/ustring.h"

class AIDevelopFlow {
public:
	enum Stage {
		IDLE,
		MODIFIED,
		BUILDING,
		BUILT,
		RESTARTING,
		USER_VERIFICATION,
		AI_VERIFICATION,
		READY_TO_UPLOAD,
		COMPLETE,
		FAILED,
	};

	static void reset();
	static void record_modified(const String &p_file);
	static void record_build_started();
	static void record_build_result(bool p_success, const String &p_detail = String());
	static void record_restart_requested();
	static void resume_after_restart();
	static void record_user_verification(bool p_passed, const String &p_detail = String());
	static void record_ai_verification(bool p_passed, const String &p_detail = String());
	static void record_simulated_upload(const String &p_file);
	static Stage get_stage();
	static String get_status_text();
	static bool is_waiting_for_user();
};
