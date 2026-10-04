// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The public voice activity detector: a def, a block from its allocator,
// and the channels' mean fed to the analysis.

#include "allocator.h"
#include "vad_core.h"

#include "maul-audio/voice.h"

#define VOICE_DETECTOR_DEF_COOKIE 0x6D766164u
#define MIN_RATE                  8000u
#define MAX_RATE                  384000u
// The longest hangover a def may ask for.
#define MAX_HANGOVER_MS 10000u

struct maudVoiceDetector
{
    maudAllocator allocator;
    uint32_t channels;
    float scale;
    maudVadCore core;
};

maudVoiceDetectorDef maudDefaultVoiceDetectorDef(void)
{
    return (maudVoiceDetectorDef){
        .cookie = VOICE_DETECTOR_DEF_COOKIE,
        .sampleRate = 48000,
        .layout = maud_layoutMono,
        .aggressiveness = 1,
        .hangoverMilliseconds = 200,
        .allocator = {0},
    };
}

static bool DefValid(const maudVoiceDetectorDef* def)
{
    return def->cookie == VOICE_DETECTOR_DEF_COOKIE && def->sampleRate >= MIN_RATE &&
           def->sampleRate <= MAX_RATE && maudGetLayoutChannelCount(def->layout) != 0 &&
           def->aggressiveness <= 3 && def->hangoverMilliseconds <= MAX_HANGOVER_MS &&
           maudIsAllocatorValid(&def->allocator);
}

maudResult maudCreateVoiceDetector(const maudVoiceDetectorDef* def, maudVoiceDetector** detectorOut)
{
    if (detectorOut != nullptr)
    {
        *detectorOut = nullptr;
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
                    def->hangoverMilliseconds / 10);
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

maudResult maudDetectVoice(maudVoiceDetector* detector, const float* frames, uint32_t frameCount,
                           maudVoiceState* stateOut)
{
    if (detector == nullptr || (frames == nullptr && frameCount != 0))
    {
        return maud_errorInvalid;
    }
    maudVadCore* core = &detector->core;
    for (uint32_t i = 0; i < frameCount; ++i)
    {
        float sum = 0.0f;
        for (uint32_t c = 0; c < detector->channels; ++c)
        {
            sum += frames[(size_t)i * detector->channels + c];
        }
        maudVadSample(core, sum * detector->scale);
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
