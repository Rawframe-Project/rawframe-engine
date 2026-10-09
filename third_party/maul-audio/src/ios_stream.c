// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Streams on iOS. Each stream has a RemoteIO unit, taking or giving
// 32-bit float interleaved frames at the stream's rate, which the unit
// converts to the session's. The unit's callback, on the system's IO
// thread, moves each buffer through the stream's fixed-period adapter.
// The session's category follows the streams that run (ios_session.m).

#include "ios_stream.h"

#include "clock.h"
#include "context.h"
#include "device.h"
#include "ios_core.h"
#include "ios_session.h"
#include "period.h"
#include "thread.h"
#include "voice.h"
#include "xrun.h"

#include <string.h>

// The most frames the unit may ask for in one render.
#define MAX_SLICE_FRAMES 4096u

static maudIosStream* EntryOf(maudContext* context, const maudStreamSlot* slot)
{
    maudIos* ios = context->native;
    return &ios->streams[slot - context->streams.slots];
}

// Counts skipped cycles: an IO cycle that starts past where the last one
// ended on the unit's sample clock.
static void CheckSampleTime(maudIosStream* entry, const AudioTimeStamp* time, UInt32 frames)
{
    if ((time->mFlags & kAudioTimeStampSampleTimeValid) == 0)
    {
        return;
    }
    if (entry->nextSampleTime >= 0.0 && time->mSampleTime > entry->nextSampleTime + 0.5)
    {
        maudCountXrun(entry->core);
    }
    entry->nextSampleTime = time->mSampleTime + frames;
}

// A buffer's host time, mach_absolute_time's units, in nanoseconds, or
// now when the time stamp has none.
static int64_t HostNanoseconds(const maudIos* ios, const AudioTimeStamp* time)
{
    if ((time->mFlags & kAudioTimeStampHostTimeValid) == 0)
    {
        return maudNowNanoseconds();
    }
    return (int64_t)((double)time->mHostTime * ios->timebaseNumer / ios->timebaseDenom);
}

// Fills the unit's buffer on the IO thread: the stream's frames while it
// runs, silence otherwise.
static OSStatus Render(void* user, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* time,
                       UInt32 bus, UInt32 frames, AudioBufferList* data)
{
    (void)bus;
    maudIosStream* entry = user;
    maudStreamCore* core = entry->core;
    const maudIos* ios = entry->owner;
    float* out = data->mBuffers[0].mData;
    CheckSampleTime(entry, time, frames);
    bool running = atomic_load_explicit(&core->state, memory_order_acquire) == maud_streamRunning;
    atomic_store_explicit(&core->renderingThread, maudCurrentThread(), memory_order_release);
    core->period.sampleRate = atomic_load_explicit(&core->blockRate, memory_order_acquire);
    OSStatus status = noErr;
    if (running && entry->objects.mixer != nullptr)
    {
        status = maudRenderAppleObjects(&entry->objects, flags, time, frames, data);
    }
    else if (running)
    {
        maudPullPeriod(&core->period, out, frames);
    }
    else
    {
        memset(out, 0, (size_t)frames * core->period.channelCount * sizeof(float));
        *flags |= kAudioUnitRenderAction_OutputIsSilence;
    }
    atomic_store_explicit(&core->renderingThread, 0, memory_order_release);
    // The buffer reaches the hardware at its host time; the session's
    // latency comes after that.
    int64_t latency = entry->sessionLatency + HostNanoseconds(ios, time) - maudNowNanoseconds();
    maudStampOutputClock(core, latency > 0 ? latency : 0);
    atomic_fetch_add_explicit(&core->position, frames, memory_order_release);
    return status;
}

// Spreads frames of `from` channels over `to`, in place, back to front
// so that nothing is overwritten before it is read; each channel past the
// unit's takes its last one.
static void Spread(float* data, UInt32 frames, UInt32 from, UInt32 to)
{
    for (UInt32 i = frames; i-- > 0;)
    {
        for (UInt32 c = to; c-- > 0;)
        {
            data[(size_t)i * to + c] = data[(size_t)i * from + (c < from ? c : from - 1)];
        }
    }
}

