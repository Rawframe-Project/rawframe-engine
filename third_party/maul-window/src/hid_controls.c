// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A HID gamepad's controls and reports.

#include "hid_controls.h"

#include <string.h>

// Generic Desktop's usages: the six axes from X, the sliders, and the
// hat switch.
#define USAGE_X      0x30
#define USAGE_RZ     0x35
#define USAGE_SLIDER 0x36
#define USAGE_WHEEL  0x38
#define USAGE_HAT    0x39

// A hat's eight steps from up, clockwise, as direction bits.
static const uint8_t s_hatBits[8] = {1, 1 | 2, 2, 2 | 4, 4, 4 | 8, 8, 8 | 1};

// Adds a Button page usage in order, once.
static void AddButton(mwinHidControls* hid, uint16_t usage)
{
    uint8_t* count = &hid->controls.buttonCount;
    uint8_t at = 0;
    while (at < *count && hid->buttons[at] < usage)
    {
        at++;
    }
    if ((at < *count && hid->buttons[at] == usage) || *count == MWIN_PAD_NUMBERED_BUTTONS)
    {
        return;
    }
    memmove(&hid->buttons[at + 1], &hid->buttons[at], (*count - at) * sizeof(uint16_t));
    hid->buttons[at] = usage;
    *count += 1;
}

void mwinHidAddButtons(mwinHidControls* hid, const mwinHidCap* caps, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        for (uint32_t usage = caps[i].first;
             caps[i].page == MWIN_HID_PAGE_BUTTON && usage <= caps[i].last; usage++)
        {
            AddButton(hid, (uint16_t)usage);
        }
    }
}

// A value control of a capability: a range whose maximum reads as
// negative is the unsigned range of its bits.
static mwinHidValue ValueOf(const mwinHidCap* caps, uint16_t usage)
{
    mwinHidValue value = {usage, caps->bits, caps->minimum, caps->maximum};
    if (value.maximum < value.minimum && value.bits < 32)
    {
        value.minimum = 0;
        value.maximum = (int32_t)((1u << value.bits) - 1);
    }
    return value;
}

void mwinHidAddValues(mwinHidControls* hid, const mwinHidCap* caps, size_t count)
{
    mwinHidValue axes[USAGE_RZ - USAGE_X + 3] = {0};
    uint8_t sliders = 0;
    mwinPadControls* controls = &hid->controls;
    for (size_t i = 0; i < count; i++)
    {
        for (uint32_t usage = caps[i].first;
             caps[i].page == MWIN_HID_PAGE_GENERIC && usage <= caps[i].last; usage++)
        {
            mwinHidValue value = ValueOf(&caps[i], (uint16_t)usage);
            if (usage >= USAGE_X && usage <= USAGE_RZ)
            {
                axes[usage - USAGE_X] = value;
            }
            else if (usage >= USAGE_SLIDER && usage <= USAGE_WHEEL && sliders < 2)
            {
                axes[USAGE_RZ - USAGE_X + 1 + sliders++] = value;
            }
            else if (usage == USAGE_HAT && controls->hatCount < MWIN_PAD_NUMBERED_HATS)
            {
                hid->hats[controls->hatCount++] = value;
            }
        }
    }
    for (size_t i = 0; i < sizeof(axes) / sizeof(axes[0]); i++)
    {
        if (axes[i].usage != 0)
        {
            hid->axes[controls->axisCount++] = axes[i];
        }
    }
}

void mwinHidSetButtons(mwinHidControls* hid, const uint16_t* down, size_t count)
{
    mwinPadControls* controls = &hid->controls;
    memset(controls->buttons, 0, sizeof(controls->buttons));
    for (size_t i = 0; i < count; i++)
    {
        for (uint8_t b = 0; b < controls->buttonCount; b++)
        {
            controls->buttons[b] = controls->buttons[b] || hid->buttons[b] == down[i];
        }
    }
}

// A value's bits as the signed number a range below 0 means.
static int32_t Extend(uint32_t bits, const mwinHidValue* control)
{
    if (control->minimum >= 0 || control->bits == 0 || control->bits >= 32)
    {
        return (int32_t)bits;
    }
    uint32_t sign = 1u << (control->bits - 1);
    bits &= (sign << 1) - 1;
    return (int32_t)(bits ^ sign) - (int32_t)sign;
}

static uint8_t HatBits(const mwinHidValue* hat, int32_t value)
{
    // In 64 bits: a device's logical range may span the whole of int32_t.
    int64_t steps = (int64_t)hat->maximum - hat->minimum + 1;
    if (value < hat->minimum || value > hat->maximum)
    {
        return 0;
    }
    return s_hatBits[((int64_t)value - hat->minimum) * 8 / steps];
}

void mwinHidSetValue(mwinHidControls* hid, uint32_t index, uint32_t bits)
{
    mwinPadControls* controls = &hid->controls;
    if (index < controls->axisCount)
    {
        const mwinHidValue* axis = &hid->axes[index];
        controls->axes[index] = mwinPadNormalize(Extend(bits, axis), axis->minimum, axis->maximum);
    }
    else if (index < (uint32_t)controls->axisCount + controls->hatCount)
    {
        const mwinHidValue* hat = &hid->hats[index - controls->axisCount];
        controls->hats[index - controls->axisCount] = HatBits(hat, Extend(bits, hat));
    }
}

uint32_t mwinHidReports(size_t bytes, uint32_t count, uint32_t size)
{
    if (size == 0)
    {
        return 0;
    }
    size_t fit = bytes / size;
    return fit < count ? (uint32_t)fit : count;
}
