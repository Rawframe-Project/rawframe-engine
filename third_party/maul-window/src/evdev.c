// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Linux input event key codes as key codes.

#include "evdev.h"

#include <stddef.h>

// The key code of each evdev code from 0 (KEY_RESERVED) to 194
// (KEY_F24), in the order of linux/input-event-codes.h; 0 where the key
// has no code. KEY_COMPOSE is the context menu key. The ISO key beside
// Enter reaches evdev as KEY_BACKSLASH, so mwin_codeIntlHash never
// comes from here.
// clang-format off
static const uint8_t s_codes[] = {
      0,  41,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  45,  46,  42,  43,
     20,  26,   8,  21,  23,  28,  24,  12,  18,  19,  47,  48,  40, 224,   4,  22,
      7,   9,  10,  11,  13,  14,  15,  51,  52,  53, 225,  49,  29,  27,   6,  25,
      5,  17,  16,  54,  55,  56, 229,  85, 226,  44,  57,  58,  59,  60,  61,  62,
     63,  64,  65,  66,  67,  83,  71,  95,  96,  97,  86,  92,  93,  94,  87,  89,
     90,  91,  98,  99,   0,   0, 100,  68,  69, 135,   0,   0, 138, 136, 139,   0,
     88, 228,  84,  70, 230,   0,  74,  82,  75,  80,  79,  77,  81,  78,  73,  76,
      0,   0,   0,   0,   0, 103,   0,  72,   0, 133, 144, 145, 137, 227, 231, 101,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0, 104, 105, 106, 107, 108, 109, 110, 111, 112,
    113, 114, 115,
};
// clang-format on

mwinKeyCode mwinKeyCodeFromEvdev(uint32_t evdev)
{
    return evdev < sizeof(s_codes) ? s_codes[evdev] : mwin_codeUnknown;
}

uint32_t mwinEvdevFromKeyCode(mwinKeyCode code)
{
    for (size_t i = 1; i < sizeof(s_codes) && code != mwin_codeUnknown; i++)
    {
        if (s_codes[i] == code)
        {
            return (uint32_t)i;
        }
    }
    return 0;
}
