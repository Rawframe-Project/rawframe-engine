// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The fixed-period adapter. Output periods are produced on demand and
// handed out across as many calls as the device side needs; input is
// gathered until a period is complete. Nothing here allocates.

#include "period.h"

#include <string.h>

void maudInitPeriod(maudPeriod* period, const maudStreamDef* def, const maudStreamFormat* format,
                    float* samples)
{
    *period = (maudPeriod){
        .callback = def->callback,
        .user = def->user,
        .samples = samples,
        .channelCount = maudGetLayoutChannelCount(format->layout),
        .frames = format->periodFrames,
        .sampleRate = format->sampleRate,
        .layout = format->layout,
        .cursor = def->direction == maud_directionOutput ? format->periodFrames : 0,
        .nextBlock = 0,
    };
}

void maudInitObjects(maudPeriod* period, maudStreamObject* objects, float* samples, uint32_t count)
{
    period->objects = objects;
    period->objectSamples = samples;
    period->objectCount = count;
    period->objectsAvailable = count;
    for (uint32_t i = 0; i < count; ++i)
    {
        objects[i] =
            (maudStreamObject){.samples = samples + (size_t)i * period->frames, .gain = 1.0f};
    }
}

static void CallBlock(maudPeriod* period, bool output)
{
    maudStreamBlock block = {
        .output = output ? period->samples : nullptr,
        .input = output ? nullptr : period->samples,
        .frameCount = period->frames,
        .sampleRate = period->sampleRate,
        .layout = period->layout,
        .position = period->nextBlock,
        .objects = period->objectCount > 0 ? period->objects : nullptr,
        .objectCount = period->objectCount,
        .objectsAvailable = period->objectsAvailable,
    };
    period->callback(&block, period->user);
    period->nextBlock += period->frames;
}

// Starts the next output period: silence, each object's frames where
// they belong (the callback may have moved its pointer), then the
// callback.
static void NextOutput(maudPeriod* period)
{
    size_t frames = period->frames;
    memset(period->samples, 0, frames * period->channelCount * sizeof(float));
    if (period->objectCount > 0)
    {
        memset(period->objectSamples, 0, frames * period->objectCount * sizeof(float));
        for (uint32_t i = 0; i < period->objectCount; ++i)
        {
            period->objects[i].samples = period->objectSamples + i * frames;
        }
    }
    CallBlock(period, true);
    period->cursor = 0;
}

void maudPullObjects(maudPeriod* period, float* bed, maudStreamObject* objectsOut, uint32_t frames)
{
    size_t channels = period->channelCount;
    size_t done = 0;
    while (done < frames)
    {
        if (period->cursor == period->frames)
        {
            NextOutput(period);
        }
        uint32_t take = period->frames - period->cursor;
        take = take < frames - done ? take : (uint32_t)(frames - done);
        memcpy(bed + done * channels, period->samples + (size_t)period->cursor * channels,
               (size_t)take * channels * sizeof(float));
        for (uint32_t i = 0; i < period->objectCount; ++i)
        {
            const float* from = period->objectSamples + (size_t)i * period->frames + period->cursor;
            memcpy(objectsOut[i].samples + done, from, (size_t)take * sizeof(float));
        }
        period->cursor += take;
        done += take;
    }
    for (uint32_t i = 0; i < period->objectCount; ++i)
    {
        const maudStreamObject* object = &period->objects[i];
        memcpy(objectsOut[i].position, object->position, sizeof(object->position));
        objectsOut[i].gain = object->gain;
        objectsOut[i].active = object->active;
    }
}

void maudPullPeriod(maudPeriod* period, float* out, uint32_t frames)
{
    size_t channels = period->channelCount;
    while (frames > 0)
    {
        if (period->cursor == period->frames)
        {
            NextOutput(period);
        }
        uint32_t take = period->frames - period->cursor;
        take = take < frames ? take : frames;
        memcpy(out, period->samples + (size_t)period->cursor * channels,
               (size_t)take * channels * sizeof(float));
        out += (size_t)take * channels;
        period->cursor += take;
        frames -= take;
    }
}

void maudPushPeriod(maudPeriod* period, const float* in, uint32_t frames)
{
    size_t channels = period->channelCount;
    while (frames > 0)
    {
        uint32_t take = period->frames - period->cursor;
        take = take < frames ? take : frames;
        memcpy(period->samples + (size_t)period->cursor * channels, in,
               (size_t)take * channels * sizeof(float));
        in += (size_t)take * channels;
        period->cursor += take;
        frames -= take;
        if (period->cursor == period->frames)
        {
            CallBlock(period, false);
            period->cursor = 0;
        }
    }
}
