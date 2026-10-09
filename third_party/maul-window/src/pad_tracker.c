// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Gamepads a platform runtime lists and reads.

#include "pad_tracker.h"

#include <math.h>

#define SEARCH_NS 500000000u

void mwinPadTrackerStart(mwinPadTracker* tracker, mwinContext* context,
                         const mwinPadRuntime* runtime)
{
    *tracker = (mwinPadTracker){.context = context, .runtime = *runtime};
}

// Stops a pair of a pad's motors, the grips' (0) or the triggers' (1).
static void Still(mwinPadTracker* tracker, mwinTrackedPad* pad, int pair)
{
    pad->motors[pair * 2] = 0.0f;
    pad->motors[pair * 2 + 1] = 0.0f;
    pad->endsNs[pair] = 0;
    (void)tracker->runtime.vibrate(tracker->runtime.self, pad->pad, pad->motors);
}

void mwinPadTrackerStop(mwinPadTracker* tracker)
{
    for (uint32_t i = 0; i < tracker->count; i++)
    {
        mwinTrackedPad* pad = &tracker->pads[i];
        if (pad->endsNs[0] != 0 || pad->endsNs[1] != 0)
        {
            pad->endsNs[1] = 0;
            pad->motors[2] = 0.0f;
            pad->motors[3] = 0.0f;
            Still(tracker, pad, 0);
        }
        tracker->runtime.release(tracker->runtime.self, tracker->pads[i].pad);
    }
    *tracker = (mwinPadTracker){0};
}

// A value within its range; a value that is not a number is 0.
static float Clamp(float value, float low)
{
    return isnan(value) ? 0.0f : value < low ? low : value > 1.0f ? 1.0f : value;
}

