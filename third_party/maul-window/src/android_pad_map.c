// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An Android gamepad's controls turned into records (android_pad_map.h).

#include "android_pad_map.h"

// Android's key codes and axes (android/keycodes.h, android/input.h),
// spelled here so that this file builds on any platform.
enum
{
    KEY_BACK = 4,
    KEY_DPAD_UP = 19,
    KEY_DPAD_DOWN = 20,
    KEY_DPAD_LEFT = 21,
    KEY_DPAD_RIGHT = 22,
    KEY_MENU = 82,
    KEY_BUTTON_A = 96,
    KEY_BUTTON_B = 97,
    KEY_BUTTON_C = 98,
    KEY_BUTTON_X = 99,
    KEY_BUTTON_Y = 100,
    KEY_BUTTON_Z = 101,
    KEY_BUTTON_L1 = 102,
    KEY_BUTTON_R1 = 103,
    KEY_BUTTON_L2 = 104,
    KEY_BUTTON_R2 = 105,
    KEY_BUTTON_THUMBL = 106,
    KEY_BUTTON_THUMBR = 107,
    KEY_BUTTON_START = 108,
    KEY_BUTTON_SELECT = 109,
    KEY_BUTTON_MODE = 110,
    KEY_BUTTON_1 = 188,
};

enum
{
    AXIS_X = 0,
    AXIS_Y = 1,
    AXIS_Z = 11,
    AXIS_RX = 12,
    AXIS_RY = 13,
    AXIS_RZ = 14,
    AXIS_HAT_X = 15,
    AXIS_HAT_Y = 16,
    AXIS_LTRIGGER = 17,
    AXIS_RTRIGGER = 18,
    AXIS_GAS = 22,
    AXIS_BRAKE = 23,
};

// A hat's direction counts once it is past the middle.
#define HAT_PRESSED 0.5f

const int32_t mwinAndroidPadKeys[MWIN_ANDROID_PAD_KEYS] = {
    KEY_BUTTON_A,      KEY_BUTTON_B,      KEY_BUTTON_C,      KEY_BUTTON_X,      KEY_BUTTON_Y,
    KEY_BUTTON_Z,      KEY_BUTTON_L1,     KEY_BUTTON_R1,     KEY_BUTTON_L2,     KEY_BUTTON_R2,
    KEY_BUTTON_THUMBL, KEY_BUTTON_THUMBR, KEY_BUTTON_START,  KEY_BUTTON_SELECT, KEY_BUTTON_MODE,
    KEY_BUTTON_1,      KEY_BUTTON_1 + 1,  KEY_BUTTON_1 + 2,  KEY_BUTTON_1 + 3,  KEY_BUTTON_1 + 4,
    KEY_BUTTON_1 + 5,  KEY_BUTTON_1 + 6,  KEY_BUTTON_1 + 7,  KEY_BUTTON_1 + 8,  KEY_BUTTON_1 + 9,
    KEY_BUTTON_1 + 10, KEY_BUTTON_1 + 11, KEY_BUTTON_1 + 12, KEY_BUTTON_1 + 13, KEY_BUTTON_1 + 14,
    KEY_BUTTON_1 + 15, KEY_DPAD_UP,       KEY_DPAD_DOWN,     KEY_DPAD_LEFT,     KEY_DPAD_RIGHT,
    KEY_BACK,          KEY_MENU,
};

// The index in the layout's axes of an Android axis, or -1.
static int8_t IndexOf(const mwinAndroidPadLayout* layout, int32_t axis)
{
    for (uint32_t i = 0; i < layout->axisCount; i++)
    {
        if (layout->axes[i] == axis)
        {
            return (int8_t)i;
        }
    }
    return -1;
}

// Two axes as a pair when the layout has both.
static bool Pair(const mwinAndroidPadLayout* layout, int32_t first, int32_t second, int8_t* out)
{
    int8_t a = IndexOf(layout, first);
    int8_t b = IndexOf(layout, second);
    if (a < 0 || b < 0)
    {
        return false;
    }
    out[0] = a;
    out[1] = b;
    return true;
}

