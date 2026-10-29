// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The automatic gain control, after WebRTC AGC2's adaptive digital
// controller. Its voice detector's core analyzes the input; at each
// 10 ms frame the speech level (a weighted average over confident
// speech frames, reliable after 12 in a row) sets a target gain, capped
// so the noise floor stays under its ceiling; the gain moves toward it
// by a bounded step, rising only after 12 confident speech frames in a
// row. Across the next frame the gain ramps linearly to the new value,
// and a peak limiter keeps every sample under -1 dBFS.

#include "allocator.h"
#include "flush.h"
#include "vad_core.h"

#include "maul-audio/voice.h"

#include <math.h>
#include <string.h>

#define MIN_RATE 8000u
#define MAX_RATE 384000u
// AGC2's rules: frames of confident speech in a row before the gain may
// rise or the level is trusted; the probability a frame needs; the level
// estimate's memory in frames once full.
#define ADJACENT_FRAMES 12u
#define CONFIDENT       0.95f
#define LEVEL_MEMORY    400.0
// The limiter: its ceiling (-1 dBFS) and release time.
#define CEILING         0.8912509f
#define RELEASE_SECONDS 0.05

struct maudGainControl
{
    maudAllocator allocator;
    uint32_t channels;
    float scale;
    maudGainControlDef def;
    maudVadCore vad;
    // The speech level estimate: a weighted average, its preliminary
    // form while a run of speech has not yet proved itself, and the
    // frames until its memory is full.
    double levelSum;
    double levelWeight;
    double reliableSum;
    double reliableWeight;
    uint32_t warmup;
    uint32_t adjacent;
    bool reliable;
    float speechDbfs;
    // The gain in dB, and the linear ramp across the current frame.
    float gainDb;
    float gain;
    float gainStep;
    float increaseLeft;
    // The limiter's envelope and release per sample.
    float envelope;
    float release;
};

static bool Within(float value, float low, float high)
{
    return value >= low && value <= high;
}

static bool DefValid(const maudGainControlDef* def)
{
    return def->cookie == MAUD_GAIN_CONTROL_DEF_COOKIE && def->sampleRate >= MIN_RATE &&
           def->sampleRate <= MAX_RATE && maudGetLayoutChannelCount(def->layout) != 0 &&
           Within(def->targetDbfs, -40.0f, -6.0f) && Within(def->minGainDb, -30.0f, 0.0f) &&
           Within(def->maxGainDb, 0.0f, 60.0f) &&
           Within(def->initialGainDb, def->minGainDb, def->maxGainDb) &&
           Within(def->maxChangeDbPerSecond, 1.0f, 30.0f) &&
           Within(def->maxNoiseDbfs, -80.0f, -20.0f) && def->aggressiveness <= 3 &&
           maudIsAllocatorValid(&def->allocator);
}

static float DbToGain(float db)
{
    return powf(10.0f, db / 20.0f);
}

maudResult maudCreateGainControl(const maudGainControlDef* def, maudGainControl** gainOut)
{
    if (gainOut != nullptr)
    {
        *gainOut = nullptr;
    }
    if (def != nullptr && def->cookie == MAUD_GAIN_CONTROL_DEF_COOKIE &&
        def->version != MAUD_ABI_VERSION)
    {
        return maud_errorVersion;
    }
    if (def == nullptr || gainOut == nullptr || !DefValid(def))
    {
        return maud_errorInvalid;
    }
    maudGainControl* gain =
        maudAllocate(&def->allocator, sizeof(maudGainControl), alignof(maudGainControl));
    if (gain == nullptr)
    {
        return maud_errorCapacity;
    }
    uint32_t channels = maudGetLayoutChannelCount(def->layout);
    *gain = (maudGainControl){
        .allocator = def->allocator,
        .channels = channels,
        .scale = 1.0f / (float)channels,
        .def = *def,
        .warmup = (uint32_t)LEVEL_MEMORY,
        .speechDbfs = MAUD_FLOOR_DBFS,
        .gainDb = def->initialGainDb,
        .gain = DbToGain(def->initialGainDb),
        .increaseLeft = (float)ADJACENT_FRAMES,
        .release = (float)exp(-1.0 / (RELEASE_SECONDS * def->sampleRate)),
    };
    // The gain control's detector decides each frame; its hangover is not
    // used.
    maudInitVadCore(&gain->vad, def->sampleRate, def->aggressiveness, 0);
    *gainOut = gain;
    return maud_success;
}

void maudDestroyGainControl(maudGainControl* gain)
{
    if (gain == nullptr)
    {
        return;
    }
    maudAllocator allocator = gain->allocator;
    maudRelease(&allocator, gain, sizeof(maudGainControl), alignof(maudGainControl));
}

// Updates the speech level from a frame: confident speech adds to the
// preliminary estimate, which becomes the reliable one after a run of
// ADJACENT_FRAMES; a shorter run is forgotten.
static void UpdateLevel(maudGainControl* gain, bool confident, float level)
{
    if (!confident)
    {
        if (gain->adjacent < ADJACENT_FRAMES)
        {
            gain->levelSum = gain->reliableSum;
            gain->levelWeight = gain->reliableWeight;
        }
        gain->adjacent = 0;
        return;
    }
    gain->adjacent++;
    double leak = gain->warmup == 0 ? 1.0 - 1.0 / LEVEL_MEMORY : 1.0;
    gain->warmup -= gain->warmup > 0 ? 1u : 0u;
    gain->levelSum = gain->levelSum * leak + (double)level;
    gain->levelWeight = gain->levelWeight * leak + 1.0;
    if (gain->adjacent >= ADJACENT_FRAMES)
    {
        gain->reliableSum = gain->levelSum;
        gain->reliableWeight = gain->levelWeight;
        gain->speechDbfs = (float)(gain->levelSum / gain->levelWeight);
        gain->reliable = true;
    }
}

