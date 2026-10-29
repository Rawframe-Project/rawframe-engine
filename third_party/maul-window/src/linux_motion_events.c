// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Linux gamepad's motion device read from plain values.

#include "linux_motion_events.h"

#include <linux/input.h>
#include <string.h>

#define STANDARD_GRAVITY   9.80665f
#define RADIANS_PER_DEGREE 0.017453292f
#define VENDOR_NINTENDO    0x057E
// How far a stamped time may stray from the event time before it starts
// again from it.
#define STRAY_NS 100000000u

bool mwinLinuxSameController(const mwinLinuxPadIdentity* a, const mwinLinuxPadIdentity* b)
{
    if (a->uniq[0] != '\0' && b->uniq[0] != '\0')
    {
        return strncmp(a->uniq, b->uniq, sizeof(a->uniq)) == 0;
    }
    return a->uniq[0] == '\0' && b->uniq[0] == '\0' && a->phys[0] != '\0' &&
           strncmp(a->phys, b->phys, sizeof(a->phys)) == 0;
}

void mwinLinuxMotionStart(mwinLinuxMotionEvents* events, const mwinLinuxPadIdentity* pad)
{
    *events = (mwinLinuxMotionEvents){.nintendo = pad->vendor == VENDOR_NINTENDO};
}

bool mwinLinuxMotionAxis(mwinLinuxMotionEvents* events, unsigned axis, int32_t value,
                         int32_t resolution)
{
    if (axis >= MWIN_LINUX_MOTION_AXES || resolution <= 0)
    {
        return false;
    }
    float unit = axis < 3 ? STANDARD_GRAVITY : RADIANS_PER_DEGREE;
    events->scale[axis] = unit / (float)resolution;
    events->raw[axis] = value;
    return true;
}

// A report's time from its stamp: the first stamped report's event time,
// then each stamp's microseconds since the last, which wrap, so only
// their difference counts. Stamps that stray from the event time by more
// than STRAY_NS, as a device's clock drifts or its stamps jump, start
// again from the event time.
static void Stamp(mwinLinuxMotionEvents* events, uint64_t eventNs)
{
    uint64_t timeNs = eventNs;
    if (events->stamped)
    {
        timeNs = events->timeNs + (uint64_t)(uint32_t)(events->next - events->stamp) * 1000u;
        uint64_t stray = timeNs > eventNs ? timeNs - eventNs : eventNs - timeNs;
        timeNs = stray > STRAY_NS ? eventNs : timeNs;
    }
    events->timeNs = timeNs;
    events->stamp = events->next;
    events->stamped = true;
    events->stamping = false;
}

// The axes as one sample, in the contract's frame, as SDL turns them:
// Nintendo's axes reordered.
static void Sample(const mwinLinuxMotionEvents* events, uint64_t timeNs,
                   mwinLinuxMotionSample* sample)
{
    float values[MWIN_LINUX_MOTION_AXES];
    for (int i = 0; i < MWIN_LINUX_MOTION_AXES; i++)
    {
        values[i] = (float)events->raw[i] * events->scale[i];
    }
    *sample = (mwinLinuxMotionSample){
        .acceleration = {values[0], values[1], values[2]},
        .rate = {values[3], values[4], values[5]},
        .timeNs = timeNs,
    };
    if (events->nintendo)
    {
        sample->acceleration[0] = -values[1];
        sample->acceleration[1] = values[2];
        sample->acceleration[2] = -values[0];
        sample->rate[0] = -values[4];
        sample->rate[1] = values[5];
        sample->rate[2] = -values[3];
    }
}

mwinLinuxMotionStep mwinLinuxMotionEvent(mwinLinuxMotionEvents* events, uint16_t type,
                                         uint16_t code, int32_t value, uint64_t timeNs,
                                         mwinLinuxMotionSample* sample)
{
    mwinEvdevStep step = mwinEvdevDropStep(&events->dropping, type, code);
    if (step != mwin_evdevTake)
    {
        // A stamp in the cut report is not this report's.
        events->stamping = false;
        return step == mwin_evdevResync ? mwin_motionResync : mwin_motionNothing;
    }
    if (type == EV_ABS && code <= ABS_Z)
    {
        events->raw[code - ABS_X] = value;
    }
    else if (type == EV_ABS && code >= ABS_RX && code <= ABS_RZ)
    {
        events->raw[3 + code - ABS_RX] = value;
    }
    else if (type == EV_MSC && code == MSC_TIMESTAMP)
    {
        events->next = (uint32_t)value;
        events->stamping = true;
    }
    else if (type == EV_SYN && code == SYN_REPORT)
    {
        // A report without a stamp is timed by its event.
        if (events->stamping)
        {
            Stamp(events, timeNs);
            timeNs = events->timeNs;
        }
        Sample(events, timeNs, sample);
        return mwin_motionSample;
    }
    return mwin_motionNothing;
}
