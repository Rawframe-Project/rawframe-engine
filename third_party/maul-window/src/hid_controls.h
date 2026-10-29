// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A HID gamepad's controls from the capabilities of its input report,
// numbered as DirectInput numbers them so that SDL's Windows mappings
// apply, and its reports read into them. Everything here takes plain
// values, which a device hands over and which the reading must hold up
// against, so it is tested on Linux as well as Windows and fuzzed; win32_hid.c
// reads the device through Windows' HID parser and hands them on.

#ifndef MAUL_WINDOW_SRC_HID_CONTROLS_H
#define MAUL_WINDOW_SRC_HID_CONTROLS_H

#include "pad_map.h"

// The usage pages the controls come from.
#define MWIN_HID_PAGE_GENERIC 0x01
#define MWIN_HID_PAGE_BUTTON  0x09

// A capability of the input report: a usage, or a range of them, of a
// page; for a value, its size in bits and its logical range.
typedef struct mwinHidCap
{
    uint16_t page;
    uint16_t first;
    uint16_t last;
    uint16_t bits;
    int32_t minimum;
    int32_t maximum;
} mwinHidCap;

// A value control, an axis or a hat: its usage, and its size and range.
typedef struct mwinHidValue
{
    uint16_t usage;
    uint16_t bits;
    int32_t minimum;
    int32_t maximum;
} mwinHidValue;

typedef struct mwinHidControls
{
    // The Button page's usages in the numbering's order, and the axes and
    // hats in theirs.
    uint16_t buttons[MWIN_PAD_NUMBERED_BUTTONS];
    mwinHidValue axes[MWIN_PAD_NUMBERED_AXES];
    mwinHidValue hats[MWIN_PAD_NUMBERED_HATS];
    mwinPadControls controls;
} mwinHidControls;

// Numbers the buttons: the Button page's usages in order, each once, up to
// MWIN_PAD_NUMBERED_BUTTONS.
void mwinHidAddButtons(mwinHidControls* hid, const mwinHidCap* caps, size_t count);

// Numbers the axes and hats of the Generic Desktop page: the six axes
// from X in usage order, then up to two sliders, dials or wheels; the
// hats apart. A range whose maximum reads below its minimum is the
// unsigned range of its bits.
void mwinHidAddValues(mwinHidControls* hid, const mwinHidCap* caps, size_t count);

// A report's buttons: the usages down, of any number and order.
void mwinHidSetButtons(mwinHidControls* hid, const uint16_t* down, size_t count);

// A report's value of the axis or hat at an index (the axes, then the
// hats), as the bits the report holds.
void mwinHidSetValue(mwinHidControls* hid, uint32_t index, uint32_t bits);

// How many reports of a size a raw input holds, of the count it gives, in
// the bytes after its header; none of size 0.
uint32_t mwinHidReports(size_t bytes, uint32_t count, uint32_t size);

#endif // MAUL_WINDOW_SRC_HID_CONTROLS_H
