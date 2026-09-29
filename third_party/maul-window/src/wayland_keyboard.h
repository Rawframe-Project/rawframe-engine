// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland keyboard. The compositor sends the keymap and the
// modifier state; libxkbcommon turns keys into characters, through
// compose sequences (dead keys) from the locale's table. Keys go to the
// window with keyboard focus, each followed by the text it types.
// Held keys repeat at the compositor's rate, from the pump.

#ifndef MAUL_WINDOW_SRC_WAYLAND_KEYBOARD_H
#define MAUL_WINDOW_SRC_WAYLAND_KEYBOARD_H

#include "wayland.h"

// The seat has a keyboard now, or no longer has one.
void mwinWaylandAddKeyboard(mwinWaylandPlatform* platform);
void mwinWaylandRemoveKeyboard(mwinWaylandPlatform* platform);

// Posts the repeats of a held key that are due; the pump calls it.
void mwinWaylandRepeatKeys(mwinWaylandPlatform* platform);

// The window in a slot goes: it loses keyboard focus.
void mwinWaylandForgetKeyboardFocus(mwinWaylandPlatform* platform, uint32_t slot);

// The backend's mapKeyCode and keyboardLayout.
mwinKey mwinWaylandMapKeyCode(const mwinWaylandPlatform* platform, mwinKeyCode code);
mwinResult mwinWaylandKeyboardLayout(const mwinWaylandPlatform* platform, char* buffer,
                                     size_t capacity, size_t* lengthOut);

#endif // MAUL_WINDOW_SRC_WAYLAND_KEYBOARD_H
