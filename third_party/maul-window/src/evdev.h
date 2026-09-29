// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The key codes of the Linux input subsystem (evdev), which Wayland
// sends as they are and X11 sends with 8 added, and the key codes of
// the API.

#ifndef MAUL_WINDOW_SRC_EVDEV_H
#define MAUL_WINDOW_SRC_EVDEV_H

#include "maul-window/input.h"

// The key code of an evdev code, mwin_codeUnknown for one without.
mwinKeyCode mwinKeyCodeFromEvdev(uint32_t evdev);

// The evdev code of a key code, 0 for one without.
uint32_t mwinEvdevFromKeyCode(mwinKeyCode code);

#endif // MAUL_WINDOW_SRC_EVDEV_H
