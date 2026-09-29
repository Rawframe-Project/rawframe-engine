// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Numbered gamepad controls through their mapping.

#include "pad_map.h"

static bool IsPressed(const mwinPadControls* pad, mwinPadSource source)
{
    uint8_t index = mwinPadSourceIndex(source);
    switch (mwinPadSourceKind(source))
    {
    case mwin_padSourceButton:
        return index < pad->buttonCount && pad->buttons[index];
    case mwin_padSourceAxis:
    {
        float value = index < pad->axisCount ? pad->axes[index] : 0.0f;
        value = mwinPadSourceInverted(source) ? -value : value;
        return mwinPadSourceHalf(source) == 2 ? value < -0.5f : value > 0.5f;
    }
    case mwin_padSourceHat:
        return (pad->hats[index] & mwinPadSourceMask(source)) != 0;
    default:
        return false;
    }
}

// An axis control's value from its source: a stick's from -1 to 1, a
// trigger's from 0 to 1.
static float AxisValue(const mwinPadControls* pad, mwinPadSource source, bool trigger)
{
    if (mwinPadSourceKind(source) != mwin_padSourceAxis)
    {
        return IsPressed(pad, source) ? 1.0f : 0.0f;
    }
    uint8_t index = mwinPadSourceIndex(source);
    float value = index < pad->axisCount ? pad->axes[index] : 0.0f;
    value = mwinPadSourceInverted(source) ? -value : value;
    int half = mwinPadSourceHalf(source);
    if (half == 0)
    {
        return trigger ? (value + 1.0f) / 2.0f : value;
    }
    float part = half == 1 ? (value > 0.0f ? value : 0.0f) : (value < 0.0f ? -value : 0.0f);
    return trigger ? part : part * 2.0f - 1.0f;
}

// Posts a raw gamepad's controls: its buttons, its axes, and each hat as
// two axes after the others.
static void PostRaw(mwinContext* context, uint32_t slot, const mwinPadControls* pad,
                    uint64_t timeNs)
{
    for (uint8_t i = 0; i < pad->buttonCount && i < MWIN_GAMEPAD_RAW_BUTTONS; i++)
    {
        mwinPostGamepadButton(context, slot, i, pad->buttons[i], timeNs);
    }
    for (uint8_t i = 0; i < pad->axisCount; i++)
    {
        mwinPostGamepadAxis(context, slot, i, pad->axes[i], timeNs);
    }
    for (uint8_t i = 0; i < pad->hatCount; i++)
    {
        uint8_t hat = pad->hats[i];
        float x = (hat & 2) != 0 ? 1.0f : ((hat & 8) != 0 ? -1.0f : 0.0f);
        float y = (hat & 4) != 0 ? 1.0f : ((hat & 1) != 0 ? -1.0f : 0.0f);
        uint8_t first = (uint8_t)(pad->axisCount + 2 * i);
        mwinPostGamepadAxis(context, slot, first, x, timeNs);
        mwinPostGamepadAxis(context, slot, (uint8_t)(first + 1), y, timeNs);
    }
}

void mwinPostPadControls(mwinContext* context, uint32_t slot, const mwinPadControls* pad,
                         uint64_t timeNs)
{
    if (pad->mapping == nullptr)
    {
        PostRaw(context, slot, pad, timeNs);
        return;
    }
    const mwinPadSource* sources = pad->mapping->sources;
    for (uint8_t i = 0; i < MWIN_GAMEPAD_BUTTONS; i++)
    {
        mwinPostGamepadButton(context, slot, i, IsPressed(pad, sources[i]), timeNs);
    }
    for (uint8_t i = 0; i < MWIN_GAMEPAD_AXES; i++)
    {
        mwinPadSource source = sources[MWIN_GAMEPAD_BUTTONS + i];
        float value = source != 0 ? AxisValue(pad, source, i >= mwin_padTriggerLeft) : 0.0f;
        // A stick's halves driven by buttons or a hat, on top of an axis
        // where the mapping gives both, held to the stick's range.
        if (i < mwin_padTriggerLeft && pad->halves != nullptr)
        {
            value += (IsPressed(pad, pad->halves[2 * i + 1]) ? 1.0f : 0.0f) -
                     (IsPressed(pad, pad->halves[2 * i]) ? 1.0f : 0.0f);
            value = value < -1.0f ? -1.0f : (value > 1.0f ? 1.0f : value);
        }
        mwinPostGamepadAxis(context, slot, i, value, timeNs);
    }
}

float mwinPadNormalize(int32_t value, int32_t minimum, int32_t maximum)
{
    if (maximum <= minimum)
    {
        return 0.0f;
    }
    float normal = (float)((double)(value - minimum) * 2.0 / (double)(maximum - minimum) - 1.0);
    return normal < -1.0f ? -1.0f : (normal > 1.0f ? 1.0f : normal);
}
