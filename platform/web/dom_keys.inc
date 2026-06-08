/**************************************************************************/
/*  dom_keys.inc                                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             JUNDOT ENGINE                               */
/*                        https://jundotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Jundot Engine contributors (see AUTHORS.md). */
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

#include "core/os/keyboard.h"

// See https://w3c.github.io/uievents-code/#code-value-tables
Key dom_code2jundot_scancode(EM_UTF8 const p_code[32], EM_UTF8 const p_key[32], bool p_physical) {
#define DOM2JUNDOT(p_str, p_jundot_code) \
	if (memcmp((const void *)p_str, (void *)(p_physical ? p_code : p_key), strlen(p_str) + 1) == 0) { \
		return Key::p_jundot_code; \
	}

	// Numpad section.
	DOM2JUNDOT("NumLock", NUMLOCK);
	DOM2JUNDOT("Numpad0", KP_0);
	DOM2JUNDOT("Numpad1", KP_1);
	DOM2JUNDOT("Numpad2", KP_2);
	DOM2JUNDOT("Numpad3", KP_3);
	DOM2JUNDOT("Numpad4", KP_4);
	DOM2JUNDOT("Numpad5", KP_5);
	DOM2JUNDOT("Numpad6", KP_6);
	DOM2JUNDOT("Numpad7", KP_7);
	DOM2JUNDOT("Numpad8", KP_8);
	DOM2JUNDOT("Numpad9", KP_9);
	DOM2JUNDOT("NumpadAdd", KP_ADD);
	DOM2JUNDOT("NumpadBackspace", BACKSPACE);
	DOM2JUNDOT("Clear", CLEAR); // NumLock on macOS.
	DOM2JUNDOT("NumpadClear", CLEAR);
	DOM2JUNDOT("NumpadClearEntry", CLEAR);
	//DOM2JUNDOT("NumpadComma", UNKNOWN);
	DOM2JUNDOT("NumpadDecimal", KP_PERIOD);
	DOM2JUNDOT("NumpadDivide", KP_DIVIDE);
	DOM2JUNDOT("NumpadEnter", KP_ENTER);
	DOM2JUNDOT("NumpadEqual", EQUAL);
	//DOM2JUNDOT("NumpadHash", UNKNOWN);
	//DOM2JUNDOT("NumpadMemoryAdd", UNKNOWN);
	//DOM2JUNDOT("NumpadMemoryClear", UNKNOWN);
	//DOM2JUNDOT("NumpadMemoryRecall", UNKNOWN);
	//DOM2JUNDOT("NumpadMemoryStore", UNKNOWN);
	//DOM2JUNDOT("NumpadMemorySubtract", UNKNOWN);
	DOM2JUNDOT("NumpadMultiply", KP_MULTIPLY);
	DOM2JUNDOT("NumpadParenLeft", PARENLEFT);
	DOM2JUNDOT("NumpadParenRight", PARENRIGHT);
	DOM2JUNDOT("NumpadStar", KP_MULTIPLY); // or ASTERISK ?
	DOM2JUNDOT("NumpadSubtract", KP_SUBTRACT);

	// Alphanumeric section.
	DOM2JUNDOT("Backquote", QUOTELEFT);
	DOM2JUNDOT("Backslash", BACKSLASH);
	DOM2JUNDOT("BracketLeft", BRACKETLEFT);
	DOM2JUNDOT("BracketRight", BRACKETRIGHT);
	DOM2JUNDOT("Comma", COMMA);
	DOM2JUNDOT("Digit0", KEY_0);
	DOM2JUNDOT("Digit1", KEY_1);
	DOM2JUNDOT("Digit2", KEY_2);
	DOM2JUNDOT("Digit3", KEY_3);
	DOM2JUNDOT("Digit4", KEY_4);
	DOM2JUNDOT("Digit5", KEY_5);
	DOM2JUNDOT("Digit6", KEY_6);
	DOM2JUNDOT("Digit7", KEY_7);
	DOM2JUNDOT("Digit8", KEY_8);
	DOM2JUNDOT("Digit9", KEY_9);
	DOM2JUNDOT("Equal", EQUAL);
	DOM2JUNDOT("IntlBackslash", BACKSLASH);
	//DOM2JUNDOT("IntlRo", UNKNOWN);
	DOM2JUNDOT("IntlYen", YEN);

	DOM2JUNDOT("KeyA", A);
	DOM2JUNDOT("KeyB", B);
	DOM2JUNDOT("KeyC", C);
	DOM2JUNDOT("KeyD", D);
	DOM2JUNDOT("KeyE", E);
	DOM2JUNDOT("KeyF", F);
	DOM2JUNDOT("KeyG", G);
	DOM2JUNDOT("KeyH", H);
	DOM2JUNDOT("KeyI", I);
	DOM2JUNDOT("KeyJ", J);
	DOM2JUNDOT("KeyK", K);
	DOM2JUNDOT("KeyL", L);
	DOM2JUNDOT("KeyM", M);
	DOM2JUNDOT("KeyN", N);
	DOM2JUNDOT("KeyO", O);
	DOM2JUNDOT("KeyP", P);
	DOM2JUNDOT("KeyQ", Q);
	DOM2JUNDOT("KeyR", R);
	DOM2JUNDOT("KeyS", S);
	DOM2JUNDOT("KeyT", T);
	DOM2JUNDOT("KeyU", U);
	DOM2JUNDOT("KeyV", V);
	DOM2JUNDOT("KeyW", W);
	DOM2JUNDOT("KeyX", X);
	DOM2JUNDOT("KeyY", Y);
	DOM2JUNDOT("KeyZ", Z);

	DOM2JUNDOT("Minus", MINUS);
	DOM2JUNDOT("Period", PERIOD);
	DOM2JUNDOT("Quote", APOSTROPHE);
	DOM2JUNDOT("Semicolon", SEMICOLON);
	DOM2JUNDOT("Slash", SLASH);

	// Functional keys in the Alphanumeric section.
	DOM2JUNDOT("Alt", ALT);
	DOM2JUNDOT("AltLeft", ALT);
	DOM2JUNDOT("AltRight", ALT);
	DOM2JUNDOT("Backspace", BACKSPACE);
	DOM2JUNDOT("CapsLock", CAPSLOCK);
	DOM2JUNDOT("ContextMenu", MENU);
	DOM2JUNDOT("Control", CTRL);
	DOM2JUNDOT("ControlLeft", CTRL);
	DOM2JUNDOT("ControlRight", CTRL);
	DOM2JUNDOT("Enter", ENTER);
	DOM2JUNDOT("Meta", META);
	DOM2JUNDOT("MetaLeft", META);
	DOM2JUNDOT("MetaRight", META);
	DOM2JUNDOT("OSLeft", META); // Command on macOS.
	DOM2JUNDOT("OSRight", META); // Command on macOS.
	DOM2JUNDOT("Shift", SHIFT);
	DOM2JUNDOT("ShiftLeft", SHIFT);
	DOM2JUNDOT("ShiftRight", SHIFT);
	DOM2JUNDOT("Space", SPACE);
	DOM2JUNDOT("Tab", TAB);

	// ControlPad section.
	DOM2JUNDOT("Delete", KEY_DELETE);
	DOM2JUNDOT("End", END);
	DOM2JUNDOT("Help", HELP);
	DOM2JUNDOT("Home", HOME);
	DOM2JUNDOT("Insert", INSERT);
	DOM2JUNDOT("PageDown", PAGEDOWN);
	DOM2JUNDOT("PageUp", PAGEUP);

	// ArrowPad section.
	DOM2JUNDOT("ArrowDown", DOWN);
	DOM2JUNDOT("ArrowLeft", LEFT);
	DOM2JUNDOT("ArrowRight", RIGHT);
	DOM2JUNDOT("ArrowUp", UP);

	// Function section.
	DOM2JUNDOT("Escape", ESCAPE);
	DOM2JUNDOT("F1", F1);
	DOM2JUNDOT("F2", F2);
	DOM2JUNDOT("F3", F3);
	DOM2JUNDOT("F4", F4);
	DOM2JUNDOT("F5", F5);
	DOM2JUNDOT("F6", F6);
	DOM2JUNDOT("F7", F7);
	DOM2JUNDOT("F8", F8);
	DOM2JUNDOT("F9", F9);
	DOM2JUNDOT("F10", F10);
	DOM2JUNDOT("F11", F11);
	DOM2JUNDOT("F12", F12);
	//DOM2JUNDOT("Fn", UNKNOWN); // never actually fired, but included in the standard draft.
	//DOM2JUNDOT("FnLock", UNKNOWN);
	DOM2JUNDOT("PrintScreen", PRINT);
	DOM2JUNDOT("ScrollLock", SCROLLLOCK);
	DOM2JUNDOT("Pause", PAUSE);

	// Media keys section.
	DOM2JUNDOT("BrowserBack", BACK);
	DOM2JUNDOT("BrowserFavorites", FAVORITES);
	DOM2JUNDOT("BrowserForward", FORWARD);
	DOM2JUNDOT("BrowserHome", OPENURL);
	DOM2JUNDOT("BrowserRefresh", REFRESH);
	DOM2JUNDOT("BrowserSearch", SEARCH);
	DOM2JUNDOT("BrowserStop", STOP);
	//DOM2JUNDOT("Eject", UNKNOWN);
	DOM2JUNDOT("LaunchApp1", LAUNCH0);
	DOM2JUNDOT("LaunchApp2", LAUNCH1);
	DOM2JUNDOT("LaunchMail", LAUNCHMAIL);
	DOM2JUNDOT("MediaPlayPause", MEDIAPLAY);
	DOM2JUNDOT("MediaSelect", LAUNCHMEDIA);
	DOM2JUNDOT("MediaStop", MEDIASTOP);
	DOM2JUNDOT("MediaTrackNext", MEDIANEXT);
	DOM2JUNDOT("MediaTrackPrevious", MEDIAPREVIOUS);
	//DOM2JUNDOT("Power", UNKNOWN);
	//DOM2JUNDOT("Sleep", UNKNOWN);
	DOM2JUNDOT("AudioVolumeDown", VOLUMEDOWN);
	DOM2JUNDOT("AudioVolumeMute", VOLUMEMUTE);
	DOM2JUNDOT("AudioVolumeUp", VOLUMEUP);
	//DOM2JUNDOT("WakeUp", UNKNOWN);

	// Printable ASCII.
	uint8_t b0 = (uint8_t)p_key[0];
	uint8_t b1 = (uint8_t)p_key[1];
	if (b0 >= 0x20 && b0 < 0x7F) { // ASCII.
		if (b0 > 0x60 && b0 < 0x7B) { // Lowercase ASCII.
			b0 -= 32;
		}
		return (Key)b0;
	} else if (b0 == 0xC2 && b1 == 0xA5) {
		return Key::YEN;
	} else if (b0 == 0xC2 && b1 == 0xA7) {
		return Key::SECTION;
	}

	return Key::NONE;
#undef DOM2JUNDOT
}

KeyLocation dom_code2jundot_key_location(EM_UTF8 const p_code[32]) {
#define DOM2JUNDOT(m_str, m_jundot_code) \
	if (memcmp((const void *)m_str, (void *)p_code, strlen(m_str) + 1) == 0) { \
		return KeyLocation::m_jundot_code; \
	}

	DOM2JUNDOT("AltLeft", LEFT);
	DOM2JUNDOT("AltRight", RIGHT);
	DOM2JUNDOT("ControlLeft", LEFT);
	DOM2JUNDOT("ControlRight", RIGHT);
	DOM2JUNDOT("MetaLeft", LEFT);
	DOM2JUNDOT("MetaRight", RIGHT);
	DOM2JUNDOT("OSLeft", LEFT);
	DOM2JUNDOT("OSRight", RIGHT);
	DOM2JUNDOT("ShiftLeft", LEFT);
	DOM2JUNDOT("ShiftRight", RIGHT);

	return KeyLocation::UNSPECIFIED;
#undef DOM2JUNDOT
}
