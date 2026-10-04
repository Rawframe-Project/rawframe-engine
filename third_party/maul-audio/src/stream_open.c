// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Opening a stream of one direction: the device it starts on, the
// format the backend settles, its period, and the backend's attach.

#include "stream_open.h"

#include "backend.h"
#include "clock.h"
#include "context.h"
#include "follow.h"
#include "period.h"
#include "voice.h"

#include <stdckdint.h>

// The device a new stream will start on: its requested device, which
// must be live and of its direction, or the default it follows, which
// may be none.
static maudResult FindStartingDevice(const maudContext* context, const maudStreamDef* def,
                                     const maudDeviceInfo** deviceOut)
{
    maudDeviceId id = def->device;
    if (id.index1 == 0)
    {
        id = context->devices.defaults[def->direction][def->role];
        if (id.index1 == 0)
        {
            *deviceOut = nullptr;
            return maud_success;
        }
    }
    const maudDeviceSlot* slot = maudFindDevice(context, id);
    if (slot == nullptr)
    {
        return maud_errorStale;
    }
    if (slot->info.direction != def->direction)
    {
        return maud_errorInvalid;
    }
    *deviceOut = &slot->info;
    return maud_success;
}

// Opens the stream's format through the backend and allocates its
// period. The slot is untouched on failure.
static maudResult OpenCore(maudContext* context, const maudStreamDef* def,
                           const maudDeviceInfo* device, uint32_t duplexGroup, maudStreamSlot* slot)
{
    if (def->share == maud_shareExclusive &&
        (!context->backend->exclusive || def->ratePolicy == maud_ratePlatformConverted))
    {
        return maud_errorUnsupported;
    }
    maudStreamFormat format;
    maudResult result = context->backend->openStream(context, def, device, &format);
    if (result != maud_success)
    {
        return result;
    }
    if (format.periodFrames == 0 || format.periodFrames > context->def.limits.periodFrames)
    {
        return maud_errorCapacity;
    }
    size_t bytes;
    if (ckd_mul(&bytes, (size_t)format.periodFrames,
                (size_t)maudGetLayoutChannelCount(format.layout)) ||
        ckd_mul(&bytes, bytes, sizeof(float)))
    {
        return maud_errorCapacity;
    }
    float* samples = maudContextAllocate(context, bytes, alignof(float));
    if (samples == nullptr)
    {
        return maud_errorCapacity;
    }
    maudStreamCore* core = &slot->core;
    core->def = *def;
    core->format = format;
    core->sampleBytes = bytes;
    core->duplexGroup = duplexGroup;
    core->exclusive = def->share == maud_shareExclusive;
    maudResetVoice(core);
    atomic_store_explicit(&core->underruns, 0, memory_order_relaxed);
    atomic_store_explicit(&core->overruns, 0, memory_order_relaxed);
    if (context->backend->hasNoVoice && def->direction == maud_directionInput)
    {
        maudReportVoice(core, maud_voiceNone);
    }
    maudInitPeriod(&core->period, def, &format, samples);
    atomic_store_explicit(&core->blockRate, format.sampleRate, memory_order_relaxed);
    atomic_store_explicit(&core->position, 0, memory_order_relaxed);
    maudResetClock(core);
    maudBindNewStream(context, slot);
    return maud_success;
}

maudResult maudOpenStream(maudContext* context, const maudStreamDef* def, uint32_t duplexGroup,
                          maudStreamSlot** slotOut)
{
    const maudDeviceInfo* device = nullptr;
    maudResult result = FindStartingDevice(context, def, &device);
    if (result != maud_success)
    {
        return result;
    }
    maudStreamSlot* slot = maudFindFreeStreamSlot(context);
    if (slot == nullptr)
    {
        return maud_errorCapacity;
    }
    result = OpenCore(context, def, device, duplexGroup, slot);
    if (result != maud_success)
    {
        return result;
    }
    if (context->backend->attachStream != nullptr)
    {
        result = context->backend->attachStream(context, slot);
        if (result != maud_success)
        {
            maudContextRelease(context, slot->core.period.samples, slot->core.sampleBytes,
                               alignof(float));
            return result;
        }
    }
    slot->live = true;
    *slotOut = slot;
    return maud_success;
}
