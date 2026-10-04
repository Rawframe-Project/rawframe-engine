// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The CoreAudio backend's context and devices. Devices are the HAL's
// devices that are not hidden, one per direction with channels on it,
// keyed by UID. The HAL reports changes to the device list and the
// defaults through a listener block on a queue the context owns; the
// block raises a flag and the drain lists devices and defaults again.

#include "backend.h"
#include "context.h"
#include "coreaudio_core.h"
#include "coreaudio_form.h"
#include "coreaudio_stream.h"
#include "device.h"
#include "layout.h"

#include <Block.h>
#include <string.h>

// The properties whose changes the context listens to, on the system
// object.
static const AudioObjectPropertySelector s_watched[] = {
    kAudioHardwarePropertyDevices,
    kAudioHardwarePropertyDefaultOutputDevice,
    kAudioHardwarePropertyDefaultInputDevice,
};

#define WATCHED_COUNT (sizeof(s_watched) / sizeof(s_watched[0]))

// Copies a string property of object as UTF-8 into out; false when it
// is missing or does not fit.
static bool ReadText(AudioObjectID object, AudioObjectPropertySelector selector, char* out,
                     size_t capacity)
{
    AudioObjectPropertyAddress address =
        maudCoreAudioAddress(selector, kAudioObjectPropertyScopeGlobal);
    CFStringRef text = nullptr;
    UInt32 size = sizeof(text);
    if (AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, (void*)&text) != noErr ||
        text == nullptr)
    {
        return false;
    }
    bool ok = CFStringGetCString(text, out, (CFIndex)capacity, kCFStringEncodingUTF8);
    CFRelease(text);
    return ok;
}

// Reads a variable-size property into the scratch buffer; returns its
// size, or 0 when it is missing or too large.
static UInt32 ReadScratch(maudCoreAudio* coreaudio, AudioObjectID object,
                          const AudioObjectPropertyAddress* address)
{
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(object, address, 0, nullptr, &size) != noErr ||
        size > MAUD_COREAUDIO_SCRATCH_BYTES)
    {
        return 0;
    }
    return AudioObjectGetPropertyData(object, address, 0, nullptr, &size, coreaudio->scratch) ==
                   noErr
               ? size
               : 0;
}

// The channels a device has in one scope, over all its streams.
static uint32_t ChannelsIn(maudCoreAudio* coreaudio, AudioObjectID object,
                           AudioObjectPropertyScope scope)
{
    AudioObjectPropertyAddress address =
        maudCoreAudioAddress(kAudioDevicePropertyStreamConfiguration, scope);
    if (ReadScratch(coreaudio, object, &address) < sizeof(AudioBufferList))
    {
        return 0;
    }
    const AudioBufferList* list = (const AudioBufferList*)(void*)coreaudio->scratch;
    uint32_t channels = 0;
    for (UInt32 i = 0; i < list->mNumberBuffers; ++i)
    {
        channels += list->mBuffers[i].mNumberChannels;
    }
    return channels;
}

// Reads the nominal rate into info, and the bounds of the rates the
// device may run at.
static void ReadRates(maudCoreAudio* coreaudio, AudioObjectID object, maudDeviceInfo* info)
{
    AudioObjectPropertyAddress address = maudCoreAudioAddress(kAudioDevicePropertyNominalSampleRate,
                                                              kAudioObjectPropertyScopeGlobal);
    Float64 nominal = 0.0;
    UInt32 size = sizeof(nominal);
    if (AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, &nominal) != noErr ||
        nominal < 1.0)
    {
        return;
    }
    uint32_t rate = (uint32_t)(nominal + 0.5);
    info->nativeSampleRate = rate;
    info->minSampleRate = rate;
    info->maxSampleRate = rate;
    address.mSelector = kAudioDevicePropertyAvailableNominalSampleRates;
    UInt32 bytes = ReadScratch(coreaudio, object, &address);
    const AudioValueRange* ranges = (const AudioValueRange*)(void*)coreaudio->scratch;
    for (UInt32 i = 0; i < bytes / sizeof(AudioValueRange); ++i)
    {
        uint32_t low = (uint32_t)(ranges[i].mMinimum + 0.5);
        uint32_t high = (uint32_t)(ranges[i].mMaximum + 0.5);
        info->minSampleRate = low < info->minSampleRate ? low : info->minSampleRate;
        info->maxSampleRate = high > info->maxSampleRate ? high : info->maxSampleRate;
    }
}

