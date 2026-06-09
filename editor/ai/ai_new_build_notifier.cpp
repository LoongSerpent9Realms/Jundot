/*  ai_new_build_notifier.cpp                                               */
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

#include "ai_new_build_notifier.h"

#include "ai_build_bridge.h"
#include "ai_restart_helper.h"
#include "core/object/callable_mp.h"
#include "editor/editor_node.h"
#include "scene/main/timer.h"

AINewBuildNotifier *AINewBuildNotifier::singleton = nullptr;

void AINewBuildNotifier::_on_poll_tick() {
	if (!AIBuildBridge::is_build_ready()) {
		return;
	}

	stop_polling();

	countdown_seconds = 3;
	dialog_template_text = TTRC("New editor build is ready.\nSaving and restarting in %d seconds...");
	set_text(vformat(dialog_template_text, countdown_seconds));
	set_title(TTRC("Editor Update"));

	countdown_timer->start();
	popup_centered();
}

void AINewBuildNotifier::_on_countdown_tick() {
	countdown_seconds--;
	if (countdown_seconds <= 0) {
		countdown_timer->stop();
		_on_confirmed();
		return;
	}
	set_text(vformat(dialog_template_text, countdown_seconds));
}

void AINewBuildNotifier::_on_confirmed() {
	countdown_timer->stop();

	// Save work state for restoration after restart.
	AIRestartHelper::save_state();

	// Trigger editor save and restart.
	EditorNode::get_singleton()->restart_editor();
}

AINewBuildNotifier *AINewBuildNotifier::get_singleton() {
	return singleton;
}

void AINewBuildNotifier::start_polling() {
	if (!singleton) {
		singleton = memnew(AINewBuildNotifier);

		singleton->poll_timer = memnew(Timer);
		singleton->poll_timer->set_wait_time(2.0);
		singleton->poll_timer->set_one_shot(false);
		singleton->poll_timer->connect("timeout", callable_mp(singleton, &AINewBuildNotifier::_on_poll_tick));
		singleton->add_child(singleton->poll_timer, false, INTERNAL_MODE_BACK);

		singleton->countdown_timer = memnew(Timer);
		singleton->countdown_timer->set_wait_time(1.0);
		singleton->countdown_timer->set_one_shot(false);
		singleton->countdown_timer->connect("timeout", callable_mp(singleton, &AINewBuildNotifier::_on_countdown_tick));
		singleton->add_child(singleton->countdown_timer, false, INTERNAL_MODE_BACK);

		singleton->connect(SceneStringName(confirmed), callable_mp(singleton, &AINewBuildNotifier::_on_confirmed));

		EditorNode::get_singleton()->get_gui_base()->add_child(singleton);
	}
	singleton->poll_timer->start();
}

void AINewBuildNotifier::stop_polling() {
	if (singleton && singleton->poll_timer) {
		singleton->poll_timer->stop();
	}
}
