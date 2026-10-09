// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The spatial audio object render stream for object streams. Each pass
// starts with BeginUpdatingAudioObjects, which gives the pass's frame
// count and the dynamic objects still available; the period fills a
// pass's worth of the bed and the objects, the bed's channels go to its
// static objects, and each active object takes a dynamic object, writes
// its frames, position (the library's frame is Windows': +x right, +y
// up, -z ahead) and volume. An object that goes inactive ends its
// stream and is released.

#include "wasapi_objects.h"

#include "context.h"

#include <string.h>

// The widest bed: 7.1.4.
#define MAX_BED_CHANNELS 12u

static const GUID s_iidSpatialClient = {
    0xBBF8E066, 0xAAAA, 0x49BE, {0x9A, 0x4D, 0xFD, 0x2A, 0x85, 0x8E, 0xA2, 0x7F}};
static const GUID s_iidRenderStream = {
    0xBAB5F473, 0xB423, 0x477B, {0x85, 0xF5, 0xB5, 0xA3, 0x32, 0xA0, 0x41, 0x53}};

// The static object of a bed channel's speaker; None for a speaker the
// bed cannot carry.
static AudioObjectType StaticObjectOf(maudSpeaker speaker)
{
    switch (speaker)
    {
    case maud_speakerFrontLeft:
        return AudioObjectType_FrontLeft;
    case maud_speakerFrontRight:
        return AudioObjectType_FrontRight;
    case maud_speakerFrontCenter:
        return AudioObjectType_FrontCenter;
    case maud_speakerLowFrequency:
        return AudioObjectType_LowFrequency;
    case maud_speakerBackLeft:
        return AudioObjectType_BackLeft;
    case maud_speakerBackRight:
        return AudioObjectType_BackRight;
    case maud_speakerSideLeft:
        return AudioObjectType_SideLeft;
    case maud_speakerSideRight:
        return AudioObjectType_SideRight;
    case maud_speakerTopFrontLeft:
        return AudioObjectType_TopFrontLeft;
    case maud_speakerTopFrontRight:
        return AudioObjectType_TopFrontRight;
    case maud_speakerTopBackLeft:
        return AudioObjectType_TopBackLeft;
    case maud_speakerTopBackRight:
        return AudioObjectType_TopBackRight;
    default:
        return AudioObjectType_None;
    }
}

// The static object of each of the layout's channels, a mono layout's
// at the front centre; false when one has none.
static bool BedOf(maudChannelLayout layout, AudioObjectType* types, uint32_t channels)
{
    for (uint32_t c = 0; c < channels; ++c)
    {
        types[c] = channels == 1 ? AudioObjectType_FrontCenter
                                 : StaticObjectOf(maudGetLayoutSpeaker(layout, c));
        if (types[c] == AudioObjectType_None)
        {
            return false;
        }
    }
    return true;
}

// The first object format the client supports: its rate is the stream's
// or the stream cannot run here.
static bool FormatFits(ISpatialAudioClient* client, uint32_t rate, WAVEFORMATEX* formatOut)
{
    IAudioFormatEnumerator* formats = nullptr;
    WAVEFORMATEX* format = nullptr;
    bool fits =
        SUCCEEDED(ISpatialAudioClient_GetSupportedAudioObjectFormatEnumerator(client, &formats)) &&
        SUCCEEDED(IAudioFormatEnumerator_GetFormat(formats, 0, &format)) && format != nullptr &&
        format->nSamplesPerSec == rate && format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT &&
        format->nChannels == 1;
    if (fits)
    {
        *formatOut = *format;
        formatOut->cbSize = 0;
    }
    if (formats != nullptr)
    {
        IAudioFormatEnumerator_Release(formats);
    }
    return fits;
}

