// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32's Xbox gamepads as Windows.Gaming.Input gives them.

#include "win32_xbox.h"

#define SEARCH_NS 500000000u

// The runtime's buttons in mwinGamepadButton order; it gives no guide
// button.
static const uint32_t s_buttons[MWIN_GAMEPAD_BUTTONS] = {
    mwin_wgiDpadUp,
    mwin_wgiDpadDown,
    mwin_wgiDpadLeft,
    mwin_wgiDpadRight,
    mwin_wgiA,
    mwin_wgiB,
    mwin_wgiX,
    mwin_wgiY,
    mwin_wgiShoulderLeft,
    mwin_wgiShoulderRight,
    mwin_wgiStickLeft,
    mwin_wgiStickRight,
    mwin_wgiMenu,
    mwin_wgiView,
    0,
};

void mwinWin32XboxStart(mwinWin32Xbox* xbox, mwinContext* context, const mwinWgiApi* api)
{
    *xbox = (mwinWin32Xbox){.context = context, .api = *api};
}

static void Still(mwinWin32Xbox* xbox, mwinWin32XboxPad* pad)
{
    static const mwinWgiMotors still = {0};
    (void)xbox->api.vibrate(xbox->api.self, pad->pad, &still);
    pad->rumbleEndsNs = 0;
}

void mwinWin32XboxStop(mwinWin32Xbox* xbox)
{
    for (uint32_t i = 0; i < xbox->count; i++)
    {
        if (xbox->pads[i].rumbleEndsNs != 0)
        {
            Still(xbox, &xbox->pads[i]);
        }
        xbox->api.release(xbox->api.self, xbox->pads[i].pad);
    }
    *xbox = (mwinWin32Xbox){0};
}

static float Unit(double value)
{
    return value < -1.0 ? -1.0f : value > 1.0 ? 1.0f : (float)value;
}

// Posts a pad's reading; sticks' y turned so down is positive.
static void Post(mwinWin32Xbox* xbox, const mwinWin32XboxPad* pad, const mwinWgiReading* reading,
                 uint64_t timeNs)
{
    for (uint8_t i = 0; i < MWIN_GAMEPAD_BUTTONS; i++)
    {
        bool down = (reading->buttons & s_buttons[i]) != 0;
        mwinPostGamepadButton(xbox->context, pad->slot, i, down, timeNs);
    }
    const float axes[MWIN_GAMEPAD_AXES] = {
        Unit(reading->leftX),   -Unit(reading->leftY),      Unit(reading->rightX),
        -Unit(reading->rightY), Unit(reading->leftTrigger), Unit(reading->rightTrigger),
    };
    for (uint8_t i = 0; i < MWIN_GAMEPAD_AXES; i++)
    {
        mwinPostGamepadAxis(xbox->context, pad->slot, i, axes[i], timeNs);
    }
}

static int32_t IndexOf(void* const* pads, int32_t count, const void* pad)
{
    for (int32_t i = 0; i < count; i++)
    {
        if (pads[i] == pad)
        {
            return i;
        }
    }
    return -1;
}

// Lets go of the pads no longer listed.
static void Forget(mwinWin32Xbox* xbox, void* const* listed, int32_t count, uint64_t nowNs)
{
    uint32_t kept = 0;
    for (uint32_t i = 0; i < xbox->count; i++)
    {
        mwinWin32XboxPad* pad = &xbox->pads[i];
        if (IndexOf(listed, count, pad->pad) >= 0)
        {
            xbox->pads[kept++] = *pad;
            continue;
        }
        mwinRemoveGamepad(xbox->context, pad->slot, nowNs);
        xbox->api.release(xbox->api.self, pad->pad);
    }
    xbox->count = kept;
}

static bool Tracked(const mwinWin32Xbox* xbox, const void* pad)
{
    for (uint32_t i = 0; i < xbox->count; i++)
    {
        if (xbox->pads[i].pad == pad)
        {
            return true;
        }
    }
    return false;
}

