// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Pens on X11 (mwin-0037): the slave pointer devices that are a
// tablet's pen or eraser, and the pen records their XInput2 events make
// in place of mouse records. A device is a pen by its X input type
// (STYLUS or ERASER); one of type TABLET or none by an eraser in its
// name, else by a pressure valuator; pressure and tilt come from the
// valuators labelled for them, each kept from its last value, as an
// event carries only the changed ones. The tip is button 1, the lower
// barrel button 2.

#ifndef MAUL_WINDOW_SRC_X11_PEN_H
#define MAUL_WINDOW_SRC_X11_PEN_H

#include "maul-window/event.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <xcb/xinput.h>

#define MWIN_X11_PENS 8

// The valuators a pen reads.
typedef enum mwinX11PenAxis
{
    mwin_x11PenPressure,
    mwin_x11PenTiltX,
    mwin_x11PenTiltY,
    mwin_x11PenAxes,
} mwinX11PenAxis;

typedef struct mwinX11Pen
{
    uint16_t device;
    bool eraser;
    // Each axis's valuator number (-1 for none), its range and its last
    // value.
    int32_t numbers[mwin_x11PenAxes];
    double minimums[mwin_x11PenAxes];
    double maximums[mwin_x11PenAxes];
    double values[mwin_x11PenAxes];
    // Contact and the barrel button, as last told.
    mwinPenFlags flags;
} mwinX11Pen;

typedef struct mwinX11Pens
{
    mwinX11Pen pens[MWIN_X11_PENS];
    uint32_t count;
} mwinX11Pens;

// What a device is, from its X input type (compared with the STYLUS,
// ERASER and TABLET atoms), its name and whether it has a pressure
// valuator. A device of another type (a touch screen, whose valuators
// may carry pressure too, a mouse or a touchpad) is no pen.
typedef enum mwinX11PenKind
{
    mwin_x11NotPen,
    mwin_x11Stylus,
    mwin_x11Eraser,
} mwinX11PenKind;

typedef struct mwinX11PenTypes
{
    xcb_atom_t stylus;
    xcb_atom_t eraser;
    xcb_atom_t tablet;
} mwinX11PenTypes;

mwinX11PenKind mwinX11PenKindOf(xcb_atom_t type, mwinX11PenTypes types, const char* name,
                                size_t length, bool pressure);

// Adds a pen device without valuators: NULL past the table.
mwinX11Pen* mwinX11AddPen(mwinX11Pens* pens, uint16_t device, bool eraser);

// Gives a pen an axis's valuator and its range.
void mwinX11SetPenAxis(mwinX11Pen* pen, mwinX11PenAxis axis, uint16_t number,
                       xcb_input_fp3232_t minimum, xcb_input_fp3232_t maximum);

// The pen of a device, or NULL for a device that is not one.
mwinX11Pen* mwinX11FindPen(mwinX11Pens* pens, uint16_t device);

// The XInput2 events of a pen.
typedef enum mwinX11PenInput
{
    mwin_x11PenMotion,
    mwin_x11PenPress,
    mwin_x11PenRelease,
} mwinX11PenInput;

// The pen records an event of a pen makes (at most one): its valuators
// (mask of words 32-bit words, one value for each bit set) read first,
// the place given. The count of records, 0 for a button no record has.
int mwinX11PenRecordOf(mwinX11Pen* pen, mwinX11PenInput input, uint32_t button,
                       mwinPosition position, const uint32_t* mask, int words,
                       const xcb_input_fp3232_t* values, int count, mwinEvent* recordOut);

#endif // MAUL_WINDOW_SRC_X11_PEN_H
