/**************************************************************************/
/*  update_dialog.cpp                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             JunDot ENGINE                               */
/**************************************************************************/
/* Copyright (c) 2026-present JunDot Engine contributors . */
/**************************************************************************/

#include "update_dialog.h"

#include "core/object/callable_mp.h"
#include "editor/editor_string_names.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/progress_bar.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/separator.h"

UpdateDialog::UpdateDialog() {
	set_title(TTRC("Engine Update Available"));
	set_size(Size2(520, 420) * EDSCALE);
	set_ok_button_text(TTRC("Remind Later"));

	VBoxContainer *main_vbox = memnew(VBoxContainer);
	add_child(main_vbox);

	// ── Header icon + version info ───────────────────────────
	HBoxContainer *header_hbox = memnew(HBoxContainer);
	header_hbox->set_alignment(BoxContainer::ALIGNMENT_BEGIN);

	VBoxContainer *info_vbox = memnew(VBoxContainer);
	info_vbox->set_h_size_flags(Control::SIZE_EXPAND_FILL);

	_version_label = memnew(Label);
	_version_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_LEFT);
	_version_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	_version_label->add_theme_font_size_override(SceneStringName(font_size), int(16 * EDSCALE));
	info_vbox->add_child(_version_label);

	_size_label = memnew(Label);
	_size_label->set_theme_type_variation("HeaderSmall");
	info_vbox->add_child(_size_label);

	header_hbox->add_child(info_vbox);
	main_vbox->add_child(header_hbox);

	_status_label = memnew(Label);
	_status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	_status_label->hide();
	main_vbox->add_child(_status_label);

	_progress_bar = memnew(ProgressBar);
	_progress_bar->set_indeterminate(true);
	_progress_bar->set_show_percentage(false);
	_progress_bar->hide();
	main_vbox->add_child(_progress_bar);

	// ── Separator ────────────────────────────────────────────
	HSeparator *sep = memnew(HSeparator);
	main_vbox->add_child(sep);

	// ── Changelog ────────────────────────────────────────────
	_changelog = memnew(RichTextLabel);
	_changelog->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	_changelog->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	_changelog->set_custom_minimum_size(Size2(0, 120) * EDSCALE);
	_changelog->set_selection_enabled(true);
	_changelog->set_scroll_follow(true);
	main_vbox->add_child(_changelog);

	// ── Action buttons ───────────────────────────────────────
	HBoxContainer *button_hbox = memnew(HBoxContainer);
	button_hbox->set_alignment(BoxContainer::ALIGNMENT_END);

	_update_button = memnew(Button);
	_update_button->set_text(TTRC("Update Now"));
	_update_button->set_focus_mode(Control::FOCUS_ALL);
	_update_button->connect(SceneStringName(pressed), callable_mp(this, &UpdateDialog::_on_update_pressed));
	button_hbox->add_child(_update_button);

	_skip_button = memnew(Button);
	_skip_button->set_text(TTRC("Skip This Version"));
	_skip_button->connect(SceneStringName(pressed), callable_mp(this, &UpdateDialog::_on_skip_pressed));
	button_hbox->add_child(_skip_button);

	main_vbox->add_child(button_hbox);
}

void UpdateDialog::set_update_info(const UpdateManifest &p_manifest) {
	_status_label->hide();
	_progress_bar->hide();
	_update_button->set_disabled(false);
	_skip_button->set_disabled(false);
	_skip_button->show();
	get_ok_button()->set_disabled(false);
	get_ok_button()->set_text(TTRC("Remind Later"));
	_update_button->set_text(TTRC("Update Now"));
	set_title(TTRC("Engine Update Available"));

	// Version display: "Jundot v1.7.3-rc is available"
	String version_text = vformat(TTR("%s is available!"), p_manifest.get_version_string());
	if (!version_text.is_empty()) {
		version_text = "Jundot v" + version_text;
	}
	_version_label->set_text(version_text);

	// Size display
	if (p_manifest.package_size > 0) {
		_size_label->set_text(vformat(TTR("Download size: %s"), UpdateManifest::format_size(p_manifest.package_size)));
	} else {
		_size_label->set_text(TTR("Download size will be determined by the launcher."));
	}

	// Changelog
	String changelog_text = p_manifest.changelog;
	if (changelog_text.is_empty()) {
		changelog_text = TTR("No changelog provided for this version.");
	}
	_changelog->set_text(changelog_text);

	// Highlight "Update Now" button for mandatory updates
	if (p_manifest.mandatory) {
		_update_button->set_text(TTRC("Update Now (Required)"));
		_skip_button->hide();
	}

	// Adjust title
	if (p_manifest.mandatory) {
		set_title(TTRC("Mandatory Engine Update"));
	}
}

void UpdateDialog::set_update_started() {
	_status_label->set_text(TTR("Updater started. Live download, verification, and installation progress is shown in the updater window."));
	_status_label->show();
	_progress_bar->set_indeterminate(true);
	_progress_bar->show();
	_update_button->set_disabled(true);
	_skip_button->set_disabled(true);
	get_ok_button()->set_disabled(true);
	popup_centered();
}

void UpdateDialog::set_update_finished(bool p_success, const String &p_message) {
	_status_label->set_text(p_message);
	_status_label->show();
	_progress_bar->hide();
	_update_button->set_disabled(p_success);
	_skip_button->set_disabled(p_success);
	get_ok_button()->set_disabled(false);
	get_ok_button()->set_text(p_success ? TTR("Close") : TTR("Remind Later"));
	popup_centered();
}

// ═══════════════════════════════════════════════════════════════

void UpdateDialog::_bind_methods() {
	ADD_SIGNAL(MethodInfo("update_now_requested"));
	ADD_SIGNAL(MethodInfo("skip_version_requested"));
}

void UpdateDialog::_on_update_pressed() {
	emit_signal("update_now_requested");
}

void UpdateDialog::_on_skip_pressed() {
	emit_signal("skip_version_requested");
	hide();
}
