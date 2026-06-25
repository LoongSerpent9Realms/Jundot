/**************************************************************************/
/*  update_dialog.h                                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             JUNDOT ENGINE                               */
/*                        https://jundotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
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

#include "editor/update/update_manifest.h"
#include "scene/gui/dialogs.h"

class Label;
class ProgressBar;
class RichTextLabel;

/// Dialog shown when a Jundot engine update is available.
/// Displays version information, changelog, file size, and
/// offers actions: update now, remind later, or skip this version.
class UpdateDialog : public AcceptDialog {
	GDCLASS(UpdateDialog, AcceptDialog);

public:
	UpdateDialog();

	/// Populate the dialog with manifest data.
	void set_update_info(const UpdateManifest &p_manifest);
	void set_update_started();
	void set_update_finished(bool p_success, const String &p_message);

	/// Signal emitted when the user clicks "Update Now".
	/// @signal update_now_requested

	/// Signal emitted when the user clicks "Skip This Version".
	/// @signal skip_version_requested

protected:
	static void _bind_methods();

private:
	Label *_version_label = nullptr;
	Label *_size_label = nullptr;
	Label *_status_label = nullptr;
	ProgressBar *_progress_bar = nullptr;
	RichTextLabel *_changelog = nullptr;
	Button *_update_button = nullptr;
	Button *_skip_button = nullptr;

	void _on_update_pressed();
	void _on_skip_pressed();
};
