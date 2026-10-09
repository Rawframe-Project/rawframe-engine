// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Touch on X11 (mwin-0039): the slave devices with a direct touch class,
// which are touch screens, and the touch records their XInput 2.2 touch
// events make. A touch's id is its device's number in the high 32 bits
// and the server's touch number in the low. Pressure comes from the
// valuator labelled "Abs MT Pressure", over its range, kept per touch
// when an event leaves it out; -1 for a device without one.

#ifndef MAUL_WINDOW_SRC_X11_TOUCH_H
#define MAUL_WINDOW_SRC_X11_TOUCH_H

#include "maul-window/event.h"

#include <stdbool.h>
#include <stdint.h>
#include <xcb/xinput.h>

#define MWIN_X11_TOUCH_DEVICES 8
// The touches whose pressure is kept at once; one past them still makes
// its records, with the pressure its own events carry.
#define MWIN_X11_TOUCHES 16

typedef struct mwinX11TouchDevice
{
    uint16_t device;
    // The pressure valuator's number (-1 for none) and its range.
    int32_t pressure;
    double minimum;
    double maximum;
} mwinX11TouchDevice;

typedef struct mwinX11Touch
{
    uint64_t id;
    // The last pressure told, over the range; -1 until one is.
    float pressure;
    bool live;
} mwinX11Touch;

typedef struct mwinX11Touches
{
    mwinX11TouchDevice devices[MWIN_X11_TOUCH_DEVICES];
    uint32_t deviceCount;
    mwinX11Touch touches[MWIN_X11_TOUCHES];
} mwinX11Touches;

// Forgets the devices, as before reading them again; the touches going on
// are kept.
void mwinX11ClearTouchDevices(mwinX11Touches* touches);

// Adds a touch screen without a pressure valuator: NULL past the table.
mwinX11TouchDevice* mwinX11AddTouchDevice(mwinX11Touches* touches, uint16_t device);

// Gives a touch screen its pressure valuator and its range; one of no
// range is left out.
void mwinX11SetTouchPressure(mwinX11TouchDevice* device, uint16_t number,
                             xcb_input_fp3232_t minimum, xcb_input_fp3232_t maximum);

// The XInput 2.2 touch events.
typedef enum mwinX11TouchInput
{
    mwin_x11TouchBegin,
    mwin_x11TouchUpdate,
    mwin_x11TouchEnd,
} mwinX11TouchInput;

// The touch record a touch event of a device makes, the place given and
// its valuators read (mask of words 32-bit words, one value for each bit
// set). The count of records: 1, or 0 for a device that is no touch
// screen.
int mwinX11TouchRecordOf(mwinX11Touches* touches, uint16_t device, mwinX11TouchInput input,
                         uint32_t touch, mwinPosition position, const uint32_t* mask, int words,
                         const xcb_input_fp3232_t* values, int count, mwinEvent* recordOut);

#endif // MAUL_WINDOW_SRC_X11_TOUCH_H
