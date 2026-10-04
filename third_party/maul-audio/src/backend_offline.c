// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The offline backend: no hardware and no thread. Its devices are
// scripted by the host and run at any rate, so a required rate is
// always native; it has no converter. The caller's thread renders its
// streams, so only pull mode exists.

#include "backend.h"
#include "device.h"

#define OFFLINE_MIN_RATE 8000u
#define OFFLINE_MAX_RATE 384000u

static maudResult AddStartingDevice(maudContext* context, maudDirection direction, const char* name,
                                    const char* key)
{
    maudDeviceSpec spec = {
        .info = {.direction = direction,
                 .nativeLayout = maud_layoutStereo,
                 .nativeSampleRate = context->def.offlineSampleRate,
                 .minSampleRate = OFFLINE_MIN_RATE,
                 .maxSampleRate = OFFLINE_MAX_RATE},
        .name = name,
        .nameLength = __builtin_strlen(name),
        .key = key,
        .keyLength = __builtin_strlen(key),
    };
    maudDeviceId id;
    return maudAddDevice(context, &spec, &id);
}

static maudResult OpenContext(maudContext* context)
{
    maudResult result =
        AddStartingDevice(context, maud_directionOutput, "Offline output", "offline-output");
    if (result != maud_success)
    {
        return result;
    }
    return AddStartingDevice(context, maud_directionInput, "Offline input", "offline-input");
}

static maudResult OpenStream(const maudContext* context, const maudStreamDef* def,
                             const maudDeviceInfo* device, maudStreamFormat* formatOut)
{
    if (def->mode != maud_modePull || def->ratePolicy == maud_ratePlatformConverted)
    {
        return maud_errorUnsupported;
    }
    uint32_t native = device != nullptr ? device->nativeSampleRate : context->def.offlineSampleRate;
    uint32_t rate = def->ratePolicy == maud_rateNative ? native : def->sampleRate;
    *formatOut = (maudStreamFormat){
        .sampleRate = rate,
        .periodFrames = def->periodFrames != 0 ? def->periodFrames : rate / 100,
        .layout = def->layout,
        .ratePolicy = def->ratePolicy,
    };
    return maud_success;
}

static const maudBackend s_offline = {
    .kind = maud_backendOffline,
    .openContext = OpenContext,
    .closeContext = nullptr,
    .pump = nullptr,
    .openStream = OpenStream,
    .attachStream = nullptr,
    .detachStream = nullptr,
    .setStreamActive = nullptr,
    .retargetStream = nullptr,
    .rendersOnCaller = true,
    .hasNoVoice = true,
};

const maudBackend* maudGetOfflineBackend(void)
{
    return &s_offline;
}
