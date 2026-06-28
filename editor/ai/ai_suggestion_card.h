/*  ai_suggestion_card.h                                                   */
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

#include "editor/ai/ai_chat_parser.h"

#include "scene/gui/panel_container.h"

class Button;
class HBoxContainer;
class Label;
class TextEdit;
class VBoxContainer;

class AISuggestionCard : public PanelContainer {
	GDCLASS(AISuggestionCard, PanelContainer)

	AISuggestion suggestion;
	bool expanded = false;

	HBoxContainer *header = nullptr;
	Label *type_icon = nullptr;
	Label *name_label = nullptr;
	Label *type_label = nullptr;
	Button *expand_button = nullptr;
	Button *accept_button = nullptr;
	Button *reject_button = nullptr;
	VBoxContainer *detail_container = nullptr;
	Label *detail_label = nullptr;

	void _toggle_expand();
	void _accept_pressed();
	void _reject_pressed();
	void _build_ui();
	void _update_translations();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void setup(const AISuggestion &p_suggestion);
	AISuggestion get_suggestion() const;

	AISuggestionCard();
};
