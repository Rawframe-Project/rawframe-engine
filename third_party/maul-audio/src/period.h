// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The fixed-period adapter: a stream's callback always gets blocks of
// the stream's period, whatever sizes the device side moves. It holds
// one period of interleaved samples.

#ifndef MAUL_AUDIO_SRC_PERIOD_H
#define MAUL_AUDIO_SRC_PERIOD_H

#include "maul-audio/stream.h"

typedef struct maudPeriod
{
    maudStreamCallback callback;
    void* user;
    // frames * channelCount interleaved samples.
    float* samples;
    uint32_t channelCount;
    uint32_t frames;
    uint32_t sampleRate;
    maudChannelLayout layout;
    // Output: frames of samples already handed out (frames when empty).
    // Input: frames of samples already filled.
    uint32_t cursor;
    // The stream frame index of the next block's first frame.
    uint64_t nextBlock;
} maudPeriod;

// Sets a period up over samples, which holds frames * channelCount
// floats, for an output or an input stream.
void maudInitPeriod(maudPeriod* period, const maudStreamDef* def, const maudStreamFormat* format,
                    float* samples);

// Fills frames * channelCount samples of out with the callback's
// output, calling it once per period as needed. Real-time safe.
void maudPullPeriod(maudPeriod* period, float* out, uint32_t frames);

// Passes frames * channelCount samples of in to the callback, once per
// completed period. Real-time safe.
void maudPushPeriod(maudPeriod* period, const float* in, uint32_t frames);

#endif // MAUL_AUDIO_SRC_PERIOD_H
