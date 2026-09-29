// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Quick clicks, counted by the backends: a press of the same button
// soon after the last and near it counts one more. Platforms with no
// double-click setting (Wayland, X11) use 500 ms and 4 logical units;
// Windows has its own.

#ifndef MAUL_WINDOW_SRC_CLICKS_H
#define MAUL_WINDOW_SRC_CLICKS_H

#include "maul-window/input.h"
#include "maul-window/window.h"

#include <math.h>

#define MWIN_DOUBLE_CLICK_NS       500000000u
#define MWIN_DOUBLE_CLICK_DISTANCE 4.0f

// The last press: its button, time and place, and the clicks it made.
typedef struct mwinClickCounter
{
    mwinMouseButton button;
    uint64_t timeNs;
    mwinPosition position;
    uint8_t clicks;
} mwinClickCounter;

// Counts a press within a time and a distance of the last: the clicks
// it completes. Times from a display server's millisecond stamps can
// come a little out of order, so the time apart counts either way.
static inline uint8_t mwinCountClickWithin(mwinClickCounter* counter, mwinMouseButton button,
                                           mwinPosition position, uint64_t timeNs,
                                           uint64_t intervalNs, float distance)
{
    uint64_t apart =
        timeNs >= counter->timeNs ? timeNs - counter->timeNs : counter->timeNs - timeNs;
    bool quick = button == counter->button && apart <= intervalNs &&
                 fabsf(position.x - counter->position.x) <= distance &&
                 fabsf(position.y - counter->position.y) <= distance;
    counter->clicks = quick && counter->clicks < UINT8_MAX ? (uint8_t)(counter->clicks + 1) : 1;
    counter->button = button;
    counter->timeNs = timeNs;
    counter->position = position;
    return counter->clicks;
}

// Counts a press by the defaults.
static inline uint8_t mwinCountClick(mwinClickCounter* counter, mwinMouseButton button,
                                     mwinPosition position, uint64_t timeNs)
{
    return mwinCountClickWithin(counter, button, position, timeNs, MWIN_DOUBLE_CLICK_NS,
                                MWIN_DOUBLE_CLICK_DISTANCE);
}

#endif // MAUL_WINDOW_SRC_CLICKS_H
