// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Web gamepads through the Gamepad API.

#include "web_pad.h"

#include "web_js.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

// A pad as the page read it: its timestamp, the disconnections counted
// at its index, its buttons held as bits and how many buttons and axes
// it has (at most 32 and 16), whether its mapping is the standard one,
// its triggers' values (the standard mapping's buttons 6 and 7) and its
// axes.
typedef struct PadState
{
    double timestamp;
    uint32_t losses;
    uint32_t pressed;
    uint32_t buttons;
    uint32_t axes;
    uint32_t standard;
    float triggers[2];
    float values[MWIN_GAMEPAD_RAW_AXES];
} PadState;

static_assert(offsetof(PadState, losses) == 8 && offsetof(PadState, triggers) == 28 &&
                  offsetof(PadState, values) == 36,
              "the page writes these offsets");
static_assert(MWIN_GAMEPAD_RAW_BUTTONS == 32 && MWIN_GAMEPAD_RAW_AXES == 16,
              "the page reads as many");

// The standard mapping's button for each mwinGamepadButton.
static const uint8_t s_standard[MWIN_GAMEPAD_BUTTONS] = {
    12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 10, 11, 9, 8, 16,
};

EM_JS_DEPS(mwin_web_pad, "$stringToUTF8");

// clang-format off
EM_JS(void, mwinPageAttachPads, (const mwinContext* context), {
    const state = Module.mwinWeb.get(context);
    state.padLosses = [];
    const lost = event => {
        const index = event.gamepad.index;
        state.padLosses[index] = (state.padLosses[index] || 0) + 1;
    };
    window.addEventListener('gamepaddisconnected', lost);
    state.listeners.push(() => window.removeEventListener('gamepaddisconnected', lost));
});

EM_JS(bool, mwinPageReadPad, (const mwinContext* context, uint32_t index, PadState* out), {
    const pads = navigator.getGamepads ? navigator.getGamepads() : [];
    const pad = pads[index];
    if (!pad || !pad.connected) {
        return false;
    }
    const buttons = Math.min(pad.buttons.length, 32);
    let pressed = 0;
    for (let i = 0; i < buttons; i++) {
        pressed |= pad.buttons[i].pressed ? 1 << i : 0;
    }
    const axes = Math.min(pad.axes.length, 16);
    const value = i => (pad.buttons[i] ? pad.buttons[i].value : 0);
    const at = out >> 2;
    HEAPF64[out >> 3] = pad.timestamp;
    HEAPU32.set([Module.mwinWeb.get(context).padLosses[index] || 0, pressed >>> 0, buttons, axes,
                 pad.mapping === 'standard' ? 1 : 0], at + 2);
    HEAPF32.set([value(6), value(7)], at + 7);
    HEAPF32.set(pad.axes.slice(0, axes), at + 9);
    return true;
});

// A pad's name, and its vendor and product in the high and low halves
// of ids[0] where its id tells them: Chrome's "Name (... Vendor: 045e
// Product: 028e)", Firefox's and Safari's "45e-28e-Name". ids[1] has
// bit 0 when it has a dual-rumble actuator, bit 1 when the actuator
// has trigger-rumble.
EM_JS(uint32_t, mwinWebPadFacts, (uint32_t index, char* name, uint32_t capacity, uint32_t* ids), {
    const pad = navigator.getGamepads()[index];
    let text = pad.id;
    let vendor = 0;
    let product = 0;
    const chrome = /^(.*?) *[(]([^()]*)[)]$/.exec(text);
    const numbers = chrome && /Vendor: ([0-9a-f]{4}) Product: ([0-9a-f]{4})/i.exec(chrome[2]);
    const others = /^([0-9a-f]{1,4})-([0-9a-f]{1,4})-(.+)$/i.exec(text);
    if (numbers || (chrome && /GAMEPAD/.test(chrome[2]))) {
        text = chrome[1];
    }
    if (numbers) {
        vendor = parseInt(numbers[1], 16);
        product = parseInt(numbers[2], 16);
    } else if (others) {
        vendor = parseInt(others[1], 16);
        product = parseInt(others[2], 16);
        text = others[3];
    }
    const actuator = pad.vibrationActuator;
    const rumble = !!actuator && (!actuator.effects || actuator.effects.includes('dual-rumble'));
    const triggers = !!actuator && !!actuator.effects &&
        actuator.effects.includes('trigger-rumble');
    HEAPU32[ids >> 2] = ((vendor << 16) | product) >>> 0;
    HEAPU32[(ids >> 2) + 1] = (rumble ? 1 : 0) | (triggers ? 2 : 0);
    return stringToUTF8(text, name, capacity);
});