// Chooses the next frame's gain and sets the ramp to it.
static void UpdateGain(maudGainControl* gain)
{
    // The limiter's envelope, flushed once a frame: a frame's release
    // takes it down a few tens of percent, so from the floor it stays
    // normal until the next.
    gain->envelope = maudFlush(gain->envelope);
    const maudGainControlDef* def = &gain->def;
    const maudVadCore* vad = &gain->vad;
    bool confident = vad->probability >= CONFIDENT;
    UpdateLevel(gain, confident, vad->level);
    float target = gain->reliable ? def->targetDbfs - gain->speechDbfs : gain->gainDb;
    target = target < def->minGainDb ? def->minGainDb : target;
    target = target > def->maxGainDb ? def->maxGainDb : target;
    // The noise must stay under its ceiling; the cap never forces the
    // gain below 0 dB.
    float noiseCap = def->maxNoiseDbfs - vad->noise[MAUD_VAD_BANDS];
    noiseCap = noiseCap > 0.0f ? noiseCap : 0.0f;
    target = target > noiseCap ? noiseCap : target;
    // Rising waits for a run of confident speech; the run's first moment
    // may then catch up the steps it waited.
    float step = def->maxChangeDbPerSecond / 100.0f;
    float rise = 0.0f;
    if (!confident)
    {
        gain->increaseLeft = (float)ADJACENT_FRAMES;
    }
    else if (gain->increaseLeft > 0.0f)
    {
        gain->increaseLeft -= 1.0f;
        rise = gain->increaseLeft == 0.0f ? step * (float)ADJACENT_FRAMES : 0.0f;
    }
    else
    {
        rise = step;
    }
    float change = target - gain->gainDb;
    change = change > rise ? rise : change;
    change = change < -step ? -step : change;
    gain->gainDb += change;
    gain->gainStep = (DbToGain(gain->gainDb) - gain->gain) / (float)vad->frameSamples;
}

// Applies the current gain and the limiter to count frames of channels,
// the gain and the envelope held in locals (the frames' stores could
// otherwise be theirs, so each frame would reload them).
static void ApplyFrames(maudGainControl* gain, float* frames, uint32_t count)
{
    float g = gain->gain;
    float envelope = gain->envelope;
    const float step = gain->gainStep;
    const float release = gain->release;
    const uint32_t channels = gain->channels;
    for (uint32_t i = 0; i < count; ++i)
    {
        float* frame = frames + (size_t)i * channels;
        float peak = 0.0f;
        for (uint32_t c = 0; c < channels; ++c)
        {
            frame[c] *= g;
            float magnitude = fabsf(frame[c]);
            peak = magnitude > peak ? magnitude : peak;
        }
        float released = envelope * release;
        envelope = peak > released ? peak : released;
        if (envelope > CEILING)
        {
            float limit = CEILING / envelope;
            for (uint32_t c = 0; c < channels; ++c)
            {
                frame[c] *= limit;
            }
        }
        g += step;
    }
    gain->gain = g;
    gain->envelope = envelope;
}

maudResult maudGainControl_Apply(maudGainControl* gain, float* frames, uint32_t frameCount,
                                 maudGainState* stateOut)
{
    // Cleared first, so a failure leaves zero.
    if (stateOut != nullptr)
    {
        memset(stateOut, 0, sizeof(*stateOut));
    }
    if (gain == nullptr || (frames == nullptr && frameCount != 0))
    {
        return maud_errorInvalid;
    }
    // A chunk at a time, up to a frame's end: the detector's samples are
    // the frames before the gain, and the gain changes only once a frame
    // completes, so analysing a chunk before applying it is the same.
    for (uint32_t i = 0; i < frameCount;)
    {
        float mono[MAUD_VAD_CHUNK];
        uint32_t n = frameCount - i < MAUD_VAD_CHUNK ? frameCount - i : MAUD_VAD_CHUNK;
        for (uint32_t k = 0; k < n; ++k)
        {
            const float* frame = frames + (size_t)(i + k) * gain->channels;
            float sum = 0.0f;
            for (uint32_t c = 0; c < gain->channels; ++c)
            {
                sum += frame[c];
            }
            mono[k] = sum * gain->scale;
        }
        bool done = false;
        n = maudVadSamples(&gain->vad, mono, n, &done);
        ApplyFrames(gain, frames + (size_t)i * gain->channels, n);
        if (done)
        {
            // The ramp lands exactly on the frame's gain before the next.
            gain->gain = DbToGain(gain->gainDb);
            UpdateGain(gain);
        }
        i += n;
    }
    if (stateOut != nullptr)
    {
        *stateOut = (maudGainState){
            .gainDb = gain->gainDb,
            .speechDbfs = gain->speechDbfs,
            .speechReliable = gain->reliable,
            .noiseDbfs = gain->vad.noise[MAUD_VAD_BANDS],
            .voiceActive = gain->vad.active,
            .frames = gain->vad.frames,
        };
    }
    return maud_success;
}