static bool Hidden(AudioObjectID object)
{
    AudioObjectPropertyAddress address =
        maudCoreAudioAddress(kAudioDevicePropertyIsHidden, kAudioObjectPropertyScopeGlobal);
    UInt32 hidden = 0;
    UInt32 size = sizeof(hidden);
    return AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, &hidden) == noErr &&
           hidden != 0;
}

// Describes one direction of a device into endpoint and spec; false
// when the device has no channels that way or no UID.
static bool Describe(maudCoreAudio* coreaudio, AudioObjectID object, maudDirection direction,
                     maudCoreAudioEndpoint* endpoint, maudDeviceSpec* spec)
{
    AudioObjectPropertyScope scope = direction == maud_directionOutput
                                         ? kAudioObjectPropertyScopeOutput
                                         : kAudioObjectPropertyScopeInput;
    uint32_t channels = ChannelsIn(coreaudio, object, scope);
    if (channels == 0 ||
        !ReadText(object, kAudioDevicePropertyDeviceUID, endpoint->key, sizeof(endpoint->key)))
    {
        return false;
    }
    if (!ReadText(object, kAudioObjectPropertyName, endpoint->name, sizeof(endpoint->name)))
    {
        memcpy(endpoint->name, endpoint->key, sizeof(endpoint->key));
    }
    endpoint->object = object;
    *spec = (maudDeviceSpec){
        .info = {.direction = direction, .nativeLayout = maudLayoutWithChannels(channels)},
        .name = endpoint->name,
        .nameLength = maudCutUtf8(endpoint->name, coreaudio->context->def.limits.deviceTextBytes),
        .key = endpoint->key,
        .keyLength = strlen(endpoint->key),
    };
    ReadRates(coreaudio, object, &spec->info);
    spec->info.form = maudCoreAudioFormOf(object, direction);
    return true;
}

// Lists the HAL's devices into the scan arrays; returns the count.
static uint32_t Scan(maudCoreAudio* coreaudio)
{
    AudioObjectPropertyAddress address =
        maudCoreAudioAddress(kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal);
    UInt32 size = coreaudio->objectCapacity * (UInt32)sizeof(AudioObjectID);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size,
                                   coreaudio->objects) != noErr)
    {
        return 0;
    }
    uint32_t limit = coreaudio->context->def.limits.devices;
    uint32_t count = 0;
    for (UInt32 i = 0; i < size / sizeof(AudioObjectID); ++i)
    {
        AudioObjectID object = coreaudio->objects[i];
        for (int pass = 0; pass < 2 && count < limit && !Hidden(object); ++pass)
        {
            maudDirection direction = pass == 0 ? maud_directionOutput : maud_directionInput;
            count += Describe(coreaudio, object, direction, &coreaudio->endpoints[count],
                              &coreaudio->specs[count])
                         ? 1u
                         : 0u;
        }
    }
    return count;
}

// Points both roles' default of one direction at the system's.
static void ReadDefault(maudCoreAudio* coreaudio, maudDirection direction)
{
    AudioObjectPropertyAddress address = maudCoreAudioAddress(
        direction == maud_directionOutput ? kAudioHardwarePropertyDefaultOutputDevice
                                          : kAudioHardwarePropertyDefaultInputDevice,
        kAudioObjectPropertyScopeGlobal);
    AudioObjectID object = kAudioObjectUnknown;
    UInt32 size = sizeof(object);
    char key[MAUD_COREAUDIO_KEY_BYTES];
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size,
                                   &object) != noErr ||
        object == kAudioObjectUnknown ||
        !ReadText(object, kAudioDevicePropertyDeviceUID, key, sizeof(key)))
    {
        return;
    }
    maudDeviceId id = maudFindDeviceByKey(coreaudio->context, direction, key, strlen(key));
    if (id.index1 != 0)
    {
        maudSetDefaultDevice(coreaudio->context, maud_roleGeneral, id);
        maudSetDefaultDevice(coreaudio->context, maud_roleCommunications, id);
    }
}

