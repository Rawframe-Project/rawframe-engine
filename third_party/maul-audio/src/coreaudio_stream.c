// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Streams on CoreAudio. Each stream has an AUHAL output unit on its
// device, taking 32-bit float interleaved frames at the stream's rate on
// its input scope and converting them to the device's format. The
// unit's render callback, on the HAL's IO thread, moves each buffer
// through the stream's fixed-period adapter. Channels go to the
// device's channels in order. A duplex stream that asks for voice
// processing runs both halves on one Voice-Processing I/O unit instead:
// bus 0 plays the output half on its device, bus 1 captures the input
// half from its device, and the unit takes what it plays out of what it
// hears.

#include "coreaudio_stream.h"

#include "clock.h"
#include "context.h"
#include "coreaudio_core.h"
#include "coreaudio_hog.h"
#include "period.h"
#include "thread.h"
#include "voice.h"
#include "xrun.h"

#include <string.h>

// The most frames the unit may ask for in one render.
#define MAX_SLICE_FRAMES 4096u

static maudCoreAudioStream* EntryOf(maudContext* context, const maudStreamSlot* slot)
{
    maudCoreAudio* coreaudio = context->native;
    return &coreaudio->streams[slot - context->streams.slots];
}

// Counts skipped cycles: an IO cycle that starts past where the last one
// ended on the device's sample clock.
static void CheckSampleTime(maudCoreAudioStream* entry, const AudioTimeStamp* time, UInt32 frames)
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

// Fills the unit's buffer on the IO thread: the stream's frames while it
// runs, silence otherwise.
static OSStatus Render(void* user, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* time,
                       UInt32 bus, UInt32 frames, AudioBufferList* data)
{
    (void)bus;
    maudCoreAudioStream* entry = user;
    maudStreamCore* core = entry->core;
    float* out = data->mBuffers[0].mData;
    CheckSampleTime(entry, time, frames);
    bool running = atomic_load_explicit(&core->state, memory_order_acquire) == maud_streamRunning;
    atomic_store_explicit(&core->renderingThread, maudCurrentThread(), memory_order_release);
    core->period.sampleRate = atomic_load_explicit(&core->blockRate, memory_order_acquire);
    if (running)
    {
        maudPullPeriod(&core->period, out, frames);
    }
    else
    {
        memset(out, 0, (size_t)frames * core->period.channelCount * sizeof(float));
        *flags |= kAudioUnitRenderAction_OutputIsSilence;
    }
    atomic_store_explicit(&core->renderingThread, 0, memory_order_release);
    // The buffer reaches the device at its host time; the device adds
    // its own latency after that.
    int64_t latency = entry->deviceLatency;
    if ((time->mFlags & kAudioTimeStampHostTimeValid) != 0)
    {
        latency += (int64_t)AudioConvertHostTimeToNanos(time->mHostTime) - maudNowNanoseconds();
    }
    maudStampOutputClock(core, latency);
    atomic_fetch_add_explicit(&core->position, frames, memory_order_release);
    return noErr;
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
// while it runs. The buffer's host time is the first frame's at the
// device; the device's own latency came before it.
static OSStatus Capture(void* user, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* time,
                        UInt32 bus, UInt32 frames, AudioBufferList* unused)
{
    (void)unused;
    maudCoreAudioStream* entry = user;
    maudStreamCore* core = entry->core;
    AudioBufferList* list = entry->captured;
    UInt32 capacity = MAX_SLICE_FRAMES * core->period.channelCount * (UInt32)sizeof(float);
    if (frames > MAX_SLICE_FRAMES)
    {
        return kAudioUnitErr_TooManyFramesToProcess;
    }
    CheckSampleTime(entry, time, frames);
    // A render leaves the size at what it brought; the next asks for room,
    // over silence, so what the unit leaves unwritten is never old bytes.
    UInt32 channels = core->period.channelCount;
    UInt32 captured = entry->unitChannels != 0 ? entry->unitChannels : channels;
    UInt32 frameBytes = captured * (UInt32)sizeof(float);
    float* data = list->mBuffers[0].mData;
    list->mBuffers[0].mDataByteSize = capacity;
    memset(data, 0, (size_t)frames * frameBytes);
    OSStatus status = AudioUnitRender(entry->unit, flags, time, bus, frames, list);
    if (status != noErr)
    {
        return status;
    }
    // A short render's frames past its size are silence too.
    UInt32 written = list->mBuffers[0].mDataByteSize / frameBytes;
    if (written < frames)
    {
        memset(data + (size_t)written * captured, 0, (size_t)(frames - written) * frameBytes);
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
    int64_t latency = entry->deviceLatency;
    if ((time->mFlags & kAudioTimeStampHostTimeValid) != 0)
    {
        latency += maudNowNanoseconds() - (int64_t)AudioConvertHostTimeToNanos(time->mHostTime);
    }
    maudStampInputClock(core, latency);
    atomic_fetch_add_explicit(&core->position, frames, memory_order_release);
    return noErr;
}

// The HAL object of the device whose UID is the stream's device key, or
// kAudioObjectUnknown.
static AudioObjectID ObjectOf(maudContext* context, const maudStreamCore* core)
{
    const maudDeviceSlot* device = maudFindDevice(context, core->binding.current);
    if (device == nullptr)
    {
        return kAudioObjectUnknown;
    }
    CFStringRef uid =
        CFStringCreateWithBytes(kCFAllocatorDefault, (const UInt8*)device->key.bytes,
                                (CFIndex)device->key.length, kCFStringEncodingUTF8, false);
    if (uid == nullptr)
    {
        return kAudioObjectUnknown;
    }
    AudioObjectPropertyAddress address = maudCoreAudioAddress(
        kAudioHardwarePropertyTranslateUIDToDevice, kAudioObjectPropertyScopeGlobal);
    AudioObjectID object = kAudioObjectUnknown;
    UInt32 size = sizeof(object);
    OSStatus status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, sizeof(uid),
                                                 (const void*)&uid, &size, &object);
    CFRelease(uid);
    return status == noErr ? object : kAudioObjectUnknown;
}

// Reads a UInt32 property of object in scope; 0 when missing.
static UInt32 ReadUInt32(AudioObjectID object, AudioObjectPropertySelector selector,
                         AudioObjectPropertyScope scope)
{
    AudioObjectPropertyAddress address = maudCoreAudioAddress(selector, scope);
    UInt32 value = 0;
    UInt32 size = sizeof(value);
    return AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, &value) == noErr ? value
                                                                                            : 0;
}

