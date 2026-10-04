// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Streams on PulseAudio. Each stream has its own pa_mainloop, pa_context
// and pa_stream. The control thread connects them corked; while the
// stream runs, one library thread iterates the loop, and the write and
// read callbacks move whatever size the server asks for through the
// stream's fixed-period adapter. Stopping joins the thread and corks.

#include "pulse_stream.h"

#include "clock.h"
#include "context.h"
#include "period.h"
#include "pulse_core.h"
#include "thread.h"
#include "xrun.h"

#include <string.h>

// Loop iterations that send a cork after the thread has stopped.
#define FLUSH_ITERATIONS 8

static maudPulseStream* EntryOf(maudContext* context, const maudStreamSlot* slot)
{
    maudPulse* pulse = context->native;
    return &pulse->streams[slot - context->streams.slots];
}

// The PulseAudio channel position of each speaker.
static pa_channel_position_t PositionOf(maudSpeaker speaker)
{
    static const pa_channel_position_t positions[] = {
        [maud_speakerNone] = PA_CHANNEL_POSITION_INVALID,
        [maud_speakerFrontLeft] = PA_CHANNEL_POSITION_FRONT_LEFT,
        [maud_speakerFrontRight] = PA_CHANNEL_POSITION_FRONT_RIGHT,
        [maud_speakerFrontCenter] = PA_CHANNEL_POSITION_FRONT_CENTER,
        [maud_speakerLowFrequency] = PA_CHANNEL_POSITION_LFE,
        [maud_speakerBackLeft] = PA_CHANNEL_POSITION_REAR_LEFT,
        [maud_speakerBackRight] = PA_CHANNEL_POSITION_REAR_RIGHT,
        [maud_speakerSideLeft] = PA_CHANNEL_POSITION_SIDE_LEFT,
        [maud_speakerSideRight] = PA_CHANNEL_POSITION_SIDE_RIGHT,
        [maud_speakerTopFrontLeft] = PA_CHANNEL_POSITION_TOP_FRONT_LEFT,
        [maud_speakerTopFrontRight] = PA_CHANNEL_POSITION_TOP_FRONT_RIGHT,
        [maud_speakerTopBackLeft] = PA_CHANNEL_POSITION_TOP_REAR_LEFT,
        [maud_speakerTopBackRight] = PA_CHANNEL_POSITION_TOP_REAR_RIGHT,
    };
    return positions[speaker];
}

// Moves frames between the device's buffer and the adapter: into out
// for playback, from in for capture. Only the stream's thread renders:
// a callback the control thread's waits run gives silence and takes
// nothing.
static void Render(maudPulseStream* entry, float* out, const float* in, uint32_t frames)
{
    bool output = out != nullptr;
    maudStreamCore* core = entry->core;
    bool running = entry->threadRunning &&
                   atomic_load_explicit(&core->state, memory_order_acquire) == maud_streamRunning;
    atomic_store_explicit(&core->renderingThread, maudCurrentThread(), memory_order_release);
    core->period.sampleRate = atomic_load_explicit(&core->blockRate, memory_order_acquire);
    if (output && running)
    {
        maudPullPeriod(&core->period, out, frames);
    }
    else if (output)
    {
        memset(out, 0, (size_t)frames * core->period.channelCount * sizeof(float));
    }
    else if (running)
    {
        maudPushPeriod(&core->period, in, frames);
    }
    atomic_store_explicit(&core->renderingThread, 0, memory_order_release);
    // The server's latency: until data written now is heard, or since
    // the data read now was captured. Until its first timing update it
    // has none.
    pa_usec_t latency = 0;
    int negative = 0;
    if (entry->api->streamGetLatency(entry->stream, &latency, &negative) != 0 || negative)
    {
        latency = 0;
    }
    if (output)
    {
        maudStampOutputClock(core, (int64_t)latency * 1000);
    }
    else
    {
        maudStampInputClock(core, (int64_t)latency * 1000);
    }
    atomic_fetch_add_explicit(&core->position, frames, memory_order_release);
}

// Fills what the server asks for in buffers it lends: no allocation,
// lock or wait.
static void OnWrite(pa_stream* stream, size_t bytes, void* user)
{
    maudPulseStream* entry = user;
    size_t stride = entry->core->period.channelCount * sizeof(float);
    while (bytes >= stride)
    {
        void* data = nullptr;
        size_t size = bytes;
        if (entry->api->streamBeginWrite(stream, &data, &size) < 0 || data == nullptr)
        {
            return;
        }
        uint32_t frames = (uint32_t)((size < bytes ? size : bytes) / stride);
        if (frames == 0)
        {
            entry->api->streamCancelWrite(stream);
            return;
        }
        Render(entry, data, nullptr, frames);
        entry->api->streamWrite(stream, data, frames * stride, nullptr, 0, PA_SEEK_RELATIVE);
        bytes -= frames * stride;
    }
}

// Takes every fragment the server has; a hole carries no samples.
// The server's buffer ran dry (playback) or overflowed (capture).
static void OnXrun(pa_stream* stream, void* user)
{
    (void)stream;
    maudPulseStream* entry = user;
    maudCountXrun(entry->core);
}

