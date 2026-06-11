/*  ai_new_build_notifier.h                                                 */
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

#include "scene/gui/dialogs.h"

class Timer;

// Polls for a new editor build and shows a countdown restart dialog.
//
// Lifecycle:
//  1. start_polling() — begins checking AIBuildBridge::is_build_ready()
//     every 2 seconds.
//  2. When a new build is detected, a modal dialog appears:
//     "New editor build is ready. Saving and restarting in N..."
//  3. User can cancel to postpone, or let the countdown expire.
//  4. On confirm/timeout: saves all scenes/layout, triggers
//     EditorNode::restart_editor().
class AINewBuildNotifier : public AcceptDialog {
	GDCLASS(AINewBuildNotifier, AcceptDialog);

	Timer *poll_timer = nullptr;
	Timer *countdown_timer = nullptr;
	int countdown_seconds = 3;
	String dialog_template_text;

	// Current task info for AI build restart tracking.
	String current_task_id;
	String current_task_summary;

	void _on_poll_tick();
	void _on_countdown_tick();
	void _on_confirmed();

public:
	static AINewBuildNotifier *get_singleton();

	static void start_polling();
	static void stop_polling();

	// Set the current AI task info for post-restart tracking.
	void set_current_task(const String &p_task_id, const String &p_task_summary);

protected:
	static AINewBuildNotifier *singleton;
};
