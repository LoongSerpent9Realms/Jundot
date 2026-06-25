/*  ai_usage_agreement_dialog.h                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#pragma once

#include "scene/gui/dialogs.h"

class CheckBox;
class Label;

class AIUsageAgreementDialog : public ConfirmationDialog {
	GDCLASS(AIUsageAgreementDialog, ConfirmationDialog)

	CheckBox *confirm_check = nullptr;
	Label *agreement_label = nullptr;

	void _confirmed();
	void _canceled();
	void _confirm_toggled(bool p_pressed);
	void _reset_confirmation_state();
	void _update_translations();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	static String get_agreement_text();

	AIUsageAgreementDialog();
};