// What the device adds on one side of a buffer's host time, in
// nanoseconds at its nominal rate: its latency and safety offset in
// scope, and its first stream's latency there, as cubeb counts it.
static int64_t DeviceLatency(AudioObjectID object, AudioObjectPropertyScope scope)
{
    uint64_t frames = (uint64_t)ReadUInt32(object, kAudioDevicePropertyLatency, scope) +
                      ReadUInt32(object, kAudioDevicePropertySafetyOffset, scope);
    AudioObjectPropertyAddress address = maudCoreAudioAddress(kAudioDevicePropertyStreams, scope);
    AudioStreamID streams[1] = {0};
    UInt32 size = sizeof(streams);
    if (AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, streams) == noErr &&
        size >= sizeof(AudioStreamID))
    {
        address.mSelector = kAudioStreamPropertyLatency;
        address.mScope = kAudioObjectPropertyScopeGlobal;
        UInt32 latency = 0;
        size = sizeof(latency);
        if (AudioObjectGetPropertyData(streams[0], &address, 0, nullptr, &size, &latency) == noErr)
        {
            frames += latency;
        }
    }
    address = maudCoreAudioAddress(kAudioDevicePropertyNominalSampleRate,
                                   kAudioObjectPropertyScopeGlobal);
    Float64 rate = 0.0;
    size = sizeof(rate);
    if (AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, &rate) != noErr ||
        rate < 1.0)
    {
        return 0;
    }
    return (int64_t)((double)frames * 1e9 / rate);
}

