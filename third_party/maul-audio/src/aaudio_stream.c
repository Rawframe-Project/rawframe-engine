// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Streams on AAudio. Each stream has an AAudio stream on the default
// device of its direction, in 32-bit float at the stream's rate and
// channel count, which AAudio converts to the device's in shared mode.
// Its data callback, on AAudio's thread, moves each buffer through the
// stream's fixed-period adapter. A device going away reaches the error
// callback, also on a thread of AAudio's, which may not close the
// stream: it raises a flag, and the drain opens the stream again on the
// new default.

#include "aaudio_stream.h"

#include "aaudio_core.h"
#include "aaudio_java.h"
#include "clock.h"
#include "context.h"
#include "device.h"
#include "follow.h"
#include "period.h"
#include "thread.h"
#include "voice.h"
#include "xrun.h"

#include <string.h>
#include <time.h>

static maudAaudioStream* EntryOf(maudContext* context, const maudStreamSlot* slot)
{
    maudAaudio* aaudio = context->native;
    return &aaudio->streams[slot - context->streams.slots];
}

// Counts the xruns AAudio saw since the last callback.
static void CountXruns(maudAaudioStream* entry, AAudioStream* stream)
{
    int32_t xruns = AAudioStream_getXRunCount(stream);
    for (; entry->xruns < xruns; ++entry->xruns)
    {
        maudCountXrun(entry->core);
    }
}

// Stamps the stream's clock. AAudio's timestamp gives the time a frame
// was heard or captured; the buffer's first frame is the frames written
// before it (output) or read before it (input) away from that frame.
// Until AAudio has a timestamp, shortly after a start, the latency is
// the stream's buffer.
static void Stamp(maudStreamCore* core, AAudioStream* stream, int32_t frames, bool output)
{
    double rate = (double)AAudioStream_getSampleRate(stream);
    int64_t position = 0;
    int64_t time = 0;
    int64_t latency = 0;
    if (AAudioStream_getTimestamp(stream, CLOCK_MONOTONIC, &position, &time) == AAUDIO_OK)
    {
        int64_t first = output ? AAudioStream_getFramesWritten(stream)
                               : AAudioStream_getFramesRead(stream) - frames;
        int64_t at = time + (int64_t)((double)(first - position) * 1e9 / rate);
        int64_t now = maudNowNanoseconds();
        latency = output ? at - now : now - at;
    }
    else
    {
        latency = (int64_t)((double)AAudioStream_getBufferSizeInFrames(stream) * 1e9 / rate);
    }
    latency = latency > 0 ? latency : 0;
    if (output)
    {
        maudStampOutputClock(core, latency);
    }
    else
    {
        maudStampInputClock(core, latency);
    }
}