static maudResult Rescan(maudCoreAudio* coreaudio)
{
    uint32_t count = Scan(coreaudio);
    maudResult result = maudSyncDevices(coreaudio->context, coreaudio->specs, count, nullptr);
    maudCoreAudioWatchSources(coreaudio, count);
    ReadDefault(coreaudio, maud_directionOutput);
    ReadDefault(coreaudio, maud_directionInput);
    return result;
}

// Starts listening to the device list and the defaults.
static void Listen(maudCoreAudio* coreaudio)
{
    atomic_bool* changed = &coreaudio->changed;
    coreaudio->listener = Block_copy(^(UInt32 count, const AudioObjectPropertyAddress* addresses) {
      (void)count;
      (void)addresses;
      atomic_store_explicit(changed, true, memory_order_release);
    });
    coreaudio->listening = true;
    for (size_t i = 0; i < WATCHED_COUNT; ++i)
    {
        AudioObjectPropertyAddress address =
            maudCoreAudioAddress(s_watched[i], kAudioObjectPropertyScopeGlobal);
        coreaudio->listening =
            coreaudio->listening &&
            AudioObjectAddPropertyListenerBlock(kAudioObjectSystemObject, &address,
                                                coreaudio->queue, coreaudio->listener) == noErr;
    }
}

// Removes the listener blocks and waits out one in flight on the queue.
static void StopListening(maudCoreAudio* coreaudio)
{
    if (coreaudio->listener == nullptr)
    {
        return;
    }
    maudCoreAudioUnwatchSources(coreaudio);
    for (size_t i = 0; i < WATCHED_COUNT; ++i)
    {
        AudioObjectPropertyAddress address =
            maudCoreAudioAddress(s_watched[i], kAudioObjectPropertyScopeGlobal);
        OSStatus status = AudioObjectRemovePropertyListenerBlock(
            kAudioObjectSystemObject, &address, coreaudio->queue, coreaudio->listener);
        (void)status;
    }
    dispatch_sync(coreaudio->queue, ^{
                  });
    Block_release(coreaudio->listener);
    coreaudio->listener = nullptr;
}

static void Release(maudContext* context, maudCoreAudio* coreaudio)
{
    StopListening(coreaudio);
    if (coreaudio->queue != nullptr)
    {
        dispatch_release(coreaudio->queue);
    }
    maudContextRelease(context, coreaudio, coreaudio->bytes, alignof(maudCoreAudio));
    context->native = nullptr;
}

// Carves the context's block: the state, then the scan arrays, the
// scratch buffer and the streams.
static maudCoreAudio* Allocate(maudContext* context)
{
    uint32_t devices = context->def.limits.devices;
    uint32_t streams = context->def.limits.streams;
    uint32_t objects = 2 * devices + 16;
    size_t bytes = sizeof(maudCoreAudio) + (size_t)objects * sizeof(AudioObjectID) +
                   (size_t)devices * (sizeof(maudCoreAudioEndpoint) + sizeof(maudDeviceSpec) +
                                      sizeof(maudCoreAudioWatch)) +
                   (size_t)streams * sizeof(maudCoreAudioStream) + MAUD_COREAUDIO_SCRATCH_BYTES +
                   alignof(max_align_t);
    maudCoreAudio* coreaudio = maudContextAllocate(context, bytes, alignof(maudCoreAudio));
    if (coreaudio == nullptr)
    {
        return nullptr;
    }
    *coreaudio = (maudCoreAudio){
        .context = context, .objectCapacity = objects, .watchCapacity = devices, .bytes = bytes};
    coreaudio->streams = (maudCoreAudioStream*)(coreaudio + 1);
    coreaudio->specs = (maudDeviceSpec*)(coreaudio->streams + streams);
    coreaudio->endpoints = (maudCoreAudioEndpoint*)(coreaudio->specs + devices);
    coreaudio->watched = (maudCoreAudioWatch*)(coreaudio->endpoints + devices);
    coreaudio->objects = (AudioObjectID*)(coreaudio->watched + devices);
    uintptr_t scratch = (uintptr_t)(coreaudio->objects + objects);
    scratch = (scratch + alignof(max_align_t) - 1) & ~(uintptr_t)(alignof(max_align_t) - 1);
    coreaudio->scratch = (unsigned char*)scratch;
    memset(coreaudio->streams, 0, (size_t)streams * sizeof(maudCoreAudioStream));
    return coreaudio;
}

