// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The public voice activity detector: a def, a block from its allocator,
// and the channels' mean fed to the analysis.

#include "allocator.h"
#include "vad_core.h"

#include "maul-audio/voice.h"

#include <string.h>

#define MIN_RATE 8000u
#define MAX_RATE 384000u
// The longest hangover a def may ask for.
#define MAX_HANGOVER 10.0f

struct maudVoiceDetector
{
    maudAllocator allocator;
    uint32_t channels;
    float scale;
    maudVadCore core;
};

static bool DefValid(const maudVoiceDetectorDef* def)
{
    return def->cookie == MAUD_VOICE_DETECTOR_DEF_COOKIE && def->sampleRate >= MIN_RATE &&
           def->sampleRate <= MAX_RATE && maudGetLayoutChannelCount(def->layout) != 0 &&
           def->aggressiveness <= 3 && def->hangover >= 0.0f && def->hangover <= MAX_HANGOVER &&
           maudIsAllocatorValid(&def->allocator);
}

maudResult maudCreateVoiceDetector(const maudVoiceDetectorDef* def, maudVoiceDetector** detectorOut)
{
    if (detectorOut != nullptr)
    {
        *detectorOut = nullptr;
    }
    if (def != nullptr && def->cookie == MAUD_VOICE_DETECTOR_DEF_COOKIE &&
        def->version != MAUD_ABI_VERSION)
    {
        return maud_errorVersion;
    }
    if (def == nullptr || detectorOut == nullptr || !DefValid(def))
    {
        return maud_errorInvalid;
    }
    maudVoiceDetector* detector =
        maudAllocate(&def->allocator, sizeof(maudVoiceDetector), alignof(maudVoiceDetector));
    if (detector == nullptr)
    {
        return maud_errorCapacity;
    }
    uint32_t channels = maudGetLayoutChannelCount(def->layout);
    detector->allocator = def->allocator;
    detector->channels = channels;
    detector->scale = 1.0f / (float)channels;
    maudInitVadCore(&detector->core, def->sampleRate, def->aggressiveness,
                    (uint32_t)(def->hangover * 100.0f + 0.5f));
    *detectorOut = detector;
    return maud_success;
}

void maudDestroyVoiceDetector(maudVoiceDetector* detector)
{
    if (detector == nullptr)
    {
        return;
    }
    maudAllocator allocator = detector->allocator;
    maudRelease(&allocator, detector, sizeof(maudVoiceDetector), alignof(maudVoiceDetector));
}

maudResult maudVoiceDetector_Detect(maudVoiceDetector* detector, const float* frames,
                                    uint32_t frameCount, maudVoiceState* stateOut)
{
    // Cleared first, so a failure leaves zero.
    if (stateOut != nullptr)
    {
        memset(stateOut, 0, sizeof(*stateOut));
    }
    if (detector == nullptr || (frames == nullptr && frameCount != 0))
    {
        return maud_errorInvalid;
    }
    maudVadCore* core = &detector->core;
    for (uint32_t i = 0; i < frameCount;)
    {
        float mono[MAUD_VAD_CHUNK];
        uint32_t n = frameCount - i < MAUD_VAD_CHUNK ? frameCount - i : MAUD_VAD_CHUNK;
        for (uint32_t k = 0; k < n; ++k)
        {
            float sum = 0.0f;
            for (uint32_t c = 0; c < detector->channels; ++c)
            {
                sum += frames[(size_t)(i + k) * detector->channels + c];
            }
            mono[k] = sum * detector->scale;
        }
        bool done = false;
        i += maudVadSamples(core, mono, n, &done);
    }
    if (stateOut != nullptr)
    {
        *stateOut = (maudVoiceState){
            .active = core->active,
            .probability = core->probability,
            .levelDbfs = core->level,
            .noiseDbfs = core->noise[MAUD_VAD_BANDS],
            .frames = core->frames,
        };
    }
    return maud_success;
}
