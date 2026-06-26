/*  ai_source_update_service.h                                            */
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

struct AISourceUpdateStatus {
	enum State {
		NOT_CHECKED,
		NOT_AVAILABLE,
		CHECKING,
		UP_TO_DATE,
		UPDATE_AVAILABLE,
		UPDATING,
		UPDATED,
		ERROR,
	};

	State state = NOT_CHECKED;
	String source_root;
	String upstream;
	String message;
	int behind_count = 0;
	uint64_t checked_at_unix = 0;
};

class AISourceUpdateService {
public:
	static String resolve_source_root();
	static AISourceUpdateStatus get_cached_status();
	static AISourceUpdateStatus check_for_updates(bool p_fetch_remote = true);
	static Error update_source(AISourceUpdateStatus &r_status);
	static Error ensure_updated_before_edit(String &r_message);
};
