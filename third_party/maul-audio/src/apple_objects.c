// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Apple's spatial mixer for object streams. The output unit's render
// callback calls maudRenderAppleObjects, which pulls a slice of the
// stream's bed and objects into the staging block, sets each object's
// bus from its record (enabled while active, its direction and distance
// from its position, its gain in decibels), then renders the mixer,
// whose input callbacks copy from the staging block, and interleaves
// what it rendered for the output unit. The mixer takes only
// non-interleaved frames.

#include "apple_objects.h"

#include "context.h"

#include <math.h>
#include <string.h>

#define DEGREES_PER_RADIAN 57.29577951308232f
// The mixer's gain floor and ceiling, in decibels.
#define MIN_GAIN_DB -120.0f
#define MAX_GAIN_DB 20.0f

bool maudAppleObjectsFit(const maudStreamCore* core)
{
    return core->period.channelCount <= 2;
}

// An object's bus: copies the slice's frames for it.
static OSStatus ObjectInput(void* user, AudioUnitRenderActionFlags* flags,
                            const AudioTimeStamp* time, UInt32 bus, UInt32 frames,
                            AudioBufferList* data)
{
    (void)flags;
    (void)time;
    (void)bus;
    const maudAppleObjectBus* source = user;
    const maudAppleObjects* objects = source->objects;
    float* out = data->mBuffers[0].mData;
    UInt32 copied = frames < objects->frames ? frames : objects->frames;
    memcpy(out, objects->records[source->index].samples, (size_t)copied * sizeof(float));
    memset(out + copied, 0, (size_t)(frames - copied) * sizeof(float));
    return noErr;
}

// The bed's bus: the slice's interleaved frames, a buffer per channel.
static OSStatus BedInput(void* user, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* time,
                         UInt32 bus, UInt32 frames, AudioBufferList* data)
{
    (void)flags;
    (void)time;
    (void)bus;
    const maudAppleObjectBus* source = user;
    const maudAppleObjects* objects = source->objects;
    UInt32 channels = objects->core->period.channelCount;
    UInt32 copied = frames < objects->frames ? frames : objects->frames;
    for (UInt32 c = 0; c < channels && c < data->mNumberBuffers; ++c)
    {
        float* out = data->mBuffers[c].mData;
        for (UInt32 i = 0; i < copied; ++i)
        {
            out[i] = objects->bed[(size_t)i * channels + c];
        }
        memset(out + copied, 0, (size_t)(frames - copied) * sizeof(float));
    }
    return noErr;
}

// 32-bit float frames of channels at the stream's rate, a buffer per
// channel.
static AudioStreamBasicDescription Format(const maudStreamCore* core, UInt32 channels)
{
    return (AudioStreamBasicDescription){
        .mSampleRate = core->format.sampleRate,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags =
            kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved,
        .mBytesPerPacket = (UInt32)sizeof(float),
        .mFramesPerPacket = 1,
        .mBytesPerFrame = (UInt32)sizeof(float),
        .mChannelsPerFrame = channels,
        .mBitsPerChannel = 32,
    };
}

// The system's renderer for what the output leads to.
static UInt32 OutputType(maudDeviceForm form)
{
    switch (form)
    {
    case maud_formHeadphones:
    case maud_formHeadset:
        return kSpatialMixerOutputType_Headphones;
    case maud_formSpeakers:
        return kSpatialMixerOutputType_BuiltInSpeakers;
    default:
        return kSpatialMixerOutputType_ExternalSpeakers;
    }
}

static bool SetProperty(AudioUnit mixer, AudioUnitPropertyID property, AudioUnitScope scope,
                        AudioUnitElement element, const void* value, UInt32 size)
{
    return AudioUnitSetProperty(mixer, property, scope, element, value, size) == noErr;
}

// One input bus: its format, its callback, its source mode and no
// distance attenuation.
static bool ConfigureBus(maudAppleObjects* objects, uint32_t bus, bool bed)
{
    AudioUnit mixer = objects->mixer;
    AudioStreamBasicDescription format =
        Format(objects->core, bed ? objects->core->period.channelCount : 1);
    AURenderCallbackStruct callback = {
        .inputProc = bed ? BedInput : ObjectInput,
        .inputProcRefCon = &objects->buses[bus],
    };
    UInt32 mode = bed ? kSpatialMixerSourceMode_Bypass : kSpatialMixerSourceMode_PointSource;
    // No attenuation: the most is 0 dB. The reference distance stays at
    // a metre, as the mixer renders a source within it as inside the
    // head, the same on both sides.
    MixerDistanceParams distance = {
        .mReferenceDistance = 1.0f,
        .mMaxDistance = 10000.0f,
        .mMaxAttenuation = 0.0f,
    };
    return SetProperty(mixer, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, bus, &format,
                       sizeof(format)) &&
           SetProperty(mixer, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, bus,
                       &callback, sizeof(callback)) &&
           SetProperty(mixer, kAudioUnitProperty_SpatialMixerSourceMode, kAudioUnitScope_Input, bus,
                       &mode, sizeof(mode)) &&
           SetProperty(mixer, kAudioUnitProperty_SpatialMixerDistanceParams, kAudioUnitScope_Input,
                       bus, &distance, sizeof(distance));
}

