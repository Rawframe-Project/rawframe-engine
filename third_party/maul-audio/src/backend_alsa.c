// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The ALSA backend's devices. ALSA has no server and no thread: the
// devices are the default PCM and each hardware endpoint the control
// interface lists, found without opening any of them, at creation and
// again whenever the drain finds /dev/snd changed.

#include "alsa_core.h"
#include "alsa_scan.h"
#include "alsa_stream.h"
#include "alsa_watch.h"
#include "backend.h"
#include "context.h"
#include "device.h"

#include <string.h>

// alsa-lib's messages go nowhere: the library prints nothing.
static void Quiet(const char* file, int line, const char* function, int error, const char* format,
                  va_list arguments)
{
    (void)file;
    (void)line;
    (void)function;
    (void)error;
    (void)format;
    (void)arguments;
}

snd_local_error_handler_t maudAlsaQuiet(const maudAlsaApi* api)
{
    return api->libErrorSetLocal(Quiet);
}

// The default PCM, for both directions.
static maudResult AddDefaults(maudContext* context)
{
    for (int direction = 0; direction < 2; ++direction)
    {
        maudDeviceSpec spec = {
            .info = {.direction = (maudDirection)direction},
            .name = "Default",
            .nameLength = 7,
            .key = "default",
            .keyLength = 7,
        };
        maudDeviceId device;
        maudResult result = maudAddDevice(context, &spec, &device);
        if (result != maud_success)
        {
            return result;
        }
        maudSetDefaultDevice(context, maud_roleGeneral, device);
        maudSetDefaultDevice(context, maud_roleCommunications, device);
    }
    return maud_success;
}

// The directory whose card nodes the context watches.
#define DEVICE_DIRECTORY "/dev/snd"

// Lists the endpoints again and brings the device table in line.
static maudResult Rescan(maudContext* context)
{
    maudAlsa* alsa = context->native;
    uint32_t count = maudAlsaScan(&alsa->api, alsa->endpoints, alsa->specs,
                                  context->def.limits.devices, context->def.limits.deviceTextBytes);
    return maudSyncDevices(context, alsa->specs, count, "default");
}

static void Release(maudContext* context, maudAlsa* alsa)
{
    maudAlsaCloseWatch(alsa->watch);
    maudUnloadAlsa(&alsa->api);
    maudContextRelease(context, alsa, alsa->bytes, alignof(maudAlsa));
    context->native = nullptr;
}

// Whether the ALSA structures the backend sets aside room for fit.
static bool StructsFit(const maudAlsaApi* api)
{
    return api->ctlCardInfoSizeof() <= MAUD_ALSA_STRUCT_BYTES &&
           api->pcmInfoSizeof() <= MAUD_ALSA_STRUCT_BYTES &&
           api->hwParamsSizeof() <= MAUD_ALSA_STRUCT_BYTES &&
           api->swParamsSizeof() <= MAUD_ALSA_STRUCT_BYTES;
}

static maudResult OpenContext(maudContext* context)
{
    uint32_t streams = context->def.limits.streams;
    uint32_t devices = context->def.limits.devices;
    size_t bytes = sizeof(maudAlsa) + (size_t)streams * sizeof(maudAlsaStream) +
                   (size_t)devices * (sizeof(maudAlsaEndpoint) + sizeof(maudDeviceSpec));
    maudAlsa* alsa = maudContextAllocate(context, bytes, alignof(maudAlsa));
    if (alsa == nullptr)
    {
        return maud_errorCapacity;
    }
    *alsa = (maudAlsa){
        .context = context,
        .streams = (maudAlsaStream*)(alsa + 1),
        .watch = -1,
        .bytes = bytes,
    };
    memset(alsa->streams, 0, (size_t)streams * sizeof(maudAlsaStream));
    alsa->endpoints = (maudAlsaEndpoint*)(alsa->streams + streams);
    alsa->specs = (maudDeviceSpec*)(alsa->endpoints + devices);
    context->native = alsa;
    if (!maudLoadAlsa(&alsa->api) || !StructsFit(&alsa->api))
    {
        Release(context, alsa);
        return maud_errorUnsupported;
    }
    snd_local_error_handler_t previous = maudAlsaQuiet(&alsa->api);
    maudResult result = AddDefaults(context);
    result = result == maud_success ? Rescan(context) : result;
    alsa->api.libErrorSetLocal(previous);
    alsa->watch = maudAlsaOpenWatch(DEVICE_DIRECTORY);
    if (result != maud_success)
    {
        Release(context, alsa);
    }
    return result;
}

static void CloseContext(maudContext* context)
{
    Release(context, context->native);
}

// Rescans when a card's node came, went or became readable.
static void Pump(maudContext* context)
{
    maudAlsa* alsa = context->native;
    if (maudAlsaTakeChanges(alsa->watch))
    {
        snd_local_error_handler_t previous = maudAlsaQuiet(&alsa->api);
        maudResult result = Rescan(context);
        (void)result;
        alsa->api.libErrorSetLocal(previous);
    }
}

// ALSA's rates are known only once a PCM is open: a native stream asks
// for the preferred rate here and takes the hardware's when it opens.
static maudResult OpenStream(const maudContext* context, const maudStreamDef* def,
                             const maudDeviceInfo* device, maudStreamFormat* formatOut)
{
    (void)device;
    // Only a hardware PCM can be had alone; the default one is shared.
    const maudDeviceSlot* chosen = maudFindDevice(context, def->device);
    bool hardware =
        chosen != nullptr && chosen->key.length >= 3 && memcmp(chosen->key.bytes, "hw:", 3) == 0;
    if (def->mode == maud_modePull || (def->share == maud_shareExclusive && !hardware))
    {
        return maud_errorUnsupported;
    }
    uint32_t rate = def->ratePolicy == maud_rateNative ? MAUD_ALSA_PREFERRED_RATE : def->sampleRate;
    *formatOut = (maudStreamFormat){
        .sampleRate = rate,
        .periodFrames = def->periodFrames != 0 ? def->periodFrames : rate / 100,
        .layout = def->layout,
        .ratePolicy = def->ratePolicy,
    };
    return maud_success;
}

static const maudBackend s_alsa = {
    .kind = maud_backendAlsa,
    .openContext = OpenContext,
    .closeContext = CloseContext,
    .pump = Pump,
    .openStream = OpenStream,
    .attachStream = maudAlsaAttachStream,
    .exclusive = true,
    .detachStream = maudAlsaDetachStream,
    .setStreamActive = maudAlsaSetStreamActive,
    .retargetStream = nullptr,
    .rendersOnCaller = false,
    .hasNoVoice = true,
};

const maudBackend* maudGetAlsaBackend(void)
{
    return &s_alsa;
}