// Takes an input buffer on the IO thread and pushes it to the stream
// while it runs. The buffer's host time is its first frame's at the
// hardware; the session's input latency came before it.
static OSStatus Capture(void* user, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* time,
                        UInt32 bus, UInt32 frames, AudioBufferList* unused)
{
    (void)unused;
    maudIosStream* entry = user;
    maudStreamCore* core = entry->core;
    const maudIos* ios = entry->owner;
    AudioBufferList* list = entry->captured;
    UInt32 channels = core->period.channelCount;
    UInt32 captured = entry->unitChannels != 0 ? entry->unitChannels : channels;
    UInt32 frameBytes = captured * (UInt32)sizeof(float);
    if (frames > MAX_SLICE_FRAMES)
    {
        return kAudioUnitErr_TooManyFramesToProcess;
    }
    CheckSampleTime(entry, time, frames);
    float* data = list->mBuffers[0].mData;
    list->mBuffers[0].mDataByteSize = MAX_SLICE_FRAMES * frameBytes;
    memset(data, 0, (size_t)frames * frameBytes);
    OSStatus status = AudioUnitRender(entry->unit, flags, time, bus, frames, list);
    if (status != noErr)
    {
        return status;
    }
    if (captured < channels)
    {
        Spread(data, frames, captured, channels);
    }
    bool running = atomic_load_explicit(&core->state, memory_order_acquire) == maud_streamRunning;
    atomic_store_explicit(&core->renderingThread, maudCurrentThread(), memory_order_release);
    core->period.sampleRate = atomic_load_explicit(&core->blockRate, memory_order_acquire);
    if (running)
    {
        maudPushPeriod(&core->period, data, frames);
    }
    atomic_store_explicit(&core->renderingThread, 0, memory_order_release);
    int64_t latency = entry->sessionLatency + maudNowNanoseconds() - HostNanoseconds(ios, time);
    maudStampInputClock(core, latency > 0 ? latency : 0);
    atomic_fetch_add_explicit(&core->position, frames, memory_order_release);
    return noErr;
}

// The client side's format: 32-bit float interleaved frames at the
// stream's rate.
static AudioStreamBasicDescription FormatWith(const maudStreamCore* core, UInt32 channels)
{
    return (AudioStreamBasicDescription){
        .mSampleRate = core->format.sampleRate,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
        .mBytesPerPacket = channels * (UInt32)sizeof(float),
        .mFramesPerPacket = 1,
        .mBytesPerFrame = channels * (UInt32)sizeof(float),
        .mChannelsPerFrame = channels,
        .mBitsPerChannel = 32,
    };
}

static AudioStreamBasicDescription FormatOf(const maudStreamCore* core)
{
    return FormatWith(core, core->period.channelCount);
}

// Sets the unit's direction, client format, callback and slice size.
static bool Configure(maudIosStream* entry)
{
    const maudStreamCore* core = entry->core;
    AudioStreamBasicDescription format = FormatOf(core);
    UInt32 slice = MAX_SLICE_FRAMES;
    AudioUnit unit = entry->unit;
    if (core->def.direction == maud_directionInput)
    {
        // Input on bus 1 only; the frames come out of its output scope.
        UInt32 on = 1;
        UInt32 off = 0;
        AURenderCallbackStruct callback = {.inputProc = Capture, .inputProcRefCon = entry};
        return AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input,
                                    1, &on, sizeof(on)) == noErr &&
               AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output,
                                    0, &off, sizeof(off)) == noErr &&
               AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output,
                                    1, &format, sizeof(format)) == noErr &&
               AudioUnitSetProperty(unit, kAudioOutputUnitProperty_SetInputCallback,
                                    kAudioUnitScope_Global, 0, &callback,
                                    sizeof(callback)) == noErr &&
               AudioUnitSetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice,
                                    kAudioUnitScope_Global, 0, &slice, sizeof(slice)) == noErr;
    }
    AURenderCallbackStruct callback = {.inputProc = Render, .inputProcRefCon = entry};
    return AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0,
                                &format, sizeof(format)) == noErr &&
           AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input,
                                0, &callback, sizeof(callback)) == noErr &&
           AudioUnitSetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice,
                                kAudioUnitScope_Global, 0, &slice, sizeof(slice)) == noErr;
}