static bool Configure(maudAppleObjects* objects, maudDeviceForm form)
{
    AudioUnit mixer = objects->mixer;
    uint32_t count = objects->core->period.objectCount;
    UInt32 buses = count + 1;
    AudioStreamBasicDescription output = Format(objects->core, objects->core->period.channelCount);
    UInt32 algorithm = kSpatializationAlgorithm_UseOutputType;
    UInt32 type = OutputType(form);
    UInt32 slice = MAUD_APPLE_OBJECT_SLICE;
    // The speakers the mixer renders for: without a layout it renders as
    // for one, the same on every channel.
    AudioChannelLayout layout = {
        .mChannelLayoutTag = objects->core->period.channelCount == 1
                                 ? kAudioChannelLayoutTag_Mono
                                 : kAudioChannelLayoutTag_Stereo,
    };
    if (!SetProperty(mixer, kAudioUnitProperty_ElementCount, kAudioUnitScope_Input, 0, &buses,
                     sizeof(buses)) ||
        !SetProperty(mixer, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &output,
                     sizeof(output)) ||
        !SetProperty(mixer, kAudioUnitProperty_AudioChannelLayout, kAudioUnitScope_Output, 0,
                     &layout, sizeof(layout)) ||
        !SetProperty(mixer, kAudioUnitProperty_SpatialMixerOutputType, kAudioUnitScope_Global, 0,
                     &type, sizeof(type)) ||
        !SetProperty(mixer, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0,
                     &slice, sizeof(slice)))
    {
        return false;
    }
    for (uint32_t bus = 0; bus < buses; ++bus)
    {
        if (!ConfigureBus(objects, bus, bus == count) ||
            !SetProperty(mixer, kAudioUnitProperty_SpatializationAlgorithm, kAudioUnitScope_Input,
                         bus, &algorithm, sizeof(algorithm)))
        {
            return false;
        }
    }
    // The listener's own HRTF where the system has one; older systems
    // keep the generic one.
    if (__builtin_available(macOS 13.0, iOS 18.0, *))
    {
        UInt32 personalized = kSpatialMixerPersonalizedHRTFMode_Auto;
        bool set = SetProperty(mixer, kAudioUnitProperty_SpatialMixerPersonalizedHRTFMode,
                               kAudioUnitScope_Global, 0, &personalized, sizeof(personalized));
        (void)set;
    }
    return true;
}

// The list a mixer of up to two channels renders into: its header and
// two buffers.
#define MIXED_LIST_BYTES (sizeof(AudioBufferList) + sizeof(AudioBuffer))

// The staging block: the records, the buses, the mixer's list, then the
// bed's slice, each object's slice and the mixer's slices.
static bool Allocate(maudContext* context, maudAppleObjects* objects)
{
    uint32_t count = objects->core->period.objectCount;
    size_t channels = objects->core->period.channelCount;
    size_t floats = (size_t)MAUD_APPLE_OBJECT_SLICE * (2 * channels + count);
    size_t records = (size_t)count * sizeof(maudStreamObject);
    size_t buses = (size_t)(count + 1) * sizeof(maudAppleObjectBus);
    objects->storageBytes = records + buses + MIXED_LIST_BYTES + floats * sizeof(float);
    objects->storage =
        maudContextAllocate(context, objects->storageBytes, alignof(maudStreamObject));
    if (objects->storage == nullptr)
    {
        return false;
    }
    objects->records = objects->storage;
    objects->buses = (maudAppleObjectBus*)(objects->records + count);
    objects->mixedList = (AudioBufferList*)(objects->buses + count + 1);
    objects->bed = (float*)((char*)objects->mixedList + MIXED_LIST_BYTES);
    objects->mixed = objects->bed + (size_t)MAUD_APPLE_OBJECT_SLICE * channels;
    float* frames = objects->mixed + (size_t)MAUD_APPLE_OBJECT_SLICE * channels;
    for (uint32_t i = 0; i < count; ++i)
    {
        objects->records[i] =
            (maudStreamObject){.samples = frames + (size_t)i * MAUD_APPLE_OBJECT_SLICE};
    }
    for (uint32_t bus = 0; bus <= count; ++bus)
    {
        objects->buses[bus] = (maudAppleObjectBus){.objects = objects, .index = bus};
    }
    return true;
}