// Asks the device for an IO buffer of the stream's period, within the
// device's range; the HAL keeps one size per process and device.
static void AskBufferFrames(AudioObjectID object, uint32_t period)
{
    AudioObjectPropertyAddress address = maudCoreAudioAddress(
        kAudioDevicePropertyBufferFrameSizeRange, kAudioObjectPropertyScopeGlobal);
    AudioValueRange range = {0};
    UInt32 size = sizeof(range);
    if (AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, &range) != noErr)
    {
        return;
    }
    UInt32 frames = period;
    frames = frames < (UInt32)range.mMinimum ? (UInt32)range.mMinimum : frames;
    frames = frames > (UInt32)range.mMaximum ? (UInt32)range.mMaximum : frames;
    address.mSelector = kAudioDevicePropertyBufferFrameSize;
    OSStatus status =
        AudioObjectSetPropertyData(object, &address, 0, nullptr, sizeof(frames), &frames);
    (void)status;
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

// Sets the unit's device, client format, callback and slice size.
static bool Configure(maudCoreAudioStream* entry, AudioObjectID object)
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
               AudioUnitSetProperty(unit, kAudioOutputUnitProperty_CurrentDevice,
                                    kAudioUnitScope_Global, 0, &object, sizeof(object)) == noErr &&
               AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output,
                                    1, &format, sizeof(format)) == noErr &&
               AudioUnitSetProperty(unit, kAudioOutputUnitProperty_SetInputCallback,
                                    kAudioUnitScope_Global, 0, &callback,
                                    sizeof(callback)) == noErr &&
               AudioUnitSetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice,
                                    kAudioUnitScope_Global, 0, &slice, sizeof(slice)) == noErr;
    }
    AURenderCallbackStruct callback = {.inputProc = Render, .inputProcRefCon = entry};
    return AudioUnitSetProperty(unit, kAudioOutputUnitProperty_CurrentDevice,
                                kAudioUnitScope_Global, 0, &object, sizeof(object)) == noErr &&
           AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0,
                                &format, sizeof(format)) == noErr &&
           AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input,
                                0, &callback, sizeof(callback)) == noErr &&
           AudioUnitSetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice,
                                kAudioUnitScope_Global, 0, &slice, sizeof(slice)) == noErr;
}

static void Disconnect(maudContext* context, maudCoreAudioStream* entry)
{
    if (entry->hogged != kAudioObjectUnknown)
    {
        maudCoreAudioGiveDevice(entry->hogged);
    }
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
    maudStreamCore* core = entry->core;
    *entry = (maudCoreAudioStream){.core = core};
}

// One buffer of interleaved frames for an input, after the list's
// header.
static bool AllocateCaptured(maudContext* context, maudCoreAudioStream* entry)
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

// Makes and initializes the stream's unit on its device.
static maudResult Connect(maudContext* context, maudCoreAudioStream* entry)
{
    AudioObjectID object = ObjectOf(context, entry->core);
    AudioComponentDescription description = {
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_HALOutput,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
    };
    // An exclusive stream holds its device before it does any IO on it.
    if (entry->core->def.share == maud_shareExclusive && object != kAudioObjectUnknown)
    {
        maudResult taken = maudCoreAudioTakeDevice(object);
        if (taken != maud_success)
        {
            return taken;
        }
        entry->hogged = object;
    }
    AudioComponent component = AudioComponentFindNext(nullptr, &description);
    if (object == kAudioObjectUnknown || component == nullptr ||
        AudioComponentInstanceNew(component, &entry->unit) != noErr)
    {
        entry->unit = nullptr;
        return maud_errorPlatform;
    }
    bool input = entry->core->def.direction == maud_directionInput;
    if (input && !AllocateCaptured(context, entry))
    {
        return maud_errorCapacity;
    }
    if (!Configure(entry, object))
    {
        return maud_errorPlatform;
    }
    AskBufferFrames(object, entry->core->format.periodFrames);
    entry->deviceLatency = DeviceLatency(object, input ? kAudioObjectPropertyScopeInput
                                                       : kAudioObjectPropertyScopeOutput);
    if (AudioUnitInitialize(entry->unit) != noErr)
    {
        return maud_errorPlatform;
    }
    // The HAL unit processes nothing.
    if (input)
    {
        maudReportVoice(entry->core, maud_voiceNone);
    }
    return maud_success;
}

