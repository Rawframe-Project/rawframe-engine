// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Smooth scrolling on X11 (mwin-0020): the scroll valuators
// of XI 2.1 devices, and the wheel movement their changes make. A
// valuator's change over its increment is one wheel click; a vertical
// one grows as the content scrolls down, a horizontal one to the right.
// The first value after a restart only starts the count.

#ifndef MAUL_WINDOW_SRC_X11_SCROLL_H
#define MAUL_WINDOW_SRC_X11_SCROLL_H

#include <stdbool.h>
#include <stdint.h>
#include <xcb/xinput.h>

#define MWIN_X11_SCROLL_AXES 16

typedef struct mwinX11ScrollAxis
{
    uint16_t device;
    uint16_t number;
    bool horizontal;
    // The last value is known, and the next one counts from it.
    bool counting;
    double increment;
    double value;
} mwinX11ScrollAxis;

typedef struct mwinX11Scroll
{
    mwinX11ScrollAxis axes[MWIN_X11_SCROLL_AXES];
    uint32_t count;
} mwinX11Scroll;

// Adds a device's scroll valuator; one of no increment, or past the
// table, is left out.
void mwinX11AddScrollAxis(mwinX11Scroll* scroll, uint16_t device, uint16_t number, bool horizontal,
                          xcb_input_fp3232_t increment);

// Every valuator counts again from its next value.
void mwinX11RestartScroll(mwinX11Scroll* scroll);

// The wheel movement in the valuators a device's event carries (mask of
// words 32-bit words, and one value for each bit set): in clicks, x to
// the right and y up. False when the event carries no scroll valuator.
bool mwinX11Scrolled(mwinX11Scroll* scroll, uint16_t device, const uint32_t* mask, int words,
                     const xcb_input_fp3232_t* values, int count, float* x, float* y);

#endif // MAUL_WINDOW_SRC_X11_SCROLL_H
