// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// X11 keyboard and pointer input. The keyboard is the core keyboard
// through XKB: xkbcommon-x11 reads its keymap and XKB's state events
// keep modifiers and layout group current; with detectable autorepeat
// the X server's repeats come as presses of a key already held. Text
// comes from the keymap, through compose sequences, unless an input
// method over D-Bus takes the key (linux_ime.h). The pointer is the core pointer:
// buttons 4 to 7 turn the wheel, a detent each, and quick clicks are
// counted by the backend (clicks.h). XInput 2's raw motion, before the
// X server's acceleration, arrives as raw deltas while a focused window
// holds the cursor captured.

#ifndef MAUL_WINDOW_SRC_X11_INPUT_H
#define MAUL_WINDOW_SRC_X11_INPUT_H

#include "x11.h"

// Reads the keyboard and follows its changes, where libxkbcommon-x11
// loads; and lets it go.
void mwinX11StartKeyboard(mwinX11Platform* platform);
void mwinX11StopKeyboard(mwinX11Platform* platform);

// Asks for XInput 2's raw motion, where the X server has XInput 2; and
// from XI 2.1, reads the devices' scroll valuators and follows their
// changes.
void mwinX11StartRawMotion(mwinX11Platform* platform);

// Has a window's pointer events come through XI2, from XI 2.1.
void mwinX11SelectPointer(const mwinX11Platform* platform, xcb_window_t window);

// Handles a keyboard, pointer, XKB or XInput event: false for another
// kind.
bool mwinX11HandleInputEvent(mwinX11Platform* platform, const xcb_generic_event_t* event);

// A window lost focus: the keys held are forgotten.
void mwinX11ForgetKeys(mwinX11Platform* platform);

// Tells the input method the window with the keyboard's focus that takes
// text, and its caret, after either changes.
void mwinX11FollowIme(mwinX11Platform* platform);

// The window in a slot goes.
void mwinX11ForgetPointer(mwinX11Platform* platform, uint32_t slot);

// The backend's mapKeyCode and keyboardLayout.
mwinKey mwinX11MapKeyCode(const mwinX11Platform* platform, mwinKeyCode code);
mwinResult mwinX11KeyboardLayout(const mwinX11Platform* platform, char* buffer, size_t capacity,
                                 size_t* lengthOut);

#endif // MAUL_WINDOW_SRC_X11_INPUT_H