static uint8_t CountBits(uint64_t bits)
{
    uint8_t count = 0;
    for (; bits != 0; bits &= bits - 1)
    {
        count += 1;
    }
    return count;
}

void mwinAndroidPadLayoutOf(mwinAndroidPadLayout* layout, mwinGamepadInfo* info)
{
    if (layout->axisCount > MWIN_ANDROID_PAD_AXES)
    {
        layout->axisCount = MWIN_ANDROID_PAD_AXES;
    }
    layout->mapped = (layout->keys & 1u) != 0;
    int8_t* sources = layout->sources;
    for (uint32_t i = 0; i < MWIN_GAMEPAD_AXES; i++)
    {
        sources[i] = -1;
    }
    layout->hat[0] = -1;
    layout->hat[1] = -1;
    (void)Pair(layout, AXIS_X, AXIS_Y, &sources[mwin_padStickLeftX]);
    bool rightOnR = Pair(layout, AXIS_RX, AXIS_RY, &sources[mwin_padStickRightX]);
    if (!rightOnR)
    {
        (void)Pair(layout, AXIS_Z, AXIS_RZ, &sources[mwin_padStickRightX]);
    }
    int8_t* triggers = &sources[mwin_padTriggerLeft];
    layout->keyTriggers = !Pair(layout, AXIS_LTRIGGER, AXIS_RTRIGGER, triggers) &&
                          !Pair(layout, AXIS_BRAKE, AXIS_GAS, triggers) &&
                          !(rightOnR && Pair(layout, AXIS_Z, AXIS_RZ, triggers));
    (void)Pair(layout, AXIS_HAT_X, AXIS_HAT_Y, layout->hat);
    info->mapped = layout->mapped;
    uint8_t buttons = CountBits(layout->keys);
    info->rawButtons = buttons < MWIN_GAMEPAD_RAW_BUTTONS ? buttons : MWIN_GAMEPAD_RAW_BUTTONS;
    info->rawAxes = (uint8_t)(layout->axisCount < MWIN_GAMEPAD_RAW_AXES ? layout->axisCount
                                                                        : MWIN_GAMEPAD_RAW_AXES);
}

// The mapped button of a key, or -1.
static int32_t ButtonOf(int32_t keyCode)
{
    switch (keyCode)
    {
    case KEY_BUTTON_A:
        return mwin_padFaceSouth;
    case KEY_BUTTON_B:
        return mwin_padFaceEast;
    case KEY_BUTTON_X:
        return mwin_padFaceWest;
    case KEY_BUTTON_Y:
        return mwin_padFaceNorth;
    case KEY_BUTTON_L1:
        return mwin_padShoulderLeft;
    case KEY_BUTTON_R1:
        return mwin_padShoulderRight;
    case KEY_BUTTON_THUMBL:
        return mwin_padStickLeft;
    case KEY_BUTTON_THUMBR:
        return mwin_padStickRight;
    case KEY_BUTTON_START:
    case KEY_MENU:
        return mwin_padStart;
    case KEY_BUTTON_SELECT:
    case KEY_BACK:
        return mwin_padSelect;
    case KEY_BUTTON_MODE:
        return mwin_padGuide;
    case KEY_DPAD_UP:
        return mwin_padDpadUp;
    case KEY_DPAD_DOWN:
        return mwin_padDpadDown;
    case KEY_DPAD_LEFT:
        return mwin_padDpadLeft;
    case KEY_DPAD_RIGHT:
        return mwin_padDpadRight;
    default:
        return -1;
    }
}

