// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The fixed-period adapter: a stream's callback always gets blocks of
// the stream's period, whatever sizes the device side moves. It holds
// one period of interleaved samples, and an object stream's objects with
// one period of frames each.

#ifndef MAUL_AUDIO_SRC_PERIOD_H
#define MAUL_AUDIO_SRC_PERIOD_H

#include "maul-audio/objects.h"
#include "maul-audio/stream.h"

// The alignment of a stream's storage block (stream_open.c), whose
// samples come first and objects last.
#define MAUD_STREAM_STORAGE_ALIGN alignof(maudStreamObject)

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
    // An object stream's objects, as the callback last left them, and
    // their frames, one period each, object after object; NULL and 0 for
    // other streams. The backend sets objectsAvailable before a pull.
    maudStreamObject* objects;
    float* objectSamples;
    uint32_t objectCount;
    uint32_t objectsAvailable;
} maudPeriod;

// Sets a period up over samples, which holds frames * channelCount
// floats, for an output or an input stream.
void maudInitPeriod(maudPeriod* period, const maudStreamDef* def, const maudStreamFormat* format,
                    float* samples);

// Gives an output period count objects over objects and samples, which
// holds frames * count floats: silent, at the listener, at unit gain,
// inactive. All of them are available until the backend says otherwise.
void maudInitObjects(maudPeriod* period, maudStreamObject* objects, float* samples, uint32_t count);

// Fills frames * channelCount samples of bed with the callback's bed and
// frames of each of objectsOut's samples with its objects' frames,
// calling it once per period as needed, then gives objectsOut the
// position, gain and activity the callback last set. Real-time safe.
void maudPullObjects(maudPeriod* period, float* bed, maudStreamObject* objectsOut, uint32_t frames);

// Fills frames * channelCount samples of out with the callback's
// output, calling it once per period as needed. Real-time safe.
void maudPullPeriod(maudPeriod* period, float* out, uint32_t frames);

// Passes frames * channelCount samples of in to the callback, once per
// completed period. Real-time safe.
void maudPushPeriod(maudPeriod* period, const float* in, uint32_t frames);

#endif // MAUL_AUDIO_SRC_PERIOD_H
