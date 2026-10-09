// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Pens on X11 (x11_pen.h).

#include "x11_pen.h"

// Tilt is in degrees on the Wacom and libinput drivers alike.
#define MOST_TILT 90.0

static double Value(xcb_input_fp3232_t value)
{
    return (double)value.integral + (double)value.frac / 4294967296.0;
}

// Whether a name holds "eraser", in any case.
static bool NamesEraser(const char* name, size_t length)
{
    static const char eraser[] = "eraser";
    size_t size = sizeof(eraser) - 1;
    for (size_t at = 0; length >= size && at <= length - size; at++)
    {
        size_t i = 0;
        while (i < size && (name[at + i] | 0x20) == eraser[i])
        {
            i++;
        }
        if (i == size)
        {
            return true;
        }
    }
    return false;
}

mwinX11PenKind mwinX11PenKindOf(xcb_atom_t type, mwinX11PenTypes types, const char* name,
                                size_t length, bool pressure)
{
    if (type != XCB_ATOM_NONE && type == types.eraser)
    {
        return mwin_x11Eraser;
    }
    if (type != XCB_ATOM_NONE && type == types.stylus)
    {
        return NamesEraser(name, length) ? mwin_x11Eraser : mwin_x11Stylus;
    }
    if (type != XCB_ATOM_NONE && type != types.tablet)
    {
        return mwin_x11NotPen;
    }
    if (NamesEraser(name, length))
    {
        return mwin_x11Eraser;
    }
    return pressure ? mwin_x11Stylus : mwin_x11NotPen;
}

mwinX11Pen* mwinX11AddPen(mwinX11Pens* pens, uint16_t device, bool eraser)
{
    if (pens->count >= MWIN_X11_PENS)
    {
        return nullptr;
    }
    mwinX11Pen* pen = &pens->pens[pens->count++];
    *pen = (mwinX11Pen){.device = device, .eraser = eraser, .numbers = {-1, -1, -1}};
    return pen;
}

void mwinX11SetPenAxis(mwinX11Pen* pen, mwinX11PenAxis axis, uint16_t number,
                       xcb_input_fp3232_t minimum, xcb_input_fp3232_t maximum)
{
    pen->numbers[axis] = number;
    pen->minimums[axis] = Value(minimum);
    pen->maximums[axis] = Value(maximum);
    pen->values[axis] = pen->minimums[axis];
}

mwinX11Pen* mwinX11FindPen(mwinX11Pens* pens, uint16_t device)
{
    for (uint32_t i = 0; i < pens->count; i++)
    {
        if (pens->pens[i].device == device)
        {
            return &pens->pens[i];
        }
    }
    return nullptr;
}

// Keeps the values of the pen's valuators an event carries.
static void ReadAxes(mwinX11Pen* pen, const uint32_t* mask, int words,
                     const xcb_input_fp3232_t* values, int count)
{
    int index = 0;
    for (int bit = 0; bit < words * 32 && index < count; bit++)
    {
        if ((mask[bit / 32] & (1u << (bit % 32))) == 0)
        {
            continue;
        }
        for (int axis = 0; axis < mwin_x11PenAxes; axis++)
        {
            if (pen->numbers[axis] == bit)
            {
                pen->values[axis] = Value(values[index]);
            }
        }
        index += 1;
    }
}

static double Clamp(double value, double low, double high)
{
    return value < low ? low : value > high ? high : value;
}

// The pen as it is: pressure over its range while in contact (full
// without a pressure valuator), 0 hovering; tilt in degrees.
static mwinPenEvent PenOf(const mwinX11Pen* pen, mwinPosition position)
{
    mwinPenEvent event = {.position = position, .flags = pen->flags};
    event.flags |= pen->eraser ? mwin_penEraser : 0;
    if ((pen->flags & mwin_penContact) != 0)
    {
        double range = pen->maximums[mwin_x11PenPressure] - pen->minimums[mwin_x11PenPressure];
        event.pressure = pen->numbers[mwin_x11PenPressure] < 0 || range <= 0.0
                             ? 1.0f
                             : (float)Clamp((pen->values[mwin_x11PenPressure] -
                                             pen->minimums[mwin_x11PenPressure]) /
                                                range,
                                            0.0, 1.0);
    }
    if (pen->numbers[mwin_x11PenTiltX] >= 0)
    {
        event.tiltX = (float)Clamp(pen->values[mwin_x11PenTiltX], -MOST_TILT, MOST_TILT);
    }
    if (pen->numbers[mwin_x11PenTiltY] >= 0)
    {
        event.tiltY = (float)Clamp(pen->values[mwin_x11PenTiltY], -MOST_TILT, MOST_TILT);
    }
    return event;
}

int mwinX11PenRecordOf(mwinX11Pen* pen, mwinX11PenInput input, uint32_t button,
                       mwinPosition position, const uint32_t* mask, int words,
                       const xcb_input_fp3232_t* values, int count, mwinEvent* recordOut)
{
    ReadAxes(pen, mask, words, values, count);
    bool pressed = input == mwin_x11PenPress;
    mwinEventType type = mwin_eventPenMoved;
    uint8_t number = 0;
    if (input != mwin_x11PenMotion)
    {
        mwinPenFlags flag = button == 1 ? mwin_penContact : button == 2 ? mwin_penBarrel : 0;
        if (flag == 0)
        {
            return 0;
        }
        pen->flags = pressed ? pen->flags | flag : pen->flags & (mwinPenFlags)~flag;
        if (button == 1)
        {
            type = pressed ? mwin_eventPenDown : mwin_eventPenUp;
        }
        else
        {
            type = pressed ? mwin_eventPenButtonDown : mwin_eventPenButtonUp;
            number = 1;
        }
    }
    *recordOut = (mwinEvent){.type = type};
    recordOut->data.pen = PenOf(pen, position);
    recordOut->data.pen.button = number;
    return 1;
}
