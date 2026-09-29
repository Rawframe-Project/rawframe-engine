// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32's Xbox gamepads as Windows.Gaming.Input gives them (win32_wgi.h):
// looked for when the runtime says one came or went, and every half
// second in case it does not say; read at each pump, their controls
// posted only when the reading's time moves; their batteries read when
// they are looked for. The runtime keeps a motor running until told
// otherwise, so a rumble is stopped at the pump after its time runs
// out.

#ifndef MAUL_WINDOW_SRC_WIN32_XBOX_H
#define MAUL_WINDOW_SRC_WIN32_XBOX_H

#include "core.h"
#include "win32_wgi.h"

#define MWIN_WIN32_XBOX_PADS 16

typedef struct mwinWin32XboxPad
{
    // The runtime's reference, and the core's gamepad slot.
    void* pad;
    uint32_t slot;
    // The last reading's time, and when the motors stop (0 while they
    // are still).
    uint64_t timestamp;
    uint64_t rumbleEndsNs;
} mwinWin32XboxPad;

typedef struct mwinWin32Xbox
{
    mwinContext* context;
    mwinWgiApi api;
    mwinWin32XboxPad pads[MWIN_WIN32_XBOX_PADS];
    uint32_t count;
    // When the pads were last looked for.
    uint64_t searchedNs;
} mwinWin32Xbox;

void mwinWin32XboxStart(mwinWin32Xbox* xbox, mwinContext* context, const mwinWgiApi* api);
// Stops the motors still running and lets the pads go.
void mwinWin32XboxStop(mwinWin32Xbox* xbox);

// Looks for pads when due, reads them and stops rumbles due, at a time.
void mwinWin32XboxPump(mwinWin32Xbox* xbox, uint64_t nowNs);

// The rumble of the pad in a core slot: false when no Xbox pad has it.
bool mwinWin32XboxOwns(const mwinWin32Xbox* xbox, uint32_t slot);
mwinResult mwinWin32XboxRumble(mwinWin32Xbox* xbox, uint32_t slot, float low, float high,
                               uint32_t durationMs, uint64_t nowNs);

#endif // MAUL_WINDOW_SRC_WIN32_XBOX_H
