/**************************************************************************/
/*  update_dialog.cpp                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             JunDot ENGINE                               */
/**************************************************************************/
/* Copyright (c) 2026-present JunDot Engine contributors . */
/**************************************************************************/

#pragma once

#include "editor/update/update_manifest.h"
#include "scene/gui/dialogs.h"

class Label;
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

	/// Signal emitted when the user clicks "Update Now".
	/// @signal update_now_requested

	/// Signal emitted when the user clicks "Skip This Version".
	/// @signal skip_version_requested

protected:
	static void _bind_methods();

private:
	Label *_version_label = nullptr;
	Label *_size_label = nullptr;
	RichTextLabel *_changelog = nullptr;
	Button *_update_button = nullptr;
	Button *_skip_button = nullptr;

	void _on_update_pressed();
	void _on_skip_pressed();
};