static void OnRead(pa_stream* stream, size_t bytes, void* user)
{
    (void)bytes;
    maudPulseStream* entry = user;
    size_t stride = entry->core->period.channelCount * sizeof(float);
    const void* data = nullptr;
    size_t size = 0;
    while (entry->api->streamPeek(stream, &data, &size) == 0 && size != 0)
    {
        if (data != nullptr)
        {
            Render(entry, nullptr, data, (uint32_t)(size / stride));
        }
        entry->api->streamDrop(stream);
    }
}

// 1 once state is ready, -1 once it failed, 0 while pending.
static int ContextProgress(const maudPulseStream* entry)
{
    pa_context_state_t state = entry->api->contextGetState(entry->context);
    return state == PA_CONTEXT_READY ? 1 : !PA_CONTEXT_IS_GOOD(state) ? -1 : 0;
}

static int StreamProgress(const maudPulseStream* entry)
{
    pa_stream_state_t state = entry->api->streamGetState(entry->stream);
    return state == PA_STREAM_READY ? 1 : !PA_STREAM_IS_GOOD(state) ? -1 : 0;
}

// Runs the stream's loop on the calling thread until progress settles
// or the deadline passes.
static bool WaitFor(maudPulseStream* entry, int (*progress)(const maudPulseStream*),
                    int64_t deadline)
{
    const maudPulseApi* api = entry->api;
    for (int settled = progress(entry); settled == 0; settled = progress(entry))
    {
        int64_t remaining = deadline - maudPulseNow();
        if (remaining <= 0 || api->mainloopPrepare(entry->loop, (int)(remaining / 1000) + 1) < 0 ||
            api->mainloopPoll(entry->loop) < 0 || api->mainloopDispatch(entry->loop) < 0)
        {
            return false;
        }
    }
    return progress(entry) == 1;
}

static void Disconnect(maudPulseStream* entry)
{
    const maudPulseApi* api = entry->api;
    if (entry->stream != nullptr)
    {
        api->streamSetWriteCallback(entry->stream, nullptr, nullptr);
        api->streamSetReadCallback(entry->stream, nullptr, nullptr);
        api->streamSetUnderflowCallback(entry->stream, nullptr, nullptr);
        api->streamSetOverflowCallback(entry->stream, nullptr, nullptr);
        api->streamDisconnect(entry->stream);
        api->streamUnref(entry->stream);
    }
    if (entry->context != nullptr)
    {
        api->contextDisconnect(entry->context);
        api->contextUnref(entry->context);
    }
    if (entry->loop != nullptr)
    {
        api->mainloopFree(entry->loop);
    }
    *entry = (maudPulseStream){0};
}

// The stream's sample spec and channel map: interleaved 32-bit float
// at its rate, with its layout's positions; a mono stream is MONO.
static void DescribeFormat(const maudStreamCore* core, pa_sample_spec* spec, pa_channel_map* map)
{
    uint32_t channels = maudGetLayoutChannelCount(core->format.layout);
    *spec = (pa_sample_spec){
        .format = PA_SAMPLE_FLOAT32NE,
        .rate = core->format.sampleRate,
        .channels = (uint8_t)channels,
    };
    *map = (pa_channel_map){.channels = (uint8_t)channels};
    for (uint32_t c = 0; c < channels; ++c)
    {
        map->map[c] = channels == 1 ? PA_CHANNEL_POSITION_MONO
                                    : PositionOf(maudGetLayoutSpeaker(core->format.layout, c));
    }
}

// Creates the pa_stream and connects it corked: to the device it was
// opened on and no other, or to the server's default, which the server
// moves it with. A period of latency at the server, two queued.
static bool ConnectStream(maudContext* context, maudPulseStream* entry, int64_t deadline)
{
    const maudPulseApi* api = entry->api;
    maudStreamCore* core = entry->core;
    pa_sample_spec spec;
    pa_channel_map map;
    DescribeFormat(core, &spec, &map);
    entry->stream = api->streamNew(entry->context, "Maul Audio", &spec, &map);
    if (entry->stream == nullptr)
    {
        return false;
    }
    bool output = core->def.direction == maud_directionOutput;
    api->streamSetWriteCallback(entry->stream, output ? OnWrite : nullptr, entry);
    api->streamSetReadCallback(entry->stream, output ? nullptr : OnRead, entry);
    api->streamSetUnderflowCallback(entry->stream, output ? OnXrun : nullptr, entry);
    api->streamSetOverflowCallback(entry->stream, output ? nullptr : OnXrun, entry);
    uint32_t period = core->format.periodFrames * spec.channels * (uint32_t)sizeof(float);
    pa_buffer_attr attr = {
        .maxlength = UINT32_MAX,
        .tlength = period * 2,
        .prebuf = UINT32_MAX,
        .minreq = period,
        .fragsize = period,
    };
    char name[MAUD_PULSE_NAME_BYTES] = {0};
    const maudDeviceSlot* device = maudFindDevice(context, core->binding.requested);
    // Timing updates keep pa_stream_get_latency current for the clock.
    pa_stream_flags_t flags = PA_STREAM_START_CORKED | PA_STREAM_ADJUST_LATENCY |
                              PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_INTERPOLATE_TIMING;
    if (device != nullptr && device->key.length < sizeof(name))
    {
        memcpy(name, device->key.bytes, device->key.length);
        flags |= PA_STREAM_DONT_MOVE;
    }
    const char* target = name[0] != '\0' ? name : nullptr;
    int connected =
        output ? api->streamConnectPlayback(entry->stream, target, &attr, flags, nullptr, nullptr)
               : api->streamConnectRecord(entry->stream, target, &attr, flags);
    return connected >= 0 && WaitFor(entry, StreamProgress, deadline);
}