// Plays the four motors (heavy, light, left, right) as one effect, the
// trigger-rumble one where triggers is set, else dual-rumble, or stops
// them for a duration of 0; the effect's promise, settled when it ends
// or gives way, is let go.
EM_JS(bool, mwinWebPadRumble, (uint32_t index, const float* motors, bool triggers,
                               uint32_t durationMs), {
    const pads = navigator.getGamepads ? navigator.getGamepads() : [];
    const actuator = pads[index] && pads[index].vibrationActuator;
    if (!actuator) {
        return false;
    }
    const at = motors >> 2;
    const params = {duration: durationMs, strongMagnitude: HEAPF32[at],
                    weakMagnitude: HEAPF32[at + 1]};
    if (triggers) {
        params.leftTrigger = HEAPF32[at + 2];
        params.rightTrigger = HEAPF32[at + 3];
    }
    const done = durationMs === 0 && actuator.reset ? actuator.reset() :
        actuator.playEffect(triggers ? 'trigger-rumble' : 'dual-rumble', params);
    Promise.resolve(done).catch(() => {});
    return true;
});
// clang-format on

void mwinWebPadsStart(mwinWebPads* pads, mwinContext* context)
{
    *pads = (mwinWebPads){.context = context};
    mwinPageAttachPads(context);
}

// A value in its range; NaN, which a page may report, as 0.
static float Clamp(float value, float low)
{
    return isnan(value) ? 0.0f : fminf(fmaxf(value, low), 1.0f);
}

static void Post(mwinWebPads* pads, const mwinWebPad* pad, const PadState* state, uint64_t timeNs)
{
    mwinContext* context = pads->context;
    if (state->standard == 0)
    {
        for (uint8_t i = 0; i < state->buttons; i++)
        {
            mwinPostGamepadButton(context, pad->slot, i, (state->pressed >> i & 1u) != 0, timeNs);
        }
        for (uint8_t i = 0; i < state->axes; i++)
        {
            mwinPostGamepadAxis(context, pad->slot, i, Clamp(state->values[i], -1.0f), timeNs);
        }
        return;
    }
    for (uint8_t i = 0; i < MWIN_GAMEPAD_BUTTONS; i++)
    {
        bool down = s_standard[i] < state->buttons && (state->pressed >> s_standard[i] & 1u) != 0;
        mwinPostGamepadButton(context, pad->slot, i, down, timeNs);
    }
    // The standard mapping's sticks already have y down positive.
    for (uint8_t i = 0; i < 4; i++)
    {
        float value = i < state->axes ? state->values[i] : 0.0f;
        mwinPostGamepadAxis(context, pad->slot, i, Clamp(value, -1.0f), timeNs);
    }
    mwinPostGamepadAxis(context, pad->slot, mwin_padTriggerLeft, Clamp(state->triggers[0], 0.0f),
                        timeNs);
    mwinPostGamepadAxis(context, pad->slot, mwin_padTriggerRight, Clamp(state->triggers[1], 0.0f),
                        timeNs);
}