// Fills an output buffer or takes an input buffer on AAudio's thread:
// the stream's frames while it runs, silence otherwise.
static aaudio_data_callback_result_t Move(AAudioStream* stream, void* user, void* data,
                                          int32_t frames)
{
    maudAaudioStream* entry = user;
    maudStreamCore* core = entry->core;
    bool output = core->def.direction == maud_directionOutput;
    CountXruns(entry, stream);
    bool running = atomic_load_explicit(&core->state, memory_order_acquire) == maud_streamRunning;
    atomic_store_explicit(&core->renderingThread, maudCurrentThread(), memory_order_release);
    core->period.sampleRate = atomic_load_explicit(&core->blockRate, memory_order_acquire);
    if (output && running)
    {
        maudPullPeriod(&core->period, data, (uint32_t)frames);
    }
    else if (output)
    {
        memset(data, 0, (size_t)frames * core->period.channelCount * sizeof(float));
    }
    else if (running)
    {
        maudPushPeriod(&core->period, data, (uint32_t)frames);
    }
    atomic_store_explicit(&core->renderingThread, 0, memory_order_release);
    Stamp(core, stream, frames, output);
    atomic_fetch_add_explicit(&core->position, (uint64_t)frames, memory_order_release);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static void Fail(AAudioStream* stream, void* user, aaudio_result_t error)
{
    (void)stream;
    (void)error;
    maudAaudioStream* entry = user;
    atomic_store_explicit(&entry->lost, true, memory_order_release);
}

// Sets what the stream is for: a voice stream (a half of a duplex
// stream that asks for voice processing, or an input that does) plays
// as a call and captures through the platform's voice processing, with
// a session of its own; any other output plays as media or as a call by
// its role, and any other input asks for the least processing.
static void Describe(AAudioStreamBuilder* builder, const maudStreamCore* core)
{
    bool voiced = core->def.voice != maud_voiceNone;
    if (core->def.direction == maud_directionOutput)
    {
        bool call = voiced || core->def.role == maud_roleCommunications;
        AAudioStreamBuilder_setUsage(builder,
                                     call ? AAUDIO_USAGE_VOICE_COMMUNICATION : AAUDIO_USAGE_MEDIA);
        AAudioStreamBuilder_setContentType(builder, call ? AAUDIO_CONTENT_TYPE_SPEECH
                                                         : AAUDIO_CONTENT_TYPE_MUSIC);
        return;
    }
    AAudioStreamBuilder_setInputPreset(builder, voiced ? AAUDIO_INPUT_PRESET_VOICE_COMMUNICATION
                                                       : AAUDIO_INPUT_PRESET_VOICE_RECOGNITION);
    if (voiced)
    {
        AAudioStreamBuilder_setSessionId(builder, AAUDIO_SESSION_ID_ALLOCATE);
    }
}

// The AAudio id of the device a stream is on: 0 for a default, which
// Android routes; -1 when the device is not in the last scan.
static int32_t DeviceIdOf(maudContext* context, const maudStreamCore* core)
{
    const maudDeviceSlot* device = maudFindDevice(context, core->binding.current);
    const maudAaudio* aaudio = context->native;
    for (uint32_t i = 0; device != nullptr && i < aaudio->endpointCount; ++i)
    {
        const maudAaudioEndpoint* endpoint = &aaudio->endpoints[i];
        if (endpoint->direction == core->def.direction &&
            strlen(endpoint->key) == device->key.length &&
            memcmp(endpoint->key, device->key.bytes, device->key.length) == 0)
        {
            return endpoint->id;
        }
    }
    return -1;
}

// Opens the stream's AAudio stream. An exclusive stream AAudio could
// only share is refused.
static maudResult Open(maudContext* context, maudStreamSlot* slot)
{
    maudAaudioStream* entry = EntryOf(context, slot);
    maudStreamCore* core = &slot->core;
    *entry = (maudAaudioStream){.core = core};
    atomic_init(&entry->lost, false);
    int32_t device = DeviceIdOf(context, core);
    if (device < 0)
    {
        return maud_errorPlatform;
    }
    AAudioStreamBuilder* builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK)
    {
        return maud_errorPlatform;
    }
    bool output = core->def.direction == maud_directionOutput;
    AAudioStreamBuilder_setDirection(builder,
                                     output ? AAUDIO_DIRECTION_OUTPUT : AAUDIO_DIRECTION_INPUT);
    AAudioStreamBuilder_setDeviceId(builder, device != 0 ? device : AAUDIO_UNSPECIFIED);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setSampleRate(builder, (int32_t)core->format.sampleRate);
    AAudioStreamBuilder_setChannelCount(builder, (int32_t)core->period.channelCount);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(builder, core->exclusive ? AAUDIO_SHARING_MODE_EXCLUSIVE
                                                                : AAUDIO_SHARING_MODE_SHARED);
    Describe(builder, core);
    const maudAaudioLate* late = &((const maudAaudio*)context->native)->late;
    if (core->def.contentSpatialized && late->setContentSpatialized != nullptr &&
        late->setSpatializationBehavior != nullptr)
    {
        late->setContentSpatialized(builder, true);
        late->setSpatializationBehavior(builder, AAUDIO_SPATIALIZATION_BEHAVIOR_NEVER);
    }
    AAudioStreamBuilder_setDataCallback(builder, Move, entry);
    AAudioStreamBuilder_setErrorCallback(builder, Fail, entry);
    AAudioStream* stream = nullptr;
    aaudio_result_t opened = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);
    if (opened != AAUDIO_OK)
    {
        return maud_errorPlatform;
    }
    if (core->exclusive && AAudioStream_getSharingMode(stream) != AAUDIO_SHARING_MODE_EXCLUSIVE)
    {
        AAudioStream_close(stream);
        return maud_errorUnsupported;
    }
    entry->stream = stream;
    // From API 32 the open stream says whether it carries the mark.
    if (core->def.contentSpatialized && late->isContentSpatialized != nullptr)
    {
        maudSpatialMark mark =
            late->isContentSpatialized(stream) ? maud_markHonored : maud_markIgnored;
        atomic_store_explicit(&core->spatialMark, mark, memory_order_release);
    }
    if (!output)
    {
        // AAudio cannot say which processing the device applied: the
        // stream reports what it asked for.
        maudReportVoice(core, core->def.voice);
    }
    return maud_success;
}