// Builds the stream's connection, corked. Succeeds without one while the
// server is away.
static maudResult Connect(maudContext* context, maudStreamSlot* slot)
{
    maudPulse* pulse = context->native;
    maudPulseStream* entry = EntryOf(context, slot);
    *entry = (maudPulseStream){.api = &pulse->api, .core = &slot->core};
    if (pulse->server.context == nullptr)
    {
        return maud_success;
    }
    int64_t deadline = maudPulseNow() + MAUD_PULSE_DEADLINE_NS;
    entry->used = true;
    entry->loop = pulse->api.mainloopNew();
    entry->context =
        entry->loop != nullptr
            ? pulse->api.contextNew(pulse->api.mainloopGetApi(entry->loop), "Maul Audio stream")
            : nullptr;
    bool connected =
        entry->context != nullptr &&
        pulse->api.contextConnect(entry->context, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) >= 0 &&
        WaitFor(entry, ContextProgress, deadline) && ConnectStream(context, entry, deadline);
    if (!connected)
    {
        Disconnect(entry);
        return maud_errorPlatform;
    }
    return maud_success;
}

static void Cork(maudPulseStream* entry, bool cork)
{
    pa_operation* operation = entry->api->streamCork(entry->stream, cork ? 1 : 0, nullptr, nullptr);
    if (operation != nullptr)
    {
        entry->api->operationUnref(operation);
    }
}

// The stream's thread: the loop until asked to return.
static void RunStream(void* user)
{
    maudPulseStream* entry = user;
    while (!atomic_load(&entry->quit))
    {
        if (entry->api->mainloopIterate(entry->loop, 1, nullptr) < 0)
        {
            break;
        }
    }
}

static void StartThread(maudPulseStream* entry)
{
    Cork(entry, false);
    atomic_store(&entry->quit, false);
    // Set before the thread starts, which reads it.
    entry->threadRunning = true;
    if (!maudStartWorker(&entry->worker, RunStream, entry, "maud-pulse"))
    {
        entry->threadRunning = false;
    }
}

// Joins the thread, then corks the stream and sends the cork.
static void StopThread(maudPulseStream* entry)
{
    if (!entry->threadRunning)
    {
        return;
    }
    atomic_store(&entry->quit, true);
    entry->api->mainloopWakeup(entry->loop);
    maudJoinWorker(&entry->worker);
    entry->threadRunning = false;
    Cork(entry, true);
    for (int i = 0; i < FLUSH_ITERATIONS; ++i)
    {
        if (entry->api->mainloopIterate(entry->loop, 0, nullptr) <= 0)
        {
            break;
        }
    }
}

// Whether the stream's connection is up and its pa_stream ready.
static bool Healthy(const maudPulseStream* entry)
{
    return entry->used && ContextProgress(entry) == 1 && StreamProgress(entry) == 1;
}

maudResult maudPulseAttachStream(maudContext* context, maudStreamSlot* slot)
{
    return Connect(context, slot);
}

void maudPulseDetachStream(maudContext* context, maudStreamSlot* slot)
{
    maudPulseStream* entry = EntryOf(context, slot);
    StopThread(entry);
    Disconnect(entry);
}

void maudPulseSetStreamActive(maudContext* context, maudStreamSlot* slot, bool active)
{
    maudPulseStream* entry = EntryOf(context, slot);
    if (!active)
    {
        StopThread(entry);
        return;
    }
    if (entry->threadRunning)
    {
        return;
    }
    if (!Healthy(entry))
    {
        Disconnect(entry);
        if (Connect(context, slot) != maud_success)
        {
            return;
        }
    }
    if (entry->used)
    {
        StartThread(entry);
    }
}

void maudPulseRetargetStream(maudContext* context, maudStreamSlot* slot)
{
    maudPulseDetachStream(context, slot);
    // A failure leaves the stream without a connection; the next drain
    // tries again if it runs.
    if (Connect(context, slot) == maud_success &&
        atomic_load_explicit(&slot->core.state, memory_order_acquire) == maud_streamRunning)
    {
        maudPulseSetStreamActive(context, slot, true);
    }
}

void maudPulseResumeStreams(maudContext* context)
{
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        maudStreamSlot* slot = &context->streams.slots[i];
        if (slot->live &&
            atomic_load_explicit(&slot->core.state, memory_order_acquire) == maud_streamRunning &&
            !EntryOf(context, slot)->threadRunning)
        {
            maudPulseSetStreamActive(context, slot, true);
        }
    }
}