// Posts a pad's reading, its values kept in their ranges.
static void Post(mwinPadTracker* tracker, const mwinTrackedPad* pad, const mwinPadReading* reading,
                 uint64_t timeNs)
{
    for (uint8_t i = 0; i < MWIN_GAMEPAD_BUTTONS; i++)
    {
        bool down = (reading->buttons & (1u << i)) != 0;
        mwinPostGamepadButton(tracker->context, pad->slot, i, down, timeNs);
    }
    for (uint8_t i = 0; i < MWIN_GAMEPAD_AXES; i++)
    {
        bool trigger = i == mwin_padTriggerLeft || i == mwin_padTriggerRight;
        mwinPostGamepadAxis(tracker->context, pad->slot, i,
                            Clamp(reading->axes[i], trigger ? 0.0f : -1.0f), timeNs);
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
static void Forget(mwinPadTracker* tracker, void* const* listed, int32_t count, uint64_t nowNs)
{
    uint32_t kept = 0;
    for (uint32_t i = 0; i < tracker->count; i++)
    {
        mwinTrackedPad* pad = &tracker->pads[i];
        if (IndexOf(listed, count, pad->pad) >= 0)
        {
            tracker->pads[kept++] = *pad;
            continue;
        }
        mwinRemoveGamepad(tracker->context, pad->slot, nowNs);
        tracker->runtime.release(tracker->runtime.self, pad->pad);
    }
    tracker->count = kept;
}

static bool Tracked(const mwinPadTracker* tracker, const void* pad)
{
    for (uint32_t i = 0; i < tracker->count; i++)
    {
        if (tracker->pads[i].pad == pad)
        {
            return true;
        }
    }
    return false;
}

// Adds a pad newly listed, keeping its reference; lets it go when the
// core has no slot for it.
static void Add(mwinPadTracker* tracker, void* listed, uint64_t nowNs)
{
    mwinGamepadInfo info = {.mapped = true};
    tracker->runtime.describe(tracker->runtime.self, listed, &info);
    info.battery = tracker->runtime.battery(tracker->runtime.self, listed);
    int32_t slot = tracker->count < MWIN_PAD_TRACKER_PADS
                       ? mwinAddGamepad(tracker->context, &info, nowNs)
                       : -1;
    if (slot < 0)
    {
        tracker->runtime.release(tracker->runtime.self, listed);
        return;
    }
    // Its first reading is posted whatever its time.
    tracker->pads[tracker->count++] =
        (mwinTrackedPad){.pad = listed, .slot = (uint32_t)slot, .timestamp = UINT64_MAX};
}

// A pad's battery, told when it changes.
static void Recharge(mwinPadTracker* tracker, const mwinTrackedPad* pad, uint64_t nowNs)
{
    int8_t battery = tracker->runtime.battery(tracker->runtime.self, pad->pad);
    mwinGamepadInfo info = tracker->context->gamepads[pad->slot].info;
    if (battery != info.battery)
    {
        info.battery = battery;
        mwinChangeGamepad(tracker->context, pad->slot, &info, nowNs);
    }
}

// Looks for pads: those gone are removed, those new added, the rest's
// batteries read.
static void Search(mwinPadTracker* tracker, uint64_t nowNs)
{
    void* listed[MWIN_PAD_TRACKER_PADS];
    int32_t count = tracker->runtime.list(tracker->runtime.self, listed, MWIN_PAD_TRACKER_PADS);
    if (count < 0)
    {
        return;
    }
    Forget(tracker, listed, count, nowNs);
    uint32_t known = tracker->count;
    for (int32_t i = 0; i < count; i++)
    {
        if (Tracked(tracker, listed[i]))
        {
            tracker->runtime.release(tracker->runtime.self, listed[i]);
        }
        else
        {
            Add(tracker, listed[i], nowNs);
        }
    }
    for (uint32_t i = 0; i < known; i++)
    {
        Recharge(tracker, &tracker->pads[i], nowNs);
    }
}

void mwinPadTrackerPump(mwinPadTracker* tracker, uint64_t nowNs)
{
    // The runtime's word is taken first, so a pad it names is found now.
    bool told = tracker->runtime.changed(tracker->runtime.self);
    if (told || tracker->searchedNs == 0 || nowNs - tracker->searchedNs >= SEARCH_NS)
    {
        tracker->searchedNs = nowNs;
        Search(tracker, nowNs);
    }
    for (uint32_t i = 0; i < tracker->count; i++)
    {
        mwinTrackedPad* pad = &tracker->pads[i];
        mwinPadReading reading;
        if (tracker->runtime.read(tracker->runtime.self, pad->pad, &reading) &&
            reading.timestamp != pad->timestamp)
        {
            pad->timestamp = reading.timestamp;
            Post(tracker, pad, &reading, nowNs);
        }
        for (int pair = 0; pair < 2; pair++)
        {
            if (pad->endsNs[pair] != 0 && nowNs >= pad->endsNs[pair])
            {
                Still(tracker, pad, pair);
            }
        }
    }
}

// The index of the pad in a core slot, or -1.
static int32_t PadOf(const mwinPadTracker* tracker, uint32_t slot)
{
    for (uint32_t i = 0; i < tracker->count; i++)
    {
        if (tracker->pads[i].slot == slot)
        {
            return (int32_t)i;
        }
    }
    return -1;
}

bool mwinPadTrackerOwns(const mwinPadTracker* tracker, uint32_t slot)
{
    return PadOf(tracker, slot) >= 0;
}

// Runs a pair of a pad's motors for a time, the other pair as it runs.
static mwinResult Run(mwinPadTracker* tracker, uint32_t slot, int pair, float a, float b,
                      uint32_t durationMs, uint64_t nowNs)
{
    int32_t index = PadOf(tracker, slot);
    if (index < 0)
    {
        return mwin_errorPlatform;
    }
    mwinTrackedPad* pad = &tracker->pads[index];
    bool running = durationMs > 0;
    float motors[4] = {pad->motors[0], pad->motors[1], pad->motors[2], pad->motors[3]};
    motors[pair * 2] = running ? a : 0.0f;
    motors[pair * 2 + 1] = running ? b : 0.0f;
    if (!tracker->runtime.vibrate(tracker->runtime.self, pad->pad, motors))
    {
        return mwin_errorPlatform;
    }
    for (int i = 0; i < 4; i++)
    {
        pad->motors[i] = motors[i];
    }
    pad->endsNs[pair] = running ? nowNs + (uint64_t)durationMs * 1000000u : 0;
    return mwin_success;
}

mwinResult mwinPadTrackerRumble(mwinPadTracker* tracker, uint32_t slot, float low, float high,
                                uint32_t durationMs, uint64_t nowNs)
{
    return Run(tracker, slot, 0, low, high, durationMs, nowNs);
}

mwinResult mwinPadTrackerTriggerRumble(mwinPadTracker* tracker, uint32_t slot, float left,
                                       float right, uint32_t durationMs, uint64_t nowNs)
{
    return Run(tracker, slot, 1, left, right, durationMs, nowNs);
}
