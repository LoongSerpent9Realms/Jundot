/*  ai_usage_agreement_dialog.cpp                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#include "ai_usage_agreement_dialog.h"

#include "ai_settings.h"

#include "core/object/callable_mp.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/check_box.h"
#include "scene/gui/label.h"
#include "scene/gui/scroll_container.h"

static constexpr int AI_USAGE_AGREEMENT_DIALOG_WIDTH = 420;
static constexpr int AI_USAGE_AGREEMENT_TEXT_HEIGHT = 120;

void AIUsageAgreementDialog::_bind_methods() {
	ADD_SIGNAL(MethodInfo("agreement_accepted"));
	ADD_SIGNAL(MethodInfo("agreement_rejected"));
}

void AIUsageAgreementDialog::_notification(int p_what) {
	if (p_what == NOTIFICATION_TRANSLATION_CHANGED) {
		_update_translations();
	} else if (p_what == NOTIFICATION_VISIBILITY_CHANGED && is_visible()) {
		_reset_confirmation_state();
	}
}

String AIUsageAgreementDialog::get_agreement_text() {
	return TTR("Using AI features sends your prompts, selected context, attachments, logs, test output, and generated analysis to the configured AI API provider. These requests consume API tokens. Adding project memories, tool context, files, logs, or automatic repair analysis can increase token usage, and multi-step defect analysis may trigger more than one request. You can cancel this agreement, disable automatic suggestions, or reset this consent from the AI Config panel. Actual billing is controlled by your API provider.");
}

void AIUsageAgreementDialog::_confirmed() {
	if (!confirm_check->is_pressed()) {
		return;
	}

	const Error err = AISettings::accept_usage_agreement();
	if (err == OK) {
		emit_signal(SNAME("agreement_accepted"));
	} else {
		emit_signal(SNAME("agreement_rejected"));
	}
}

void AIUsageAgreementDialog::_canceled() {
	emit_signal(SNAME("agreement_rejected"));
}

void AIUsageAgreementDialog::_confirm_toggled(bool p_pressed) {
	get_ok_button()->set_disabled(!p_pressed);
}

void AIUsageAgreementDialog::_reset_confirmation_state() {
	if (confirm_check) {
		confirm_check->set_pressed(false);
	}
	if (get_ok_button()) {
		get_ok_button()->set_disabled(true);
	}
}

void AIUsageAgreementDialog::_update_translations() {
	set_title(TTR("AI Usage Agreement"));
	set_ok_button_text(TTR("I Agree"));
	get_cancel_button()->set_text(TTR("Cancel"));
	agreement_label->set_text(get_agreement_text());
	confirm_check->set_text(TTR("I understand that AI requests can consume additional API tokens."));
}

AIUsageAgreementDialog::AIUsageAgreementDialog() {
	set_exclusive(true);
	set_min_size(Size2(AI_USAGE_AGREEMENT_DIALOG_WIDTH, 0) * EDSCALE);

	VBoxContainer *root = memnew(VBoxContainer);
	root->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	root->add_theme_constant_override("separation", 8 * EDSCALE);
	add_child(root);

	ScrollContainer *agreement_scroll = memnew(ScrollContainer);
	agreement_scroll->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	agreement_scroll->set_custom_minimum_size(Size2(AI_USAGE_AGREEMENT_DIALOG_WIDTH, AI_USAGE_AGREEMENT_TEXT_HEIGHT) * EDSCALE);
	root->add_child(agreement_scroll);

	agreement_label = memnew(Label);
	agreement_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	agreement_label->set_custom_minimum_size(Size2(AI_USAGE_AGREEMENT_DIALOG_WIDTH, 0) * EDSCALE);
	agreement_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	agreement_scroll->add_child(agreement_label);

	confirm_check = memnew(CheckBox);
	confirm_check->connect(SceneStringName(toggled), callable_mp(this, &AIUsageAgreementDialog::_confirm_toggled));
	root->add_child(confirm_check);

	connect(SceneStringName(confirmed), callable_mp(this, &AIUsageAgreementDialog::_confirmed));
	connect(SNAME("canceled"), callable_mp(this, &AIUsageAgreementDialog::_canceled));

	_update_translations();
	_reset_confirmation_state();
}