static void Disconnect(maudContext* context, maudIosStream* entry)
{
    if (entry->unit != nullptr)
    {
        if (entry->playing)
        {
            OSStatus stopped = AudioOutputUnitStop(entry->unit);
            (void)stopped;
        }
        OSStatus uninitialized = AudioUnitUninitialize(entry->unit);
        OSStatus disposed = AudioComponentInstanceDispose(entry->unit);
        (void)uninitialized;
        (void)disposed;
    }
    if (entry->captured != nullptr)
    {
        maudContextRelease(context, entry->captured, entry->capturedBytes,
                           alignof(AudioBufferList));
    }
    if (entry->voicePartner != nullptr)
    {
        entry->voicePartner->voicePartner = nullptr;
    }
    // The unit is gone, so the mixer renders no more.
    maudCloseAppleObjects(context, &entry->objects);
    maudStreamCore* core = entry->core;
    const maudIos* owner = entry->owner;
    *entry = (maudIosStream){.core = core, .owner = owner};
}

// One buffer of interleaved frames for an input, after the list's
// header.
static bool AllocateCaptured(maudContext* context, maudIosStream* entry)
{
    uint32_t channels = entry->core->period.channelCount;
    size_t data = (size_t)MAX_SLICE_FRAMES * channels * sizeof(float);
    entry->capturedBytes = sizeof(AudioBufferList) + data;
    entry->captured = maudContextAllocate(context, entry->capturedBytes, alignof(AudioBufferList));
    if (entry->captured == nullptr)
    {
        return false;
    }
    entry->captured->mNumberBuffers = 1;
    entry->captured->mBuffers[0] = (AudioBuffer){
        .mNumberChannels = channels,
        .mDataByteSize = (UInt32)data,
        .mData = entry->captured + 1,
    };
    return true;
}

// Points the session's preferred input at the port a pinned input
// stream is on; a stream on the default leaves it. false when the port
// is no longer available.
static bool PreferPort(maudContext* context, const maudStreamCore* core)
{
    const maudDeviceSlot* device = maudFindDevice(context, core->binding.current);
    static const char prefix[] = "port:";
    size_t skip = sizeof(prefix) - 1;
    if (core->def.direction != maud_directionInput || device == nullptr ||
        device->key.length <= skip || memcmp(device->key.bytes, prefix, skip) != 0)
    {
        return true;
    }
    return maudIosSessionPreferInput(device->key.bytes + skip, device->key.length - skip);
}

static maudResult Connect(maudContext* context, maudIosStream* entry)
{
    AudioComponentDescription description = {
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_RemoteIO,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
    };
    AudioComponent component = AudioComponentFindNext(nullptr, &description);
    if (component == nullptr || AudioComponentInstanceNew(component, &entry->unit) != noErr)
    {
        entry->unit = nullptr;
        return maud_errorPlatform;
    }
    bool input = entry->core->def.direction == maud_directionInput;
    if (input && !AllocateCaptured(context, entry))
    {
        return maud_errorCapacity;
    }
    // The session takes the stream's direction before its unit
    // initializes.
    bool initialized = Configure(entry) && maudIosUpdateSession(context, true) &&
                       PreferPort(context, entry->core) &&
                       AudioUnitInitialize(entry->unit) == noErr;
    // Back to what runs: an initialized unit outlives the session's
    // deactivation, as it does an interruption's.
    bool settled = maudIosUpdateSession(context, false);
    (void)settled;
    if (!initialized)
    {
        return maud_errorPlatform;
    }
    entry->sessionLatency = maudIosSessionLatency(input);
    // RemoteIO processes nothing; only a voiced duplex stream runs on
    // Voice-Processing I/O.
    if (input)
    {
        maudReportVoice(entry->core, maud_voiceNone);
    }
    // An object stream renders through the spatial mixer, for what the
    // route leads to.
    if (entry->core->def.objectCount > 0)
    {
        const maudDeviceSlot* device = maudFindDevice(context, entry->core->binding.current);
        return maudAppleObjectsFit(entry->core)
                   ? maudOpenAppleObjects(context, entry->core,
                                          device != nullptr ? device->info.form : maud_formUnknown,
                                          &entry->objects)
                   : maud_errorUnsupported;
    }
    return maud_success;
}