// The block: the bed's objects, the held objects, the records, then the
// frames, a pass's worth of the bed and of each object.
static bool Allocate(maudContext* context, maudWasapiObjects* objects, uint32_t channels,
                     uint32_t count)
{
    size_t pointers = (size_t)(channels + count) * sizeof(ISpatialAudioObject*);
    size_t records = (size_t)count * sizeof(maudStreamObject);
    size_t floats = (size_t)objects->passFrames * (channels + count);
    objects->storageBytes = pointers + records + floats * sizeof(float);
    objects->storage =
        maudContextAllocate(context, objects->storageBytes, alignof(maudStreamObject));
    if (objects->storage == nullptr)
    {
        return false;
    }
    memset(objects->storage, 0, pointers);
    objects->channels = channels;
    objects->count = count;
    objects->bed = (ISpatialAudioObject**)objects->storage;
    objects->held = objects->bed + channels;
    objects->records = (maudStreamObject*)(objects->held + count);
    objects->frames = (float*)(objects->records + count);
    float* frames = objects->frames + (size_t)objects->passFrames * channels;
    for (uint32_t i = 0; i < count; ++i)
    {
        objects->records[i] =
            (maudStreamObject){.samples = frames + (size_t)i * objects->passFrames};
    }
    return true;
}

// The stream and its bed, from the client.
static maudResult Activate(maudWasapiObjects* objects, const maudStreamCore* core, HANDLE event,
                           const WAVEFORMATEX* format, const AudioObjectType* types,
                           uint32_t channels)
{
    AudioObjectType mask = AudioObjectType_None;
    for (uint32_t c = 0; c < channels; ++c)
    {
        mask = (AudioObjectType)(mask | types[c]);
    }
    SpatialAudioObjectRenderStreamActivationParams params = {
        .ObjectFormat = format,
        .StaticObjectTypeMask = mask,
        .MinDynamicObjectCount = 0,
        .MaxDynamicObjectCount = core->period.objectCount,
        .Category = core->def.role == maud_roleCommunications ? AudioCategory_Communications
                                                              : AudioCategory_GameEffects,
        .EventHandle = event,
        .NotifyObject = nullptr,
    };
    PROPVARIANT value;
    PropVariantInit(&value);
    value.vt = VT_BLOB;
    value.blob.cbSize = sizeof(params);
    value.blob.pBlobData = (BYTE*)&params;
    if (FAILED(ISpatialAudioClient_ActivateSpatialAudioStream(
            objects->client, &value, &s_iidRenderStream, (void**)&objects->stream)))
    {
        objects->stream = nullptr;
        return maud_errorPlatform;
    }
    for (uint32_t c = 0; c < channels; ++c)
    {
        if (FAILED(ISpatialAudioObjectRenderStream_ActivateSpatialAudioObject(
                objects->stream, types[c], &objects->bed[c])))
        {
            objects->bed[c] = nullptr;
            return maud_errorPlatform;
        }
    }
    return maud_success;
}

maudResult maudWasapiOpenObjects(maudContext* context, IMMDevice* device, maudStreamCore* core,
                                 HANDLE event, maudWasapiObjects* objects)
{
    *objects = (maudWasapiObjects){0};
    uint32_t channels = core->period.channelCount;
    AudioObjectType types[MAX_BED_CHANNELS];
    WAVEFORMATEX format;
    if (channels > MAX_BED_CHANNELS || !BedOf(core->format.layout, types, channels) ||
        FAILED(IMMDevice_Activate(device, &s_iidSpatialClient, CLSCTX_INPROC_SERVER, nullptr,
                                  (void**)&objects->client)))
    {
        objects->client = nullptr;
        return maud_errorUnsupported;
    }
    if (!FormatFits(objects->client, core->format.sampleRate, &format) ||
        FAILED(
            ISpatialAudioClient_GetMaxFrameCount(objects->client, &format, &objects->passFrames)) ||
        objects->passFrames == 0)
    {
        maudWasapiCloseObjects(context, objects);
        return maud_errorUnsupported;
    }
    if (!Allocate(context, objects, channels, core->period.objectCount))
    {
        maudWasapiCloseObjects(context, objects);
        return maud_errorCapacity;
    }
    maudResult result = Activate(objects, core, event, &format, types, channels);
    if (result != maud_success)
    {
        maudWasapiCloseObjects(context, objects);
    }
    return result;
}

