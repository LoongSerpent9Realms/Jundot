/*  ai_first_run_guide_dialog.h                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#pragma once

#include "scene/gui/dialogs.h"

class Label;
class TextureRect;

class AIFirstRunGuideDialog : public ConfirmationDialog {
	GDCLASS(AIFirstRunGuideDialog, ConfirmationDialog)

	TextureRect *hero_icon = nullptr;
	Label *title_label = nullptr;
	Label *body_label = nullptr;
	Button *config_button = nullptr;

	void _open_config_pressed();
	void _update_translations();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	AIFirstRunGuideDialog();
};
