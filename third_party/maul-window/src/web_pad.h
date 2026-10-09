// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Web gamepads through the Gamepad API: navigator.getGamepads() read at
// each pump, a pad's controls posted when its timestamp moves. A pad
// with the standard mapping is mapped, any other raw. A disconnection the
// page reports (gamepaddisconnected) counts one more for its index, so
// a pad swapped for another between two pumps is seen as both. Rumble is
// the vibration actuator's dual-rumble effect, which stops itself, or
// its trigger-rumble effect where the actuator has one, which then also
// runs the triggers' motors: a new effect cuts the one playing, so the
// four motors play as one effect, played again with a pair stilled when
// that pair's time runs out first. The browser gives no battery. Browsers show no pad before the
// page has had a press of one.

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
    // The motors' strengths, the heavy, light, left and right ones, and
    // when each pair stops, the grips' and the triggers' (0 while still).
    float motors[4];
    uint64_t endsNs[2];
} mwinWebPad;

typedef struct mwinWebPads
{
    mwinContext* context;
    mwinWebPad pads[MWIN_WEB_PADS];
} mwinWebPads;

// Listens for connections; without the Gamepad API there are no pads.
void mwinWebPadsStart(mwinWebPads* pads, mwinContext* context);

// Reads the pads, finds new ones and stills a pair of motors whose time
// ran out before the other's, at a time.
void mwinWebPadsPump(mwinWebPads* pads, uint64_t nowNs);

// The backend's rumble and trigger rumble.
mwinResult mwinWebPadsRumble(mwinWebPads* pads, uint32_t slot, float low, float high,
                             uint32_t durationMs, uint64_t nowNs);
mwinResult mwinWebPadsTriggerRumble(mwinWebPads* pads, uint32_t slot, float left, float right,
                                    uint32_t durationMs, uint64_t nowNs);

#endif // MAUL_WINDOW_SRC_WEB_PAD_H
