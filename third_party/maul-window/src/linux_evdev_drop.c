// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An evdev reader's steps after the kernel drops events.

#include "linux_evdev_drop.h"

#define LONG_BITS (sizeof(unsigned long) * CHAR_BIT)

mwinEvdevStep mwinEvdevDropStep(bool* dropping, uint16_t type, uint16_t code)
{
    if (type == EV_SYN && code == SYN_DROPPED)
    {
        *dropping = true;
        return mwin_evdevSkip;
    }
    if (!*dropping)
    {
        return mwin_evdevTake;
    }
    if (type == EV_SYN && code == SYN_REPORT)
    {
        *dropping = false;
        return mwin_evdevResync;
    }
    return mwin_evdevSkip;
}

bool mwinEvdevResyncButtons(const unsigned long keys[MWIN_EVDEV_KEY_LONGS], bool read,
                            const uint16_t* codes, uint8_t count, bool* buttons)
{
    if (!read)
    {
        return false;
    }
    for (uint8_t i = 0; i < count; i++)
    {
        unsigned code = codes[i];
        buttons[i] = code < KEY_CNT && ((keys[code / LONG_BITS] >> (code % LONG_BITS)) & 1u) != 0;
    }
    return true;
}
