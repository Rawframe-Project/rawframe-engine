// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32 gamepads. Xbox-compatible pads come through Windows.Gaming.Input
// (win32_wgi.h, read by pad_tracker.h) where the runtime is found when a
// context starts, and otherwise through XInput, loaded from
// xinput1_4.dll, or xinput1_3.dll or xinput9_1_0.dll on older Windows:
// up to four pads, mapped. Connected pads are read at each pump; the free player slots are looked
// at every half second, as asking about a slot with no pad costs time. XInput has no durations, so
// a rumble is stopped at the pump after its time runs out. The functions are a table, so a test can
// stand in for XInput. The other gamepads come through Raw Input (win32_hid.h), to a message-only
// window of the pads' own.

#ifndef MAUL_WINDOW_SRC_WIN32_PAD_H
#define MAUL_WINDOW_SRC_WIN32_PAD_H

#include "core.h"
#include "pad_tracker.h"
#include "win32_hid.h"
#include "win32_wgi.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// After windows.h, whose types it uses.
#include <xinput.h>

#define MWIN_WIN32_PADS 4

typedef struct mwinXInputApi
{
    HMODULE library;
    DWORD(WINAPI* getState)(DWORD user, XINPUT_STATE* state);
    DWORD(WINAPI* setState)(DWORD user, XINPUT_VIBRATION* vibration);
    DWORD(WINAPI* getBattery)(DWORD user, BYTE type, XINPUT_BATTERY_INFORMATION* battery);
} mwinXInputApi;

typedef struct mwinWin32Pad
{
    bool connected;
    // The core's gamepad slot.
    uint32_t slot;
    // The last state's packet number, and when the motors stop (0 while
    // they are still).
    DWORD packet;
    uint64_t rumbleEndsNs;
    int8_t battery;
} mwinWin32Pad;

typedef struct mwinWin32Pads
{
    mwinContext* context;
    mwinXInputApi api;
    mwinWin32Pad pads[MWIN_WIN32_PADS];
    // When the free player slots were last looked at.
    uint64_t searchedNs;
    // The generic gamepads, and the window Raw Input brings them to.
    mwinWin32Hid hid;
    HWND listener;
    // Windows.Gaming.Input and its pads, while the runtime is there.
    mwinWgi wgi;
    mwinPadTracker xbox;
    bool runtime;
} mwinWin32Pads;

// Finds Windows.Gaming.Input, else loads XInput; without either there
// are no Xbox pads, and no error.
void mwinWin32PadsStart(mwinWin32Pads* pads, mwinContext* context);
void mwinWin32PadsStop(mwinWin32Pads* pads);

// Reads the pads, finds new ones and stops rumbles due, at a time.
void mwinWin32PadsPump(mwinWin32Pads* pads, uint64_t nowNs);

// The backend's rumble and trigger rumble.
mwinResult mwinWin32PadsRumble(mwinWin32Pads* pads, uint32_t slot, float low, float high,
                               uint32_t durationMs, uint64_t nowNs);
mwinResult mwinWin32PadsTriggerRumble(mwinWin32Pads* pads, uint32_t slot, float left, float right,
                                      uint32_t durationMs, uint64_t nowNs);

#endif // MAUL_WINDOW_SRC_WIN32_PAD_H