static void Start(maudAaudioStream* entry)
{
    if (entry->stream != nullptr && !entry->playing)
    {
        entry->playing = AAudioStream_requestStart(entry->stream) == AAUDIO_OK;
    }
}

// Stops the stream and waits until AAudio says it stopped, so that no
// callback runs past it.
static void Stop(maudAaudioStream* entry)
{
    if (!entry->playing)
    {
        return;
    }
    entry->playing = false;
    if (AAudioStream_requestStop(entry->stream) != AAUDIO_OK)
    {
        return;
    }
    aaudio_stream_state_t state = AAUDIO_STREAM_STATE_STOPPING;
    aaudio_stream_state_t next = AAUDIO_STREAM_STATE_UNINITIALIZED;
    // A second at most; a stream lost meanwhile ends the wait.
    while (state == AAUDIO_STREAM_STATE_STOPPING &&
           AAudioStream_waitForStateChange(entry->stream, state, &next, 1000000000) == AAUDIO_OK)
    {
        state = next;
    }
}

static void Close(maudAaudioStream* entry)
{
    if (entry->stream != nullptr)
    {
        // Closing stops the stream first and returns once its callbacks
        // are done.
        aaudio_result_t closed = AAudioStream_close(entry->stream);
        (void)closed;
        entry->stream = nullptr;
        entry->playing = false;
    }
}

static bool Running(const maudStreamSlot* slot)
{
    return atomic_load_explicit(&slot->core.state, memory_order_acquire) == maud_streamRunning;
}

// Whether an input stream waits for the microphone: the application
// does not hold the permission, which Java says.
static bool Unpermitted(maudContext* context, const maudStreamSlot* slot)
{
    maudAaudio* aaudio = context->native;
    return slot->core.def.direction == maud_directionInput && aaudio->hasJava &&
           !maudAaudioMayRecord(aaudio);
}

// An input stream without the permission waits with
// maud_suspendPermission, after the library asks once; the drain opens
// it when the permission comes.
maudResult maudAaudioAttachStream(maudContext* context, maudStreamSlot* slot)
{
    maudResetVoice(&slot->core);
    if (Unpermitted(context, slot))
    {
        *EntryOf(context, slot) = (maudAaudioStream){.core = &slot->core};
        maudAaudioAskToRecord(context->native);
        maudAwaitPermission(context, slot, true);
        return maud_success;
    }
    return Open(context, slot);
}

void maudAaudioDetachStream(maudContext* context, maudStreamSlot* slot)
{
    Close(EntryOf(context, slot));
}

void maudAaudioSetStreamActive(maudContext* context, maudStreamSlot* slot, bool active)
{
    maudAaudioStream* entry = EntryOf(context, slot);
    if (!active)
    {
        Stop(entry);
        return;
    }
    if (entry->stream == nullptr && !slot->core.binding.awaitingPermission)
    {
        maudResult result = Open(context, slot);
        (void)result;
    }
    Start(entry);
}

void maudAaudioResumeStreams(maudContext* context)
{
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        maudStreamSlot* slot = &context->streams.slots[i];
        maudAaudioStream* entry = EntryOf(context, slot);
        if (!slot->live)
        {
            continue;
        }
        if (slot->core.binding.awaitingPermission)
        {
            // Granted, the stream opens as it resumes, if it runs.
            if (!Unpermitted(context, slot))
            {
                maudAwaitPermission(context, slot, false);
            }
            continue;
        }
        bool lost = entry->stream != nullptr &&
                    atomic_exchange_explicit(&entry->lost, false, memory_order_acq_rel);
        if (!lost && (entry->stream != nullptr || !Running(slot)))
        {
            continue;
        }
        // A failure leaves the stream without an AAudio stream; the drain
        // tries again while it runs.
        Close(entry);
        if (Open(context, slot) == maud_success && Running(slot))
        {
            Start(entry);
        }
    }
}