// The raw button of a key: its place among the keys the gamepad has, or
// -1.
static int32_t RawButtonOf(const mwinAndroidPadLayout* layout, int32_t keyCode)
{
    int32_t button = 0;
    for (uint32_t i = 0; i < MWIN_ANDROID_PAD_KEYS; i++)
    {
        if ((layout->keys & (1ull << i)) == 0)
        {
            continue;
        }
        if (mwinAndroidPadKeys[i] == keyCode)
        {
            return button < MWIN_GAMEPAD_RAW_BUTTONS ? button : -1;
        }
        button += 1;
    }
    return -1;
}

void mwinAndroidPadKey(mwinContext* context, uint32_t slot, const mwinAndroidPadLayout* layout,
                       int32_t keyCode, bool down, uint64_t timeNs)
{
    if (!layout->mapped)
    {
        int32_t button = RawButtonOf(layout, keyCode);
        if (button >= 0)
        {
            mwinPostGamepadButton(context, slot, (uint8_t)button, down, timeNs);
        }
        return;
    }
    if (layout->keyTriggers && (keyCode == KEY_BUTTON_L2 || keyCode == KEY_BUTTON_R2))
    {
        uint8_t axis = keyCode == KEY_BUTTON_L2 ? mwin_padTriggerLeft : mwin_padTriggerRight;
        mwinPostGamepadAxis(context, slot, axis, down ? 1.0f : 0.0f, timeNs);
        return;
    }
    int32_t button = ButtonOf(keyCode);
    if (button >= 0)
    {
        mwinPostGamepadButton(context, slot, (uint8_t)button, down, timeNs);
    }
}

// A value of an axis from its range to 0..1.
static float Unit(const mwinAndroidPadLayout* layout, uint32_t index, float value)
{
    float span = layout->max[index] - layout->min[index];
    float unit = span > 0.0f ? (value - layout->min[index]) / span : 0.0f;
    return unit < 0.0f ? 0.0f : unit > 1.0f ? 1.0f : unit;
}

bool mwinAndroidIsPadKey(int32_t keyCode)
{
    for (uint32_t i = 0; i < MWIN_ANDROID_PAD_KEYS; i++)
    {
        if (mwinAndroidPadKeys[i] == keyCode)
        {
            return true;
        }
    }
    return false;
}

void mwinAndroidPadAxes(mwinContext* context, uint32_t slot, mwinAndroidPadLayout* layout,
                        const float* values, uint64_t timeNs)
{
    if (!layout->mapped)
    {
        for (uint32_t i = 0; i < layout->axisCount && i < MWIN_GAMEPAD_RAW_AXES; i++)
        {
            mwinPostGamepadAxis(context, slot, (uint8_t)i, values[i], timeNs);
        }
        return;
    }
    for (uint8_t axis = 0; axis < MWIN_GAMEPAD_AXES; axis++)
    {
        int8_t index = layout->sources[axis];
        if (index < 0)
        {
            continue;
        }
        float unit = Unit(layout, (uint32_t)index, values[index]);
        bool trigger = axis == mwin_padTriggerLeft || axis == mwin_padTriggerRight;
        if (trigger && layout->min[index] < 0.0f)
        {
            bool* moved = &layout->moved[axis - mwin_padTriggerLeft];
            *moved = *moved || values[index] != 0.0f;
            unit = *moved ? unit : 0.0f;
        }
        mwinPostGamepadAxis(context, slot, axis, trigger ? unit : unit * 2.0f - 1.0f, timeNs);
    }
    if (layout->hat[0] >= 0)
    {
        float x = values[layout->hat[0]];
        float y = values[layout->hat[1]];
        mwinPostGamepadButton(context, slot, mwin_padDpadLeft, x < -HAT_PRESSED, timeNs);
        mwinPostGamepadButton(context, slot, mwin_padDpadRight, x > HAT_PRESSED, timeNs);
        mwinPostGamepadButton(context, slot, mwin_padDpadUp, y < -HAT_PRESSED, timeNs);
        mwinPostGamepadButton(context, slot, mwin_padDpadDown, y > HAT_PRESSED, timeNs);
    }
}