// Whether a stream is a half of a duplex stream that asks for voice
// processing, which runs on one voice-processing unit.
static bool Voiced(const maudStreamCore* core)
{
    return core->duplexGroup != 0 && core->def.voice != maud_voiceNone;
}

// The other half of a duplex stream: the slot of the same group.
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

// Sets the voice-processing unit's devices, client formats, callbacks,
// processing and slice size: bus 0 plays the output half, bus 1
// captures the input half.
static bool ConfigureVoice(maudCoreAudioStream* input, maudCoreAudioStream* output,
                           AudioObjectID heard, AudioObjectID played)
{
    AudioUnit unit = input->unit;
    AudioStreamBasicDescription outputFormat = FormatOf(output->core);
    // The unit captures one channel whatever it is asked for, so it is
    // asked for one; Capture spreads it.
    AudioStreamBasicDescription inputFormat = FormatWith(input->core, 1);
    AURenderCallbackStruct render = {.inputProc = Render, .inputProcRefCon = output};
    AURenderCallbackStruct capture = {.inputProc = Capture, .inputProcRefCon = input};
    UInt32 on = 1;
    UInt32 bypass = 0;
    UInt32 gain = (input->core->def.voice & maud_voiceGainControl) != 0 ? 1u : 0u;
    UInt32 slice = MAX_SLICE_FRAMES;
    return AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1,
                                &on, sizeof(on)) == noErr &&
           AudioUnitSetProperty(unit, kAudioOutputUnitProperty_CurrentDevice,
                                kAudioUnitScope_Global, 0, &played, sizeof(played)) == noErr &&
           AudioUnitSetProperty(unit, kAudioOutputUnitProperty_CurrentDevice,
                                kAudioUnitScope_Global, 1, &heard, sizeof(heard)) == noErr &&
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
static void ReportUnitVoice(maudCoreAudioStream* input)
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
// stream from its input half, for both halves.
static maudResult ConnectVoice(maudContext* context, maudCoreAudioStream* input,
                               maudCoreAudioStream* output)
{
    AudioObjectID heard = ObjectOf(context, input->core);
    AudioObjectID played = ObjectOf(context, output->core);
    AudioComponentDescription description = {
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_VoiceProcessingIO,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
    };
    AudioComponent component = AudioComponentFindNext(nullptr, &description);
    if (heard == kAudioObjectUnknown || played == kAudioObjectUnknown || component == nullptr ||
        AudioComponentInstanceNew(component, &input->unit) != noErr)
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
    if (!ConfigureVoice(input, output, heard, played))
    {
        return maud_errorPlatform;
    }
    AskBufferFrames(played, output->core->format.periodFrames);
    AskBufferFrames(heard, input->core->format.periodFrames);
    output->deviceLatency = DeviceLatency(played, kAudioObjectPropertyScopeOutput);
    input->deviceLatency = DeviceLatency(heard, kAudioObjectPropertyScopeInput);
    if (AudioUnitInitialize(input->unit) != noErr)
    {
        return maud_errorPlatform;
    }
    ReportUnitVoice(input);
    return maud_success;
}

// A voiced half: the output half waits for the input half, which builds
// the unit once both have devices.
static maudResult OpenVoiced(maudContext* context, maudStreamSlot* slot)
{
    maudStreamSlot* partner = PartnerOf(context, &slot->core);
    if (slot->core.def.direction == maud_directionOutput || partner == nullptr ||
        slot->core.binding.current.index1 == 0 || partner->core.binding.current.index1 == 0)
    {
        return maud_success;
    }
    maudCoreAudioStream* entry = EntryOf(context, slot);
    maudResult result = ConnectVoice(context, entry, EntryOf(context, partner));
    if (result != maud_success)
    {
        Disconnect(context, entry);
    }
    return result;
}