// Whether a stream is a half of a duplex stream that asks for voice
// processing, whose halves run on one Voice-Processing I/O unit so that
// it cancels the echo of what it plays. An input alone runs on RemoteIO,
// which processes nothing.
static bool Voiced(const maudStreamCore* core)
{
    return core->duplexGroup != 0 && core->def.voice != maud_voiceNone;
}

// The other half of a duplex stream: the live slot of the same group.
static maudStreamSlot* PartnerOf(maudContext* context, const maudStreamCore* core)
{
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        maudStreamSlot* slot = &context->streams.slots[i];
        if (slot->live && &slot->core != core && slot->core.duplexGroup == core->duplexGroup)
        {
            return slot;
        }
    }
    return nullptr;
}

// Sets the voice-processing unit's client formats, callbacks, processing
// and slice size: bus 0 plays the output half, bus 1 captures the input
// half, one channel (Capture spreads it).
static bool ConfigureVoice(maudIosStream* input, maudIosStream* output)
{
    AudioUnit unit = input->unit;
    AudioStreamBasicDescription outputFormat = FormatOf(output->core);
    AudioStreamBasicDescription inputFormat = FormatWith(input->core, 1);
    AURenderCallbackStruct render = {.inputProc = Render, .inputProcRefCon = output};
    AURenderCallbackStruct capture = {.inputProc = Capture, .inputProcRefCon = input};
    UInt32 on = 1;
    UInt32 bypass = 0;
    UInt32 gain = (input->core->def.voice & maud_voiceGainControl) != 0 ? 1u : 0u;
    UInt32 slice = MAX_SLICE_FRAMES;
    return AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1,
                                &on, sizeof(on)) == noErr &&
           AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0,
                                &outputFormat, sizeof(outputFormat)) == noErr &&
           AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1,
                                &inputFormat, sizeof(inputFormat)) == noErr &&
           AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input,
                                0, &render, sizeof(render)) == noErr &&
           AudioUnitSetProperty(unit, kAudioOutputUnitProperty_SetInputCallback,
                                kAudioUnitScope_Global, 1, &capture, sizeof(capture)) == noErr &&
           AudioUnitSetProperty(unit, kAUVoiceIOProperty_BypassVoiceProcessing,
                                kAudioUnitScope_Global, 0, &bypass, sizeof(bypass)) == noErr &&
           AudioUnitSetProperty(unit, kAUVoiceIOProperty_VoiceProcessingEnableAGC,
                                kAudioUnitScope_Global, 0, &gain, sizeof(gain)) == noErr &&
           AudioUnitSetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice,
                                kAudioUnitScope_Global, 0, &slice, sizeof(slice)) == noErr;
}

// Reports the unit's processing: echo cancellation and noise
// suppression while it is not bypassed, and gain control as it reads.
static void ReportUnitVoice(maudIosStream* input)
{
    UInt32 gain = 0;
    UInt32 size = sizeof(gain);
    OSStatus status = AudioUnitGetProperty(input->unit, kAUVoiceIOProperty_VoiceProcessingEnableAGC,
                                           kAudioUnitScope_Global, 0, &gain, &size);
    maudVoiceProcessing active = maud_voiceEchoCancellation | maud_voiceNoiseSuppression;
    active |= status == noErr && gain != 0 ? maud_voiceGainControl : maud_voiceNone;
    maudReportVoice(input->core, active);
}