maudResult maudOpenAppleObjects(maudContext* context, maudStreamCore* core, maudDeviceForm form,
                                maudAppleObjects* objects)
{
    *objects = (maudAppleObjects){.core = core};
    AudioComponentDescription description = {
        .componentType = kAudioUnitType_Mixer,
        .componentSubType = kAudioUnitSubType_SpatialMixer,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
    };
    AudioComponent component = AudioComponentFindNext(nullptr, &description);
    if (!maudAppleObjectsFit(core) || component == nullptr)
    {
        return maud_errorPlatform;
    }
    if (!Allocate(context, objects))
    {
        return maud_errorCapacity;
    }
    // The mixer takes every object.
    core->period.objectsAvailable = core->period.objectCount;
    if (AudioComponentInstanceNew(component, &objects->mixer) != noErr)
    {
        objects->mixer = nullptr;
        maudCloseAppleObjects(context, objects);
        return maud_errorPlatform;
    }
    if (!Configure(objects, form) || AudioUnitInitialize(objects->mixer) != noErr)
    {
        maudCloseAppleObjects(context, objects);
        return maud_errorPlatform;
    }
    return maud_success;
}

void maudCloseAppleObjects(maudContext* context, maudAppleObjects* objects)
{
    if (objects->mixer != nullptr)
    {
        OSStatus uninitialized = AudioUnitUninitialize(objects->mixer);
        OSStatus disposed = AudioComponentInstanceDispose(objects->mixer);
        (void)uninitialized;
        (void)disposed;
    }
    if (objects->storage != nullptr)
    {
        maudContextRelease(context, objects->storage, objects->storageBytes,
                           alignof(maudStreamObject));
    }
    *objects = (maudAppleObjects){0};
}

// Sets an object's bus from its record: the library's frame (+x right,
// +y up, -z ahead) to the mixer's azimuth (0 ahead, 90 to the right),
// elevation and distance.
static void Place(AudioUnit mixer, uint32_t bus, const maudStreamObject* record)
{
    AudioUnitSetParameter(mixer, kSpatialMixerParam_Enable, kAudioUnitScope_Input, bus,
                          record->active ? 1.0f : 0.0f, 0);
    if (!record->active)
    {
        return;
    }
    float x = record->position[0];
    float y = record->position[1];
    float z = record->position[2];
    float across = sqrtf(x * x + z * z);
    float azimuth = across > 0.0f ? atan2f(x, -z) * DEGREES_PER_RADIAN : 0.0f;
    float elevation = across > 0.0f || y != 0.0f ? atan2f(y, across) * DEGREES_PER_RADIAN : 0.0f;
    float distance = sqrtf(across * across + y * y);
    float gain = record->gain > 0.0f ? 20.0f * log10f(record->gain) : MIN_GAIN_DB;
    gain = gain < MIN_GAIN_DB ? MIN_GAIN_DB : gain > MAX_GAIN_DB ? MAX_GAIN_DB : gain;
    AudioUnitSetParameter(mixer, kSpatialMixerParam_Azimuth, kAudioUnitScope_Input, bus, azimuth,
                          0);
    AudioUnitSetParameter(mixer, kSpatialMixerParam_Elevation, kAudioUnitScope_Input, bus,
                          elevation, 0);
    AudioUnitSetParameter(mixer, kSpatialMixerParam_Distance, kAudioUnitScope_Input, bus, distance,
                          0);
    AudioUnitSetParameter(mixer, kSpatialMixerParam_Gain, kAudioUnitScope_Input, bus, gain, 0);
}

OSStatus maudRenderAppleObjects(maudAppleObjects* objects, AudioUnitRenderActionFlags* flags,
                                const AudioTimeStamp* time, UInt32 frames, AudioBufferList* out)
{
    if (frames > MAUD_APPLE_OBJECT_SLICE)
    {
        return kAudioUnitErr_TooManyFramesToProcess;
    }
    maudPeriod* period = &objects->core->period;
    maudPullObjects(period, objects->bed, objects->records, frames);
    objects->frames = frames;
    for (uint32_t i = 0; i < period->objectCount; ++i)
    {
        Place(objects->mixer, i, &objects->records[i]);
    }
    UInt32 channels = period->channelCount;
    AudioBufferList* list = objects->mixedList;
    list->mNumberBuffers = channels;
    for (UInt32 c = 0; c < channels; ++c)
    {
        list->mBuffers[c] = (AudioBuffer){
            .mNumberChannels = 1,
            .mDataByteSize = frames * (UInt32)sizeof(float),
            .mData = objects->mixed + (size_t)c * MAUD_APPLE_OBJECT_SLICE,
        };
    }
    OSStatus status = AudioUnitRender(objects->mixer, flags, time, 0, frames, list);
    if (status != noErr)
    {
        return status;
    }
    float* interleaved = out->mBuffers[0].mData;
    for (UInt32 c = 0; c < channels; ++c)
    {
        const float* mixed = list->mBuffers[c].mData;
        for (UInt32 i = 0; i < frames; ++i)
        {
            interleaved[(size_t)i * channels + c] = mixed[i];
        }
    }
    return noErr;
}