// Connects the stream's unit if it has a device; a stream without one
// waits.
static maudResult Open(maudContext* context, maudStreamSlot* slot)
{
    maudCoreAudioStream* entry = EntryOf(context, slot);
    *entry = (maudCoreAudioStream){.core = &slot->core};
    maudResetVoice(&slot->core);
    if (Voiced(&slot->core))
    {
        return OpenVoiced(context, slot);
    }
    // Only a duplex stream gives the voice-processing unit what it plays;
    // an input alone runs on the HAL unit, which processes nothing.
    if (slot->core.binding.current.index1 == 0)
    {
        return maud_success;
    }
    maudResult result = Connect(context, entry);
    if (result != maud_success)
    {
        Disconnect(context, entry);
    }
    return result;
}

static void Start(maudCoreAudioStream* entry)
{
    if (entry->unit != nullptr && !entry->playing)
    {
        // The time since the unit last ran is not an xrun.
        entry->nextSampleTime = -1.0;
        if (entry->voicePartner != nullptr)
        {
            entry->voicePartner->nextSampleTime = -1.0;
        }
        entry->playing = AudioOutputUnitStart(entry->unit) == noErr;
    }
}

// Stops the unit; when AudioOutputUnitStop returns, the IO thread has
// left the render callback.
static void Stop(maudCoreAudioStream* entry)
{
    if (entry->playing)
    {
        OSStatus status = AudioOutputUnitStop(entry->unit);
        (void)status;
        entry->playing = false;
    }
}

static bool Running(const maudStreamSlot* slot)
{
    return atomic_load_explicit(&slot->core.state, memory_order_acquire) == maud_streamRunning;
}

maudResult maudCoreAudioAttachStream(maudContext* context, maudStreamSlot* slot)
{
    return Open(context, slot);
}

// The half that holds a voiced duplex stream's unit: the input half.
static maudStreamSlot* UnitHolder(maudContext* context, maudStreamSlot* slot)
{
    if (!Voiced(&slot->core) || slot->core.def.direction == maud_directionInput)
    {
        return slot;
    }
    return PartnerOf(context, &slot->core);
}

void maudCoreAudioDetachStream(maudContext* context, maudStreamSlot* slot)
{
    maudCoreAudioStream* entry = EntryOf(context, slot);
    // The unit plays the output half's frames: it goes before they do.
    if (entry->unit == nullptr && entry->voicePartner != nullptr)
    {
        Disconnect(context, entry->voicePartner);
    }
    Disconnect(context, entry);
}

void maudCoreAudioRetargetStream(maudContext* context, maudStreamSlot* slot)
{
    // A voiced half moves by rebuilding the unit both halves share.
    slot = UnitHolder(context, slot);
    if (slot == nullptr)
    {
        return;
    }
    maudCoreAudioDetachStream(context, slot);
    // A failure leaves the stream without a unit; the drain tries again
    // while it runs.
    if (Open(context, slot) == maud_success && Running(slot))
    {
        Start(EntryOf(context, slot));
    }
}

void maudCoreAudioSetStreamActive(maudContext* context, maudStreamSlot* slot, bool active)
{
    // A voiced duplex stream's unit starts and stops with its input
    // half; the output half renders silence whenever it is not running.
    if (Voiced(&slot->core) && slot->core.def.direction == maud_directionOutput)
    {
        return;
    }
    maudCoreAudioStream* entry = EntryOf(context, slot);
    if (!active)
    {
        Stop(entry);
        return;
    }
    if (entry->unit == nullptr)
    {
        maudResult result = Open(context, slot);
        (void)result;
    }
    Start(entry);
}

void maudCoreAudioResumeStreams(maudContext* context)
{
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        maudStreamSlot* slot = &context->streams.slots[i];
        // A voiced output half never has a unit of its own.
        bool ownsUnit = !(Voiced(&slot->core) && slot->core.def.direction == maud_directionOutput);
        if (slot->live && ownsUnit && Running(slot) && slot->core.binding.current.index1 != 0 &&
            EntryOf(context, slot)->unit == nullptr)
        {
            maudCoreAudioRetargetStream(context, slot);
        }
    }
}
