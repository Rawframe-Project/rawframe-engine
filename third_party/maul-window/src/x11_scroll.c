// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The wheel movement of XI 2.1 scroll valuators.

#include "x11_scroll.h"

static double Value(xcb_input_fp3232_t value)
{
    return (double)value.integral + (double)value.frac / 4294967296.0;
}

void mwinX11AddScrollAxis(mwinX11Scroll* scroll, uint16_t device, uint16_t number, bool horizontal,
                          xcb_input_fp3232_t increment)
{
    double step = Value(increment);
    if (step != 0.0 && scroll->count < MWIN_X11_SCROLL_AXES)
    {
        scroll->axes[scroll->count++] =
            (mwinX11ScrollAxis){device, number, horizontal, false, step, 0.0};
    }
}

void mwinX11RestartScroll(mwinX11Scroll* scroll)
{
    for (uint32_t i = 0; i < scroll->count; i++)
    {
        scroll->axes[i].counting = false;
    }
}

static mwinX11ScrollAxis* Find(mwinX11Scroll* scroll, uint16_t device, int number)
{
    for (uint32_t i = 0; i < scroll->count; i++)
    {
        if (scroll->axes[i].device == device && scroll->axes[i].number == number)
        {
            return &scroll->axes[i];
        }
    }
    return nullptr;
}

bool mwinX11Scrolled(mwinX11Scroll* scroll, uint16_t device, const uint32_t* mask, int words,
                     const xcb_input_fp3232_t* values, int count, float* x, float* y)
{
    bool scrolled = false;
    double across = 0.0;
    double down = 0.0;
    int index = 0;
    for (int bit = 0; bit < words * 32 && index < count; bit++)
    {
        if ((mask[bit / 32] & (1u << (bit % 32))) == 0)
        {
            continue;
        }
        mwinX11ScrollAxis* axis = Find(scroll, device, bit);
        double value = Value(values[index++]);
        if (axis == nullptr)
        {
            continue;
        }
        scrolled = true;
        double clicks = axis->counting ? (value - axis->value) / axis->increment : 0.0;
        across += axis->horizontal ? clicks : 0.0;
        down += axis->horizontal ? 0.0 : clicks;
        axis->value = value;
        axis->counting = true;
    }
    *x = (float)across;
    *y = (float)-down;
    return scrolled;
}