// Tells the core of a pad found at an index; false when it has no room.
static bool Found(mwinWebPads* pads, uint32_t index, const PadState* state, uint64_t nowNs)
{
    char name[MWIN_GAMEPAD_NAME_BYTES + 1];
    uint32_t ids[2] = {0, 0};
    uint32_t length = mwinWebPadFacts(index, name, sizeof(name), ids);
    mwinGamepadInfo info = {
        .vendor = (uint16_t)(ids[0] >> 16),
        .product = (uint16_t)ids[0],
        .mapped = state->standard != 0,
        .rawButtons = state->standard != 0 ? 0 : (uint8_t)state->buttons,
        .rawAxes = state->standard != 0 ? 0 : (uint8_t)state->axes,
        .capabilities = (mwinGamepadCapabilities)(((ids[1] & 1u) != 0 ? mwin_padRumble : 0) |
                                                  ((ids[1] & 2u) != 0 ? mwin_padTriggerRumble : 0)),
        .battery = -1,
    };
    info.nameLength = length < MWIN_GAMEPAD_NAME_BYTES ? length : MWIN_GAMEPAD_NAME_BYTES;
    memcpy(info.name, name, info.nameLength);
    int32_t slot = mwinAddGamepad(pads->context, &info, nowNs);
    if (slot < 0)
    {
        return false;
    }
    // A timestamp no read gives, so its controls are posted now.
    pads->pads[index] = (mwinWebPad){true, (uint32_t)slot, state->losses, -1.0, {0}, {0}};
    return true;
}

// The index of the pad in a core slot, or -1.
static int32_t IndexOf(const mwinWebPads* pads, uint32_t slot)
{
    for (uint32_t index = 0; index < MWIN_WEB_PADS; index++)
    {
        if (pads->pads[index].connected && pads->pads[index].slot == slot)
        {
            return (int32_t)index;
        }
    }
    return -1;
}

// Runs a pair of a pad's motors, the grips' (0) or the triggers' (1),
// for a time, and plays all four for as long as the longer pair runs.
static mwinResult Run(mwinWebPads* pads, uint32_t index, uint32_t pair, float a, float b,
                      uint32_t durationMs, uint64_t nowNs)
{
    mwinWebPad* pad = &pads->pads[index];
    pad->motors[pair * 2] = durationMs > 0 ? a : 0.0f;
    pad->motors[pair * 2 + 1] = durationMs > 0 ? b : 0.0f;
    pad->endsNs[pair] = durationMs > 0 ? nowNs + (uint64_t)durationMs * 1000000u : 0;
    uint64_t endsNs = pad->endsNs[0] > pad->endsNs[1] ? pad->endsNs[0] : pad->endsNs[1];
    uint32_t remainingMs = endsNs > nowNs ? (uint32_t)((endsNs - nowNs + 999999u) / 1000000u) : 0;
    bool triggers =
        (pads->context->gamepads[pad->slot].info.capabilities & mwin_padTriggerRumble) != 0;
    return mwinWebPadRumble(index, pad->motors, triggers, remainingMs) ? mwin_success
                                                                       : mwin_errorPlatform;
}

void mwinWebPadsPump(mwinWebPads* pads, uint64_t nowNs)
{
    for (uint32_t index = 0; index < MWIN_WEB_PADS; index++)
    {
        PadState state;
        bool present = mwinPageReadPad(pads->context, index, &state);
        mwinWebPad* pad = &pads->pads[index];
        // Gone, or disconnected since and another there.
        if (pad->connected && (!present || state.losses != pad->losses))
        {
            mwinRemoveGamepad(pads->context, pad->slot, nowNs);
            pad->connected = false;
        }
        if (present && !pad->connected && !Found(pads, index, &state, nowNs))
        {
            continue;
        }
        if (present && state.timestamp != pad->timestamp)
        {
            pad->timestamp = state.timestamp;
            Post(pads, pad, &state, nowNs);
        }
        for (uint32_t pair = 0; pad->connected && pair < 2; pair++)
        {
            if (pad->endsNs[pair] != 0 && nowNs >= pad->endsNs[pair])
            {
                (void)Run(pads, index, pair, 0.0f, 0.0f, 0, nowNs);
            }
        }
    }
}

mwinResult mwinWebPadsRumble(mwinWebPads* pads, uint32_t slot, float low, float high,
                             uint32_t durationMs, uint64_t nowNs)
{
    int32_t index = IndexOf(pads, slot);
    return index < 0 ? mwin_errorPlatform
                     : Run(pads, (uint32_t)index, 0, low, high, durationMs, nowNs);
}

mwinResult mwinWebPadsTriggerRumble(mwinWebPads* pads, uint32_t slot, float left, float right,
                                    uint32_t durationMs, uint64_t nowNs)
{
    int32_t index = IndexOf(pads, slot);
    return index < 0 ? mwin_errorPlatform
                     : Run(pads, (uint32_t)index, 1, left, right, durationMs, nowNs);
}
