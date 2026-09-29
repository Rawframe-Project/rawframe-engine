// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A gamepad's numbered controls, as SDL numbers a device's buttons, axes
// and hats, and what they mean: through a mapping onto the standard
// layout, or as they are for a raw gamepad. The Linux and Win32 backends
// read devices into these, and post them from here.

#ifndef MAUL_WINDOW_SRC_PAD_MAP_H
#define MAUL_WINDOW_SRC_PAD_MAP_H

#include "core.h"
#include "pad_db.h"

#define MWIN_PAD_NUMBERED_BUTTONS 64
#define MWIN_PAD_NUMBERED_AXES    16
#define MWIN_PAD_NUMBERED_HATS    4

typedef struct mwinPadControls
{
    // The mapping and the halves of the sticks from buttons, or NULL for
    // a raw gamepad.
    const mwinPadMapping* mapping;
    const mwinPadSource* halves;
    uint8_t buttonCount;
    uint8_t axisCount;
    uint8_t hatCount;
    bool buttons[MWIN_PAD_NUMBERED_BUTTONS];
    // From -1 to 1.
    float axes[MWIN_PAD_NUMBERED_AXES];
    // Each hat's direction bits (1 up, 2 right, 4 down, 8 left).
    uint8_t hats[MWIN_PAD_NUMBERED_HATS];
} mwinPadControls;

// A value over a device's range as an axis's, from -1 to 1; 0 for an
// empty range.
float mwinPadNormalize(int32_t value, int32_t minimum, int32_t maximum);

// Posts what the controls mean now: a mapped gamepad's standard buttons
// and axes, or a raw one's buttons, axes, and each hat as two axes after
// the others. The core posts only changes.
void mwinPostPadControls(mwinContext* context, uint32_t slot, const mwinPadControls* controls,
                         uint64_t timeNs);

#endif // MAUL_WINDOW_SRC_PAD_MAP_H
