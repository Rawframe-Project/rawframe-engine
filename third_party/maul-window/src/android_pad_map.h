// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An Android gamepad's controls turned into records, with no platform
// type, so that it builds and is tested on any platform. Android's key
// layouts name a known gamepad's controls (the south face button is
// BUTTON_A, the left stick X and Y); one with a south face button comes
// mapped, as SDL maps it, and any other raw.
//
// - Buttons: A south, B east, X west, Y north (the Xbox layout's names,
//   as Android's layouts give them), L1 and R1 the shoulders, THUMBL
//   and THUMBR the sticks, START and MENU start, SELECT and BACK select,
//   MODE the guide, the dpad's keys the dpad; a hat's axes press the
//   dpad too.
// - Axes: the left stick is X and Y; the right stick RX and RY where
//   both are there, else Z and RZ (Android's convention for the gamepads
//   it knows); the triggers LTRIGGER and RTRIGGER, else BRAKE and GAS,
//   else Z and RZ beside a right stick on RX and RY (the generic layout
//   of a Linux gamepad), else the L2 and R2 keys, pressed as 1. Sticks
//   are scaled from their range to -1..1, triggers to 0..1.
// - A raw gamepad's buttons are the keys of mwinAndroidPadKeys it has,
//   numbered in that order, and its axes its joystick axes in increasing
//   order, with Android's values.

#ifndef MAUL_WINDOW_SRC_ANDROID_PAD_MAP_H
#define MAUL_WINDOW_SRC_ANDROID_PAD_MAP_H

#include "core.h"

// The joystick axes of a gamepad kept, and the keys of gamepads.
#define MWIN_ANDROID_PAD_AXES 16
#define MWIN_ANDROID_PAD_KEYS 37

// The keys of gamepads (Android key codes), the first the south face
// button: the order of a layout's key bits and of a raw gamepad's
// buttons.
extern const int32_t mwinAndroidPadKeys[MWIN_ANDROID_PAD_KEYS];

// A gamepad's controls: the keys it has (bit i for mwinAndroidPadKeys[i])
// and its joystick axes (Android's axis ids, increasing, with their
// ranges), as Android describes it; then what mwinAndroidPadLayoutOf
// makes of them: whether it is mapped, the index in axes of each of the
// contract's axes and of the hat's two (-1 for none), and whether its
// triggers are the L2 and R2 keys; and whether each trigger read from a
// centered axis has moved off Android's first value.
typedef struct mwinAndroidPadLayout
{
    uint64_t keys;
    uint32_t axisCount;
    int32_t axes[MWIN_ANDROID_PAD_AXES];
    float min[MWIN_ANDROID_PAD_AXES];
    float max[MWIN_ANDROID_PAD_AXES];
    bool mapped;
    int8_t sources[MWIN_GAMEPAD_AXES];
    int8_t hat[2];
    bool keyTriggers;
    bool moved[2];
} mwinAndroidPadLayout;

// Makes the layout of the keys and axes described, and the facts the
// core keeps of it (whether mapped, how many raw buttons and axes).
void mwinAndroidPadLayoutOf(mwinAndroidPadLayout* layout, mwinGamepadInfo* info);

// Whether a key is a gamepad's (one of mwinAndroidPadKeys).
bool mwinAndroidIsPadKey(int32_t keyCode);

// A key of the gamepad in a core slot pressed or let go.
void mwinAndroidPadKey(mwinContext* context, uint32_t slot, const mwinAndroidPadLayout* layout,
                       int32_t keyCode, bool down, uint64_t timeNs);

// The gamepad's joystick axes read, one value for each of the layout's.
// Android gives an axis 0 until its device reports it, which a trigger
// at rest on a centered axis never does: such a trigger stays at rest
// until its value moves off 0.
void mwinAndroidPadAxes(mwinContext* context, uint32_t slot, mwinAndroidPadLayout* layout,
                        const float* values, uint64_t timeNs);

#endif // MAUL_WINDOW_SRC_ANDROID_PAD_MAP_H
