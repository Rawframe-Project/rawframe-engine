// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Touch on X11 (x11_touch.h).

#include "x11_touch.h"

static double Value(xcb_input_fp3232_t value)
{
    return (double)value.integral + (double)value.frac / 4294967296.0;
}

void mwinX11ClearTouchDevices(mwinX11Touches* touches)
{
    touches->deviceCount = 0;
}

mwinX11TouchDevice* mwinX11AddTouchDevice(mwinX11Touches* touches, uint16_t device)
{
    if (touches->deviceCount >= MWIN_X11_TOUCH_DEVICES)
    {
        return nullptr;
    }
    mwinX11TouchDevice* added = &touches->devices[touches->deviceCount++];
    *added = (mwinX11TouchDevice){.device = device, .pressure = -1};
    return added;
}

void mwinX11SetTouchPressure(mwinX11TouchDevice* device, uint16_t number,
                             xcb_input_fp3232_t minimum, xcb_input_fp3232_t maximum)
{
    // A valuator of no range measures nothing.
    if (Value(maximum) > Value(minimum))
    {
        device->pressure = number;
        device->minimum = Value(minimum);
        device->maximum = Value(maximum);
    }
}

static const mwinX11TouchDevice* FindDevice(const mwinX11Touches* touches, uint16_t device)
{
    for (uint32_t i = 0; i < touches->deviceCount; i++)
    {
        if (touches->devices[i].device == device)
        {
            return &touches->devices[i];
        }
    }
    return nullptr;
}

// The kept touch of an id, one taken for it when it has none (NULL when
// every one is taken).
static mwinX11Touch* TouchOf(mwinX11Touches* touches, uint64_t id)
{
    mwinX11Touch* unused = nullptr;
    for (uint32_t i = 0; i < MWIN_X11_TOUCHES; i++)
    {
        mwinX11Touch* touch = &touches->touches[i];
        if (touch->live && touch->id == id)
        {
            return touch;
        }
        unused = !touch->live ? touch : unused;
    }
    if (unused != nullptr)
    {
        *unused = (mwinX11Touch){.id = id, .pressure = -1.0f, .live = true};
    }
    return unused;
}

// The pressure an event carries over the device's range, or -1 where it
// carries none.
static float PressureOf(const mwinX11TouchDevice* device, const uint32_t* mask, int words,
                        const xcb_input_fp3232_t* values, int count)
{
    int index = 0;
    for (int bit = 0; bit < words * 32 && index < count; bit++)
    {
        if ((mask[bit / 32] & (1u << (bit % 32))) == 0)
        {
            continue;
        }
        if (bit == device->pressure)
        {
            double scaled =
                (Value(values[index]) - device->minimum) / (device->maximum - device->minimum);
            return (float)(scaled < 0.0 ? 0.0 : scaled > 1.0 ? 1.0 : scaled);
        }
        index += 1;
    }
    return -1.0f;
}

int mwinX11TouchRecordOf(mwinX11Touches* touches, uint16_t device, mwinX11TouchInput input,
                         uint32_t touch, mwinPosition position, const uint32_t* mask, int words,
                         const xcb_input_fp3232_t* values, int count, mwinEvent* recordOut)
{
    const mwinX11TouchDevice* screen = FindDevice(touches, device);
    if (screen == nullptr)
    {
        return 0;
    }
    uint64_t id = (uint64_t)device << 32 | touch;
    float pressure = PressureOf(screen, mask, words, values, count);
    mwinX11Touch* kept = TouchOf(touches, id);
    if (kept != nullptr)
    {
        // A new touch tells its own pressure or none.
        kept->pressure = input == mwin_x11TouchBegin ? -1.0f : kept->pressure;
        kept->pressure = pressure >= 0.0f ? pressure : kept->pressure;
        pressure = kept->pressure;
        kept->live = input != mwin_x11TouchEnd;
    }
    *recordOut = (mwinEvent){.type = input == mwin_x11TouchBegin    ? mwin_eventTouchDown
                                     : input == mwin_x11TouchUpdate ? mwin_eventTouchMoved
                                                                    : mwin_eventTouchUp};
    recordOut->data.touch = (mwinTouchEvent){id, position, pressure};
    return 1;
}
