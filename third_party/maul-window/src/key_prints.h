// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Which keys print under a layout, for the backends that learn what a
// key types when it is pressed (iOS, Android): letters, digits, the
// space, punctuation and the keypad's printing keys, by their key codes
// (USB HID usages). The others are named by their codes.

#ifndef MAUL_WINDOW_SRC_KEY_PRINTS_H
#define MAUL_WINDOW_SRC_KEY_PRINTS_H

#include "maul-window/input.h"

static inline bool mwinKeyPrints(mwinKeyCode code)
{
    return (code >= 0x04 && code <= 0x27) || (code >= 0x2C && code <= 0x38) ||
           (code >= 0x54 && code <= 0x57) || (code >= 0x59 && code <= 0x64) || code == 0x67;
}

#endif // MAUL_WINDOW_SRC_KEY_PRINTS_H