void maudWasapiCloseObjects(maudContext* context, maudWasapiObjects* objects)
{
    // The bed's objects and the held ones lie side by side.
    for (uint32_t i = 0; i < objects->channels + objects->count; ++i)
    {
        if (objects->bed[i] != nullptr)
        {
            ISpatialAudioObject_Release(objects->bed[i]);
        }
    }
    if (objects->stream != nullptr)
    {
        ISpatialAudioObjectRenderStream_Release(objects->stream);
    }
    if (objects->client != nullptr)
    {
        ISpatialAudioClient_Release(objects->client);
    }
    if (objects->storage != nullptr)
    {
        maudContextRelease(context, objects->storage, objects->storageBytes,
                           alignof(maudStreamObject));
    }
    *objects = (maudWasapiObjects){0};
}

// Copies frames into an object's buffer for the pass; false when it has
// none, as when its stream ended.
static bool Write(ISpatialAudioObject* object, const float* frames, size_t stride, UINT32 count)
{
    BYTE* buffer = nullptr;
    UINT32 bytes = 0;
    if (FAILED(ISpatialAudioObject_GetBuffer(object, &buffer, &bytes)) || buffer == nullptr)
    {
        return false;
    }
    float* out = (float*)buffer;
    UINT32 room = bytes / (UINT32)sizeof(float);
    UINT32 written = count < room ? count : room;
    for (UINT32 i = 0; i < written; ++i)
    {
        out[i] = frames[(size_t)i * stride];
    }
    return true;
}

// Gives back a held object: its stream ends now.
static void Release(maudWasapiObjects* objects, uint32_t index)
{
    HRESULT ended = ISpatialAudioObject_SetEndOfStream(objects->held[index], 0);
    (void)ended;
    ISpatialAudioObject_Release(objects->held[index]);
    objects->held[index] = nullptr;
}

// Writes an active object, taking a dynamic object for it first if it
// holds none and one is left; one that cannot be had is not heard.
static void Play(maudWasapiObjects* objects, uint32_t index, UINT32 frames, UINT32* available)
{
    const maudStreamObject* record = &objects->records[index];
    if (objects->held[index] == nullptr)
    {
        if (*available == 0 ||
            FAILED(ISpatialAudioObjectRenderStream_ActivateSpatialAudioObject(
                objects->stream, AudioObjectType_Dynamic, &objects->held[index])))
        {
            objects->held[index] = nullptr;
            return;
        }
        --*available;
    }
    ISpatialAudioObject* object = objects->held[index];
    float volume = record->gain < 0.0f ? 0.0f : record->gain > 1.0f ? 1.0f : record->gain;
    if (!Write(object, record->samples, 1, frames) ||
        FAILED(ISpatialAudioObject_SetPosition(object, record->position[0], record->position[1],
                                               record->position[2])) ||
        FAILED(ISpatialAudioObject_SetVolume(object, volume)))
    {
        Release(objects, index);
    }
}

HRESULT maudWasapiRenderObjects(maudWasapiObjects* objects, maudStreamCore* core, bool running,
                                UINT32* framesOut)
{
    UINT32 available = 0;
    UINT32 frames = 0;
    *framesOut = 0;
    HRESULT result = ISpatialAudioObjectRenderStream_BeginUpdatingAudioObjects(objects->stream,
                                                                               &available, &frames);
    if (FAILED(result))
    {
        return result;
    }
    frames = frames < objects->passFrames ? frames : objects->passFrames;
    *framesOut = frames;
    maudPeriod* period = &core->period;
    uint32_t channels = period->channelCount;
    // What the platform offers: the objects already held, and those left.
    uint32_t held = 0;
    for (uint32_t i = 0; i < period->objectCount; ++i)
    {
        held += objects->held[i] != nullptr ? 1u : 0u;
    }
    period->objectsAvailable = held + available;
    if (running)
    {
        maudPullObjects(period, objects->frames, objects->records, frames);
    }
    else
    {
        memset(objects->frames, 0, (size_t)frames * channels * sizeof(float));
    }
    for (uint32_t c = 0; c < channels; ++c)
    {
        bool written = Write(objects->bed[c], objects->frames + c, channels, frames);
        (void)written;
    }
    for (uint32_t i = 0; i < period->objectCount; ++i)
    {
        if (running && objects->records[i].active)
        {
            Play(objects, i, frames, &available);
        }
        else if (objects->held[i] != nullptr)
        {
            Release(objects, i);
        }
    }
    return ISpatialAudioObjectRenderStream_EndUpdatingAudioObjects(objects->stream);
}
