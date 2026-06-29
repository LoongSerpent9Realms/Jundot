/*  ai_first_run_guide_dialog.cpp                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                                JunDot                                  */
/**************************************************************************/

#include "ai_first_run_guide_dialog.h"

#include "core/object/callable_mp.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/label.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/texture_rect.h"
#include "scene/resources/style_box_flat.h"

void AIFirstRunGuideDialog::_bind_methods() {
	ADD_SIGNAL(MethodInfo("open_ai_config_requested"));
}

void AIFirstRunGuideDialog::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE || p_what == NOTIFICATION_THEME_CHANGED) {
		if (hero_icon && EditorNode::get_singleton() && EditorNode::get_singleton()->get_editor_theme().is_valid()) {
			hero_icon->set_texture(EditorNode::get_singleton()->get_editor_theme()->get_icon(SNAME("TitleBarLogo"), EditorStringName(EditorIcons)));
		}
	} else if (p_what == NOTIFICATION_TRANSLATION_CHANGED) {
		_update_translations();
	}
}

void AIFirstRunGuideDialog::_open_config_pressed() {
	emit_signal(SNAME("open_ai_config_requested"));
	hide();
}

void AIFirstRunGuideDialog::_update_translations() {
	set_title(TTR("Set up Jundot AI"));
	set_ok_button_text(TTR("Start Using AI"));
	if (config_button) {
		config_button->set_text(TTR("Open AI Config"));
	}
	if (title_label) {
		title_label->set_text(TTR("Welcome to Jundot AI"));
	}
	if (body_label) {
		body_label->set_text(TTR("Let's finish the first-time AI setup:\n\n1. Choose or confirm your AI provider in Configuration.\n2. Add the API key or local endpoint you want Jundot AI to use.\n3. Send your first request in Chat. Beginner mode can stay chat-only while Jundot AI opens other editor panels only when needed."));
	}
}

AIFirstRunGuideDialog::AIFirstRunGuideDialog() {
	set_exclusive(true);
	set_min_size(Size2(560, 0) * EDSCALE);

	VBoxContainer *root = memnew(VBoxContainer);
	root->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	root->add_theme_constant_override("separation", 10 * EDSCALE);
	add_child(root);

	PanelContainer *hero_panel = memnew(PanelContainer);
	hero_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	root->add_child(hero_panel);

	Ref<StyleBoxFlat> hero_style;
	hero_style.instantiate();
	hero_style->set_bg_color(Color(0.12f, 0.14f, 0.16f));
	hero_style->set_border_color(Color(0.35f, 0.49f, 0.72f));
	hero_style->set_border_width_all(1 * EDSCALE);
	hero_style->set_corner_radius_all(8 * EDSCALE);
	hero_panel->add_theme_style_override("panel", hero_style);

	VBoxContainer *hero_box = memnew(VBoxContainer);
	hero_box->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	hero_box->set_alignment(BoxContainer::ALIGNMENT_CENTER);
	hero_box->add_theme_constant_override("separation", 6 * EDSCALE);
	hero_panel->add_child(hero_box);

	hero_icon = memnew(TextureRect);
	hero_icon->set_custom_minimum_size(Size2(72, 72) * EDSCALE);
	hero_icon->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	hero_icon->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
	hero_box->add_child(hero_icon);

	title_label = memnew(Label);
	title_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	title_label->add_theme_font_size_override("font_size", 20 * EDSCALE);
	hero_box->add_child(title_label);

	body_label = memnew(Label);
	body_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	body_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	body_label->add_theme_font_size_override("font_size", 14 * EDSCALE);
	root->add_child(body_label);

	config_button = add_button(TTR("Open AI Config"), false);
	config_button->connect(SceneStringName(pressed), callable_mp(this, &AIFirstRunGuideDialog::_open_config_pressed));

	_update_translations();
}
