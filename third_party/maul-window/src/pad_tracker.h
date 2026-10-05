// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Gamepads a platform runtime lists and reads (Windows.Gaming.Input on
// Windows, GameController on Apple's systems): looked for when the
// runtime says one came or went, and every half second in case it does
// not say; read at each pump, their controls posted only when the
// reading's time moves; their batteries read when they are looked for.
// A runtime keeps a motor running until told otherwise, so a rumble is
// stopped at the pump after its time runs out. The runtime is a table of
// functions, so a test can stand in for it.

#ifndef MAUL_WINDOW_SRC_PAD_TRACKER_H
#define MAUL_WINDOW_SRC_PAD_TRACKER_H

#include "core.h"

#define MWIN_PAD_TRACKER_PADS 16

// A pad's controls in the contract's terms: when the runtime read them
// (in its own units, moving with each report), the buttons held (bit b
// for mwinGamepadButton b), and the axes in mwinGamepadAxis order,
// sticks' y down positive.
typedef struct mwinPadReading
{
    uint64_t timestamp;
    uint32_t buttons;
    float axes[MWIN_GAMEPAD_AXES];
} mwinPadReading;

// A pad is an opaque reference the table hands out; the same pad is the
// same pointer each time it is listed.
typedef struct mwinPadRuntime
{
    void* self;
    // Lists the pads connected now, up to capacity, each with a reference
    // the caller releases; how many, or -1 when the runtime fails.
    int32_t (*list)(void* self, void** pads, uint32_t capacity);
    void (*release)(void* self, void* pad);
    bool (*read)(void* self, void* pad, mwinPadReading* reading);
    // Runs the heavy and light motors, each from 0 to 1; both 0 stops
    // them.
    bool (*vibrate)(void* self, void* pad, float low, float high);
    // Its name, vendor, product and capabilities into info.
    void (*describe)(void* self, void* pad, mwinGamepadInfo* info);
    // Its battery's charge in percent, or -1 where it has none or says
    // nothing.
    int8_t (*battery)(void* self, void* pad);
    // Whether a pad came or went since the last call.
    bool (*changed)(void* self);
} mwinPadRuntime;

typedef struct mwinTrackedPad
{
    // The runtime's reference, and the core's gamepad slot.
    void* pad;
    uint32_t slot;
    // The last reading's time, and when the motors stop (0 while they
    // are still).
    uint64_t timestamp;
    uint64_t rumbleEndsNs;
} mwinTrackedPad;

typedef struct mwinPadTracker
{
    mwinContext* context;
    mwinPadRuntime runtime;
    mwinTrackedPad pads[MWIN_PAD_TRACKER_PADS];
    uint32_t count;
    // When the pads were last looked for.
    uint64_t searchedNs;
} mwinPadTracker;

void mwinPadTrackerStart(mwinPadTracker* tracker, mwinContext* context,
                         const mwinPadRuntime* runtime);
// Stops the motors still running and lets the pads go.
void mwinPadTrackerStop(mwinPadTracker* tracker);

// Looks for pads when due, reads them and stops rumbles due, at a time.
void mwinPadTrackerPump(mwinPadTracker* tracker, uint64_t nowNs);

// The rumble of the pad in a core slot: false when the tracker does not
// have it.
bool mwinPadTrackerOwns(const mwinPadTracker* tracker, uint32_t slot);
mwinResult mwinPadTrackerRumble(mwinPadTracker* tracker, uint32_t slot, float low, float high,
                                uint32_t durationMs, uint64_t nowNs);

#endif // MAUL_WINDOW_SRC_PAD_TRACKER_H
