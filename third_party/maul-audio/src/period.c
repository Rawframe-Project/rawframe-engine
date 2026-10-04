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

static void CallBlock(maudPeriod* period, bool output)
{
    maudStreamBlock block = {
        .output = output ? period->samples : nullptr,
        .input = output ? nullptr : period->samples,
        .frameCount = period->frames,
        .sampleRate = period->sampleRate,
        .layout = period->layout,
        .position = period->nextBlock,
    };
    period->callback(&block, period->user);
    period->nextBlock += period->frames;
}

void maudPullPeriod(maudPeriod* period, float* out, uint32_t frames)
{
    size_t channels = period->channelCount;
    while (frames > 0)
    {
        if (period->cursor == period->frames)
        {
            memset(period->samples, 0, (size_t)period->frames * channels * sizeof(float));
            CallBlock(period, true);
            period->cursor = 0;
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
