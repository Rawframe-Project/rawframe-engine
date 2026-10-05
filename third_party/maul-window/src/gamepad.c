// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The gamepad component's functions: listing, facts, state and rumble.

#include "maul-window/gamepad.h"

#include "core.h"

#include <math.h>

mwinResult mwinGetGamepads(const mwinContext* context, mwinGamepadId* gamepads, size_t capacity,
                           size_t* countOut)
{
    if (context == nullptr || countOut == nullptr || (gamepads == nullptr && capacity != 0))
    {
        return mwinMisuse(context);
    }
    // In the order they came: each round takes the earliest after the last.
    size_t count = 0;
    uint64_t after = 0;
    bool first = true;
    for (;;)
    {
        int32_t next = -1;
        for (uint32_t i = 0; i < context->limits.gamepads; i++)
        {
            const mwinGamepad* gamepad = &context->gamepads[i];
            bool later = first || gamepad->arrival > after;
            if (gamepad->status == mwin_slotLive && later &&
                (next < 0 || gamepad->arrival < context->gamepads[next].arrival))
            {
                next = (int32_t)i;
            }
        }
        if (next < 0)
        {
            break;
        }
        if (count < capacity)
        {
            gamepads[count] = mwinGamepadIdOf(context, (uint32_t)next);
        }
        count += 1;
        after = context->gamepads[next].arrival;
        first = false;
    }
    *countOut = count;
    return count > capacity ? mwin_errorCapacity : mwin_success;
}

mwinResult mwinGetGamepadInfo(const mwinContext* context, mwinGamepadId gamepad,
                              mwinGamepadInfo* infoOut)
{
    if (context == nullptr || infoOut == nullptr)
    {
        return mwinMisuse(context);
    }
    int32_t slot = mwinFindGamepad(context, gamepad);
    if (slot < 0)
    {
        return mwin_errorStale;
    }
    *infoOut = context->gamepads[slot].info;
    return mwin_success;
}

mwinResult mwinGetGamepadState(const mwinContext* context, mwinGamepadId gamepad,
                               mwinGamepadState* stateOut)
{
    if (context == nullptr || stateOut == nullptr)
    {
        return mwinMisuse(context);
    }
    int32_t slot = mwinFindGamepad(context, gamepad);
    if (slot < 0)
    {
        return mwin_errorStale;
    }
    *stateOut = context->gamepads[slot].state;
    return mwin_success;
}

static bool IsStrength(float value)
{
    return isfinite(value) && value >= 0.0f && value <= 1.0f;
}

mwinResult mwinSetGamepadRumble(mwinContext* context, mwinGamepadId gamepad, float low, float high,
                                uint32_t durationMs)
{
    if (context == nullptr || !IsStrength(low) || !IsStrength(high))
    {
        return mwinMisuse(context);
    }
    int32_t slot = mwinFindGamepad(context, gamepad);
    if (slot < 0)
    {
        return mwin_errorStale;
    }
    if ((context->gamepads[slot].info.capabilities & mwin_padRumble) == 0)
    {
        return mwin_errorUnsupported;
    }
    return context->backend->rumble(context, (uint32_t)slot, low, high, durationMs);
}
