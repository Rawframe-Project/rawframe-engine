// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Web gamepads through the Gamepad API: navigator.getGamepads() read at
// each pump, a pad's controls posted when its timestamp moves. A pad
// with the standard mapping is mapped, any other raw. A disconnection the
// page reports (gamepaddisconnected) counts one more for its index, so
// a pad swapped for another between two pumps is seen as both. Rumble is
// the vibration actuator's dual-rumble effect, which stops itself; the
// browser gives no battery. Browsers show no pad before the page has
// had a press of one.

#ifndef MAUL_WINDOW_SRC_WEB_PAD_H
#define MAUL_WINDOW_SRC_WEB_PAD_H

#include "core.h"

// The most of navigator.getGamepads() read.
#define MWIN_WEB_PADS 8

typedef struct mwinWebPad
{
    bool connected;
    // The core's gamepad slot, the disconnections counted at its index
    // when it was found, and its last timestamp.
    uint32_t slot;
    uint32_t losses;
    double timestamp;
} mwinWebPad;

typedef struct mwinWebPads
{
    mwinContext* context;
    mwinWebPad pads[MWIN_WEB_PADS];
} mwinWebPads;

// Listens for connections; without the Gamepad API there are no pads.
void mwinWebPadsStart(mwinWebPads* pads, mwinContext* context);

// Reads the pads and finds new ones, at a time.
void mwinWebPadsPump(mwinWebPads* pads, uint64_t nowNs);

// The backend's rumble.
mwinResult mwinWebPadsRumble(const mwinWebPads* pads, uint32_t slot, float low, float high,
                             uint32_t durationMs);

#endif // MAUL_WINDOW_SRC_WEB_PAD_H