static maudResult OpenContext(maudContext* context)
{
    maudCoreAudio* coreaudio = Allocate(context);
    if (coreaudio == nullptr)
    {
        return maud_errorCapacity;
    }
    context->native = coreaudio;
    atomic_init(&coreaudio->changed, false);
    coreaudio->queue = dispatch_queue_create("maud-coreaudio", DISPATCH_QUEUE_SERIAL);
    if (coreaudio->queue == nullptr)
    {
        Release(context, coreaudio);
        return maud_errorPlatform;
    }
    Listen(coreaudio);
    maudResult result = Rescan(coreaudio);
    if (result != maud_success)
    {
        Release(context, coreaudio);
    }
    return result;
}

static void CloseContext(maudContext* context)
{
    Release(context, context->native);
}

static void Pump(maudContext* context)
{
    maudCoreAudio* coreaudio = context->native;
    if (atomic_exchange_explicit(&coreaudio->changed, false, memory_order_acq_rel))
    {
        maudResult result = Rescan(coreaudio);
        (void)result;
    }
    maudCoreAudioResumeStreams(context);
}

// AUHAL runs at the device's nominal rate: a native stream takes it, a
// required rate must be it, and a platform-converted output may differ;
// its input side cannot convert the rate (TN2091), so a converted input
// is refused. With no device yet, the rate is taken as 48 kHz.
static maudResult OpenStream(const maudContext* context, const maudStreamDef* def,
                             const maudDeviceInfo* device, maudStreamFormat* formatOut)
{
    (void)context;
    uint32_t nominal =
        device != nullptr && device->nativeSampleRate != 0 ? device->nativeSampleRate : 48000;
    bool input = def->direction == maud_directionInput;
    if (def->mode == maud_modePull ||
        (input && def->ratePolicy == maud_ratePlatformConverted && def->sampleRate != nominal) ||
        (def->ratePolicy == maud_rateRequired && def->sampleRate != nominal))
    {
        return maud_errorUnsupported;
    }
    uint32_t rate = def->ratePolicy == maud_rateNative ? nominal : def->sampleRate;
    *formatOut = (maudStreamFormat){
        .sampleRate = rate,
        .periodFrames = def->periodFrames != 0 ? def->periodFrames : rate / 100,
        .layout = def->layout,
        .ratePolicy = def->ratePolicy,
    };
    return maud_success;
}

// A HAL device has one clock: the halves of a duplex stream share it
// when both are on one device, which their keys, the device's UID, say,
// or when they share a voice-processing unit, which runs both.
static bool SharesClock(const maudContext* context, const maudStreamSlot* output,
                        const maudStreamSlot* input)
{
    const maudCoreAudio* coreaudio = context->native;
    if (coreaudio->streams[output - context->streams.slots].voicePartner != nullptr)
    {
        return true;
    }
    const maudDeviceSlot* played = maudFindDevice(context, output->core.binding.current);
    const maudDeviceSlot* captured = maudFindDevice(context, input->core.binding.current);
    return played != nullptr && captured != nullptr && played->key.length == captured->key.length &&
           memcmp(played->key.bytes, captured->key.bytes, played->key.length) == 0;
}

static const maudBackend s_coreaudio = {
    .kind = maud_backendCoreAudio,
    .openContext = OpenContext,
    .closeContext = CloseContext,
    .pump = Pump,
    .openStream = OpenStream,
    .exclusive = true,
    .attachStream = maudCoreAudioAttachStream,
    .detachStream = maudCoreAudioDetachStream,
    .setStreamActive = maudCoreAudioSetStreamActive,
    .retargetStream = maudCoreAudioRetargetStream,
    .reopensOnMove = true,
    .sharesClock = SharesClock,
    .rendersOnCaller = false,
};

const maudBackend* maudGetCoreAudioBackend(void)
{
    return &s_coreaudio;
}