// Makes and initializes the voice-processing unit of a voiced duplex
// stream from its input half, for both halves, in the session's voice
// chat mode.
static maudResult ConnectVoice(maudContext* context, maudIosStream* input, maudIosStream* output)
{
    AudioComponentDescription description = {
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_VoiceProcessingIO,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
    };
    AudioComponent component = AudioComponentFindNext(nullptr, &description);
    if (component == nullptr || AudioComponentInstanceNew(component, &input->unit) != noErr)
    {
        input->unit = nullptr;
        return maud_errorPlatform;
    }
    input->voicePartner = output;
    output->voicePartner = input;
    if (!AllocateCaptured(context, input))
    {
        return maud_errorCapacity;
    }
    input->unitChannels = 1;
    input->captured->mBuffers[0].mNumberChannels = 1;
    bool initialized = ConfigureVoice(input, output) && maudIosUpdateSession(context, true) &&
                       AudioUnitInitialize(input->unit) == noErr;
    bool settled = maudIosUpdateSession(context, false);
    (void)settled;
    if (!initialized)
    {
        return maud_errorPlatform;
    }
    output->sessionLatency = maudIosSessionLatency(false);
    input->sessionLatency = maudIosSessionLatency(true);
    ReportUnitVoice(input);
    return maud_success;
}

// A voiced half: the output half waits for the input half, which builds
// the unit for both once the output half is live.
static maudResult OpenVoiced(maudContext* context, maudStreamSlot* slot)
{
    maudStreamSlot* partner = PartnerOf(context, &slot->core);
    if (slot->core.def.direction == maud_directionOutput || partner == nullptr)
    {
        return maud_success;
    }
    maudIosStream* entry = EntryOf(context, slot);
    maudResult result = ConnectVoice(context, entry, EntryOf(context, partner));
    if (result != maud_success)
    {
        Disconnect(context, entry);
    }
    return result;
}

maudResult maudIosAttachStream(maudContext* context, maudStreamSlot* slot)
{
    maudIosStream* entry = EntryOf(context, slot);
    *entry = (maudIosStream){.core = &slot->core, .owner = context->native};
    maudResetVoice(&slot->core);
    if (Voiced(&slot->core))
    {
        return OpenVoiced(context, slot);
    }
    maudResult result = Connect(context, entry);
    if (result != maud_success)
    {
        Disconnect(context, entry);
    }
    return result;
}

void maudIosDetachStream(maudContext* context, maudStreamSlot* slot)
{
    maudIosStream* entry = EntryOf(context, slot);
    // The unit plays the output half's frames: it goes before they do.
    if (entry->unit == nullptr && entry->voicePartner != nullptr)
    {
        Disconnect(context, entry->voicePartner);
    }
    Disconnect(context, entry);
    bool updated = maudIosUpdateSession(context, false);
    (void)updated;
}

// The category follows every stream that has a unit, including one
// being attached (its slot goes live after); activation, the streams
// that play, and the unit being made. A unit exists only between attach
// and detach.
bool maudIosUpdateSession(maudContext* context, bool preparing)
{
    maudIosUse use = {.running = preparing};
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        maudStreamSlot* slot = &context->streams.slots[i];
        const maudIosStream* entry = EntryOf(context, slot);
        if (entry->unit != nullptr)
        {
            bool input = slot->core.def.direction == maud_directionInput;
            // A voiced unit plays and captures both.
            bool voiced = entry->voicePartner != nullptr;
            use.inputs = use.inputs || input;
            use.outputs = use.outputs || !input || voiced;
            use.voiced = use.voiced || voiced;
            use.running = use.running || entry->playing;
        }
    }
    return maudIosSessionUpdate(context->native, use);
}

void maudIosSetStreamActive(maudContext* context, maudStreamSlot* slot, bool active)
{
    // A voiced duplex stream's unit starts and stops with its input half;
    // the output half renders silence whenever it is not running.
    if (Voiced(&slot->core) && slot->core.def.direction == maud_directionOutput)
    {
        return;
    }
    maudIosStream* entry = EntryOf(context, slot);
    if (entry->unit == nullptr || entry->playing == active)
    {
        return;
    }
    if (!active)
    {
        OSStatus stopped = AudioOutputUnitStop(entry->unit);
        (void)stopped;
        entry->playing = false;
        bool updated = maudIosUpdateSession(context, false);
        (void)updated;
        return;
    }
    // The session takes the stream's direction before the unit starts.
    entry->playing = true;
    entry->nextSampleTime = -1.0;
    entry->playing =
        maudIosUpdateSession(context, false) && AudioOutputUnitStart(entry->unit) == noErr;
}
