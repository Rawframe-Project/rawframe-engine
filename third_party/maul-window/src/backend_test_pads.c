// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The test platform's gamepads: connected, changed and removed at once,
// their controls and motion posted as a platform reports them, and what
// the program asked of their motors and sensors read back.

#include "backend_test.h"
#include "core.h"

#include "maul-window/test.h"

// The test platform's clock.
static uint64_t Now(const mwinContext* context)
{
    return mwinTestPlatformOf(context)->timeNs;
}

// The slot of a gamepad of a test context, or why there is none.
static mwinResult FindTestGamepad(const mwinContext* context, mwinGamepadId gamepad,
                                  int32_t* slotOut)
{
    if (context == nullptr)
    {
        return mwinMisuse(context);
    }
    if (mwinTestPlatformOf(context) == nullptr)
    {
        return mwin_errorUnsupported;
    }
    *slotOut = mwinFindGamepad(context, gamepad);
    return *slotOut < 0 ? mwin_errorStale : mwin_success;
}

mwinResult mwinAddTestGamepad(mwinContext* context, const mwinGamepadInfo* info,
                              mwinGamepadId* gamepadOut)
{
    mwinClearOut(gamepadOut, sizeof *gamepadOut);
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    if (context == nullptr || info == nullptr || gamepadOut == nullptr)
    {
        return mwinMisuse(context);
    }
    if (mwinTestPlatformOf(context) == nullptr)
    {
        return mwin_errorUnsupported;
    }
    int32_t slot = mwinAddGamepad(context, info, Now(context));
    if (slot < 0)
    {
        return mwin_errorCapacity;
    }
    mwinTestPlatformOf(context)->pads[slot] = (mwinTestPad){0};
    *gamepadOut = mwinGamepadIdOf(context, (uint32_t)slot);
    return mwin_success;
}

mwinResult mwinGamepad_SetTestInfo(mwinContext* context, mwinGamepadId gamepad,
                                   const mwinGamepadInfo* info)
{
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    int32_t slot = -1;
    mwinResult status =
        info != nullptr ? FindTestGamepad(context, gamepad, &slot) : mwinMisuse(context);
    if (status == mwin_success)
    {
        mwinChangeGamepad(context, (uint32_t)slot, info, Now(context));
    }
    return status;
}

mwinResult mwinRemoveTestGamepad(mwinContext* context, mwinGamepadId gamepad)
{
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    int32_t slot = -1;
    mwinResult status = FindTestGamepad(context, gamepad, &slot);
    if (status == mwin_success)
    {
        mwinRemoveGamepad(context, (uint32_t)slot, Now(context));
    }
    return status;
}

mwinResult mwinGamepad_SetTestButton(mwinContext* context, mwinGamepadId gamepad, uint8_t button,
                                     bool down)
{
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    int32_t slot = -1;
    mwinResult status = FindTestGamepad(context, gamepad, &slot);
    if (status == mwin_success)
    {
        mwinPostGamepadButton(context, (uint32_t)slot, button, down, Now(context));
    }
    return status;
}

mwinResult mwinGamepad_SetTestAxis(mwinContext* context, mwinGamepadId gamepad, uint8_t axis,
                                   float value)
{
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    int32_t slot = -1;
    mwinResult status = FindTestGamepad(context, gamepad, &slot);
    if (status == mwin_success)
    {
        mwinPostGamepadAxis(context, (uint32_t)slot, axis, value, Now(context));
    }
    return status;
}

// Reads one of a test gamepad's rumbles: its motors' or its triggers'.
static mwinResult GetRumble(const mwinContext* context, mwinGamepadId gamepad, bool triggers,
                            float* lowOut, float* highOut, uint32_t* durationMsOut,
                            uint32_t* countOut)
{
    mwinClearOut(lowOut, sizeof *lowOut);
    mwinClearOut(highOut, sizeof *highOut);
    mwinClearOut(durationMsOut, sizeof *durationMsOut);
    mwinClearOut(countOut, sizeof *countOut);
    if (lowOut == nullptr || highOut == nullptr || durationMsOut == nullptr || countOut == nullptr)
    {
        return mwinMisuse(context);
    }
    int32_t slot = -1;
    mwinResult status = FindTestGamepad(context, gamepad, &slot);
    if (status == mwin_success)
    {
        const mwinTestPad* pad = &mwinTestPlatformOf(context)->pads[slot];
        const mwinTestRumble* rumble = triggers ? &pad->triggers : &pad->rumble;
        *lowOut = rumble->low;
        *highOut = rumble->high;
        *durationMsOut = rumble->durationMs;
        *countOut = rumble->count;
    }
    return status;
}

mwinResult mwinGamepad_GetTestRumble(const mwinContext* context, mwinGamepadId gamepad,
                                     float* lowOut, float* highOut, uint32_t* durationMsOut,
                                     uint32_t* countOut)
{
    return GetRumble(context, gamepad, false, lowOut, highOut, durationMsOut, countOut);
}

mwinResult mwinGamepad_GetTestTriggerRumble(const mwinContext* context, mwinGamepadId gamepad,
                                            float* leftOut, float* rightOut,
                                            uint32_t* durationMsOut, uint32_t* countOut)
{
    return GetRumble(context, gamepad, true, leftOut, rightOut, durationMsOut, countOut);
}

mwinResult mwinGamepad_IsTestMotionEnabled(const mwinContext* context, mwinGamepadId gamepad,
                                           bool* onOut)
{
    mwinClearOut(onOut, sizeof *onOut);
    if (onOut == nullptr)
    {
        return mwinMisuse(context);
    }
    int32_t slot = -1;
    mwinResult status = FindTestGamepad(context, gamepad, &slot);
    if (status == mwin_success)
    {
        *onOut = mwinTestPlatformOf(context)->pads[slot].motion;
    }
    return status;
}

mwinResult mwinGamepad_PostTestMotion(mwinContext* context, mwinGamepadId gamepad,
                                      const float acceleration[3], const float rotationRate[3],
                                      uint64_t timeNs)
{
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    int32_t slot = -1;
    mwinResult status = acceleration != nullptr && rotationRate != nullptr
                            ? FindTestGamepad(context, gamepad, &slot)
                            : mwinMisuse(context);
    if (status == mwin_success)
    {
        mwinPostGamepadMotion(context, (uint32_t)slot, acceleration, rotationRate, timeNs);
    }
    return status;
}