// Adds a pad newly listed, keeping its reference; lets it go when the
// core has no slot for it.
static void Add(mwinWin32Xbox* xbox, void* listed, uint64_t nowNs)
{
    mwinGamepadInfo info = {.mapped = true, .capabilities = mwin_padRumble};
    xbox->api.describe(xbox->api.self, listed, &info);
    info.battery = xbox->api.battery(xbox->api.self, listed);
    int32_t slot =
        xbox->count < MWIN_WIN32_XBOX_PADS ? mwinAddGamepad(xbox->context, &info, nowNs) : -1;
    if (slot < 0)
    {
        xbox->api.release(xbox->api.self, listed);
        return;
    }
    // Its first reading is posted whatever its time.
    xbox->pads[xbox->count++] =
        (mwinWin32XboxPad){.pad = listed, .slot = (uint32_t)slot, .timestamp = UINT64_MAX};
}

// A pad's battery, told when it changes.
static void Recharge(mwinWin32Xbox* xbox, const mwinWin32XboxPad* pad, uint64_t nowNs)
{
    int8_t battery = xbox->api.battery(xbox->api.self, pad->pad);
    mwinGamepadInfo info = xbox->context->gamepads[pad->slot].info;
    if (battery != info.battery)
    {
        info.battery = battery;
        mwinChangeGamepad(xbox->context, pad->slot, &info, nowNs);
    }
}

// Looks for pads: those gone are removed, those new added, the rest's
// batteries read.
static void Search(mwinWin32Xbox* xbox, uint64_t nowNs)
{
    void* listed[MWIN_WIN32_XBOX_PADS];
    int32_t count = xbox->api.list(xbox->api.self, listed, MWIN_WIN32_XBOX_PADS);
    if (count < 0)
    {
        return;
    }
    Forget(xbox, listed, count, nowNs);
    uint32_t known = xbox->count;
    for (int32_t i = 0; i < count; i++)
    {
        if (Tracked(xbox, listed[i]))
        {
            xbox->api.release(xbox->api.self, listed[i]);
        }
        else
        {
            Add(xbox, listed[i], nowNs);
        }
    }
    for (uint32_t i = 0; i < known; i++)
    {
        Recharge(xbox, &xbox->pads[i], nowNs);
    }
}

void mwinWin32XboxPump(mwinWin32Xbox* xbox, uint64_t nowNs)
{
    // The runtime's word is taken first, so a pad it names is found now.
    bool told = xbox->api.changed(xbox->api.self);
    if (told || xbox->searchedNs == 0 || nowNs - xbox->searchedNs >= SEARCH_NS)
    {
        xbox->searchedNs = nowNs;
        Search(xbox, nowNs);
    }
    for (uint32_t i = 0; i < xbox->count; i++)
    {
        mwinWin32XboxPad* pad = &xbox->pads[i];
        mwinWgiReading reading;
        if (xbox->api.read(xbox->api.self, pad->pad, &reading) &&
            reading.timestamp != pad->timestamp)
        {
            pad->timestamp = reading.timestamp;
            Post(xbox, pad, &reading, nowNs);
        }
        if (pad->rumbleEndsNs != 0 && nowNs >= pad->rumbleEndsNs)
        {
            Still(xbox, pad);
        }
    }
}

// The index of the pad in a core slot, or -1.
static int32_t PadOf(const mwinWin32Xbox* xbox, uint32_t slot)
{
    for (uint32_t i = 0; i < xbox->count; i++)
    {
        if (xbox->pads[i].slot == slot)
        {
            return (int32_t)i;
        }
    }
    return -1;
}

bool mwinWin32XboxOwns(const mwinWin32Xbox* xbox, uint32_t slot)
{
    return PadOf(xbox, slot) >= 0;
}

mwinResult mwinWin32XboxRumble(mwinWin32Xbox* xbox, uint32_t slot, float low, float high,
                               uint32_t durationMs, uint64_t nowNs)
{
    int32_t index = PadOf(xbox, slot);
    if (index < 0)
    {
        return mwin_errorPlatform;
    }
    mwinWin32XboxPad* pad = &xbox->pads[index];
    mwinWgiMotors motors = {0};
    if (durationMs > 0)
    {
        motors.low = (double)low;
        motors.high = (double)high;
    }
    if (!xbox->api.vibrate(xbox->api.self, pad->pad, &motors))
    {
        return mwin_errorPlatform;
    }
    pad->rumbleEndsNs = durationMs > 0 ? nowNs + (uint64_t)durationMs * 1000000u : 0;
    return mwin_success;
}
