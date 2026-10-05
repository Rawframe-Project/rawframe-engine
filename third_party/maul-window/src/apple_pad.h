// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Gamepads on Apple's systems (apple_pad.m, apple_rumble.m), which the
// macOS and iOS backends share: GameController's pads for the pad
// tracker, and CoreHaptics' motors. Included by Objective-C files only.

#ifndef MAUL_WINDOW_SRC_APPLE_PAD_H
#define MAUL_WINDOW_SRC_APPLE_PAD_H

#include "core.h"
#include "pad_tracker.h"

#import <Foundation/Foundation.h>

// GameController's pads: the tracker, what tells of their connections,
// whether one came or went since they were looked for, whether they are
// watched at all, and each rumbled pad's motors by its controller (nil
// before the first).
typedef struct mwinApplePads
{
    mwinPadTracker tracker;
    id observers[2];
    bool changed;
    bool started;
    id rumbles;
} mwinApplePads;

// Watches the pads from the start of a backend to its stop, and reads
// them at each pump.
void mwinAppleStartPads(mwinApplePads* pads, mwinContext* context);
void mwinAppleStopPads(mwinApplePads* pads);
void mwinApplePumpPads(mwinApplePads* pads, uint64_t nowNs);

// Whether a controller has motors; runs them, each from 0 to 1, both 0
// stopping them; lets go of the motors of controllers not kept, or of
// all with nil (apple_rumble.m).
bool mwinAppleCanRumble(id controller);
bool mwinAppleRumble(mwinApplePads* pads, id controller, float low, float high);
void mwinAppleForgetRumbles(mwinApplePads* pads, NSArray* kept);

#endif // MAUL_WINDOW_SRC_APPLE_PAD_H
