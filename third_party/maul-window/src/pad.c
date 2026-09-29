// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Gamepads in the core: slots the backend fills as the platform reports
// hotplug, their state, and their records. A removed gamepad's slot waits
// until its record is drained, so its id cannot come back while the
// program may still hold it.

#include "core.h"

#include "maul-unicode/encoding.h"

mwinGamepadId mwinGamepadIdOf(const mwinContext* context, uint32_t slot)
{
    return (mwinGamepadId){slot + 1, context->gamepads[slot].generation};
}

int32_t mwinFindGamepad(const mwinContext* context, mwinGamepadId gamepad)
{
    if (gamepad.index1 == 0 || gamepad.index1 > context->limits.gamepads)
    {
        return -1;
    }
    const mwinGamepad* found = &context->gamepads[gamepad.index1 - 1];
    return found->status == mwin_slotLive && found->generation == gamepad.generation
               ? (int32_t)(gamepad.index1 - 1)
               : -1;
}

// Keeps a gamepad's facts, with a name that is not UTF-8 left empty and
// raw counts kept to what the state holds.
static void Store(mwinGamepad* gamepad, const mwinGamepadInfo* info)
{
    gamepad->info = *info;
    if (info->nameLength > MWIN_GAMEPAD_NAME_BYTES ||
        muniValidateUtf8(info->name, info->nameLength).status != muni_success)
    {
        gamepad->info.nameLength = 0;
    }
    uint8_t buttons =
        info->rawButtons < MWIN_GAMEPAD_RAW_BUTTONS ? info->rawButtons : MWIN_GAMEPAD_RAW_BUTTONS;
    uint8_t axes = info->rawAxes < MWIN_GAMEPAD_RAW_AXES ? info->rawAxes : MWIN_GAMEPAD_RAW_AXES;
    gamepad->info.rawButtons = info->mapped ? 0 : buttons;
    gamepad->info.rawAxes = info->mapped ? 0 : axes;
}

static void PostGlobal(mwinContext* context, uint32_t slot, mwinEventType type, uint64_t timeNs)
{
    mwinEvent event = {.type = type, .timeNs = timeNs};
    event.data.gamepad = mwinGamepadIdOf(context, slot);
    mwinPostGlobal(context, &event);
}

int32_t mwinAddGamepad(mwinContext* context, const mwinGamepadInfo* info, uint64_t timeNs)
{
    for (uint32_t i = 0; i < context->limits.gamepads; i++)
    {
        mwinGamepad* gamepad = &context->gamepads[i];
        if (gamepad->status == mwin_slotFree)
        {
            gamepad->status = mwin_slotLive;
            gamepad->generation += 1;
            gamepad->arrival = context->arrivals++;
            gamepad->state = (mwinGamepadState){0};
            Store(gamepad, info);
            PostGlobal(context, i, mwin_eventGamepadAdded, timeNs);
            return (int32_t)i;
        }
    }
    return -1;
}

void mwinChangeGamepad(mwinContext* context, uint32_t slot, const mwinGamepadInfo* info,
                       uint64_t timeNs)
{
    Store(&context->gamepads[slot], info);
    PostGlobal(context, slot, mwin_eventGamepadChanged, timeNs);
}

void mwinRemoveGamepad(mwinContext* context, uint32_t slot, uint64_t timeNs)
{
    PostGlobal(context, slot, mwin_eventGamepadRemoved, timeNs);
    context->gamepads[slot].status = mwin_slotDestroyed;
}

void mwinReleaseGamepad(mwinContext* context, mwinGamepadId gamepad)
{
    mwinGamepad* slot = &context->gamepads[gamepad.index1 - 1];
    if (slot->status == mwin_slotDestroyed && slot->generation == gamepad.generation)
    {
        slot->status = mwin_slotFree;
    }
}

void mwinPostGamepadButton(mwinContext* context, uint32_t slot, uint8_t button, bool down,
                           uint64_t timeNs)
{
    mwinGamepad* gamepad = &context->gamepads[slot];
    uint8_t count = gamepad->info.mapped ? MWIN_GAMEPAD_BUTTONS : gamepad->info.rawButtons;
    uint32_t bit = 1u << (button & 31u);
    if (button >= count || ((gamepad->state.buttons & bit) != 0) == down)
    {
        return;
    }
    gamepad->state.buttons = down ? gamepad->state.buttons | bit : gamepad->state.buttons & ~bit;
    mwinEvent event = {.type = down ? mwin_eventGamepadButtonDown : mwin_eventGamepadButtonUp,
                       .timeNs = timeNs};
    event.data.gamepadButton =
        (mwinGamepadButtonEvent){mwinGamepadIdOf(context, slot), button, !gamepad->info.mapped};
    mwinPostGamepadRecord(context, &event);
}

void mwinPostGamepadAxis(mwinContext* context, uint32_t slot, uint8_t axis, float value,
                         uint64_t timeNs)
{
    mwinGamepad* gamepad = &context->gamepads[slot];
    uint8_t count = gamepad->info.mapped ? MWIN_GAMEPAD_AXES : gamepad->info.rawAxes;
    if (axis >= count || gamepad->state.axes[axis] == value)
    {
        return;
    }
    gamepad->state.axes[axis] = value;
    mwinEvent event = {.type = mwin_eventGamepadAxisMoved, .timeNs = timeNs};
    event.data.gamepadAxis =
        (mwinGamepadAxisEvent){mwinGamepadIdOf(context, slot), axis, !gamepad->info.mapped, value};
    mwinPostGamepadRecord(context, &event);
}
