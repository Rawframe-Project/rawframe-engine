// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Streams on PipeWire. Each stream is a pw_stream processed on
// libpipewire's real-time data thread: its process callback moves
// whatever size PipeWire asks for through the stream's fixed-period
// adapter, so the host's callback always sees whole periods.

#include "pipewire_stream.h"

#include "clock.h"
#include "context.h"
#include "period.h"
#include "pipewire_core.h"
#include "thread.h"
#include "xrun.h"

#include <sched.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>
#include <string.h>

// Bytes of the pod that describes a stream's format.
#define FORMAT_POD_BYTES 1024

static maudPipewireStream* EntryOf(maudContext* context, const maudStreamSlot* slot)
{
    maudPipewire* pipewire = context->native;
    return &pipewire->streams[slot - context->streams.slots];
}

// The PipeWire channel position of each speaker.
static uint32_t PositionOf(maudSpeaker speaker)
{
    static const uint32_t positions[] = {
        [maud_speakerNone] = SPA_AUDIO_CHANNEL_UNKNOWN,
        [maud_speakerFrontLeft] = SPA_AUDIO_CHANNEL_FL,
        [maud_speakerFrontRight] = SPA_AUDIO_CHANNEL_FR,
        [maud_speakerFrontCenter] = SPA_AUDIO_CHANNEL_FC,
        [maud_speakerLowFrequency] = SPA_AUDIO_CHANNEL_LFE,
        [maud_speakerBackLeft] = SPA_AUDIO_CHANNEL_RL,
        [maud_speakerBackRight] = SPA_AUDIO_CHANNEL_RR,
        [maud_speakerSideLeft] = SPA_AUDIO_CHANNEL_SL,
        [maud_speakerSideRight] = SPA_AUDIO_CHANNEL_SR,
        [maud_speakerTopFrontLeft] = SPA_AUDIO_CHANNEL_TFL,
        [maud_speakerTopFrontRight] = SPA_AUDIO_CHANNEL_TFR,
        [maud_speakerTopBackLeft] = SPA_AUDIO_CHANNEL_TRL,
        [maud_speakerTopBackRight] = SPA_AUDIO_CHANNEL_TRR,
    };
    return positions[speaker];
}

// Builds the stream's format into buffer: interleaved 32-bit float at
// its rate, with its layout's positions; a mono stream is MONO.
static const struct spa_pod* BuildFormat(const maudStreamCore* core, uint8_t* buffer)
{
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, FORMAT_POD_BYTES);
    uint32_t channels = maudGetLayoutChannelCount(core->format.layout);
    struct spa_audio_info_raw info = {
        .format = SPA_AUDIO_FORMAT_F32,
        .rate = core->format.sampleRate,
        .channels = channels,
    };
    for (uint32_t c = 0; c < channels; ++c)
    {
        info.position[c] = channels == 1 ? SPA_AUDIO_CHANNEL_MONO
                                         : PositionOf(maudGetLayoutSpeaker(core->format.layout, c));
    }
    return spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);
}

static void OnStateChanged(void* data, enum pw_stream_state old, enum pw_stream_state state,
                           const char* error)
{
    (void)old;
    (void)error;
    maudPipewireStream* entry = data;
    entry->state = state;
}

// The latency of this cycle's buffer, from the graph's report: the
// delay to the device, filters included, plus the frames queued ahead
// of the buffer and held in the resampler; for capture, the delay plus
// the buffer's own length, back to its first frame. Real-time safe.
static int64_t LatencyOf(const maudPipewireStream* entry, const struct pw_time* time,
                         uint32_t frames, bool output)
{
    const maudStreamCore* core = entry->core;
    int64_t delay = time->delay * 1000000000 * (int64_t)time->rate.num / (int64_t)time->rate.denom;
    uint64_t stride = (uint64_t)core->period.channelCount * sizeof(float);
    uint64_t extra = output ? time->queued / stride + time->buffered : frames;
    return delay + (int64_t)(extra * 1000000000 / core->format.sampleRate);
}

// Counts a skipped cycle: the graph's ticks between two cycles are the
// first one's length, so a gap past one and a half of the frames it
// moved (at the stream's rate) is an xrun. A change of quantum shows in
// the frames, not the gap, and is not counted.
static void CheckTicks(maudPipewireStream* entry, const struct pw_time* time, uint32_t frames)
{
    if (atomic_exchange_explicit(&entry->forgetTicks, false, memory_order_acquire))
    {
        entry->lastFrames = 0;
    }
    if (entry->lastFrames != 0 && time->ticks > entry->lastTicks)
    {
        uint64_t gap = (time->ticks - entry->lastTicks) * time->rate.num *
                       entry->core->format.sampleRate / time->rate.denom;
        if (2 * gap > 3 * (uint64_t)entry->lastFrames)
        {
            maudCountXrun(entry->core);
        }
    }
    entry->lastTicks = time->ticks;
    entry->lastFrames = frames;
}

// Runs on libpipewire's data thread: no allocation, lock or wait.
static void Process(maudPipewireStream* entry)
{
    maudStreamCore* core = entry->core;
    const maudPipewireApi* api = &entry->owner->api;
    struct pw_buffer* buffer = api->streamDequeueBuffer(entry->stream);
    if (buffer == nullptr)
    {
        return;
    }
    struct spa_data* plane = &buffer->buffer->datas[0];
    uint32_t stride = core->period.channelCount * (uint32_t)sizeof(float);
    bool output = core->def.direction == maud_directionOutput;
    uint32_t frames = output ? plane->maxsize / stride : plane->chunk->size / stride;
    if (output && buffer->requested != 0 && buffer->requested < frames)
    {
        frames = (uint32_t)buffer->requested;
    }
    if (plane->data != nullptr && frames != 0)
    {
        bool running =
            atomic_load_explicit(&core->state, memory_order_acquire) == maud_streamRunning;
        atomic_store_explicit(&core->renderingThread, maudCurrentThread(), memory_order_release);
        core->period.sampleRate = atomic_load_explicit(&core->blockRate, memory_order_acquire);
        float* samples = (float*)((uint8_t*)plane->data + (output ? 0 : plane->chunk->offset));
        if (output && running)
        {
            maudPullPeriod(&core->period, samples, frames);
        }
        else if (output)
        {
            memset(samples, 0, (size_t)frames * stride);
        }
        else if (running)
        {
            maudPushPeriod(&core->period, samples, frames);
        }
        atomic_store_explicit(&core->renderingThread, 0, memory_order_release);
        struct pw_time time = {0};
        bool timed =
            api->streamGetTime(entry->stream, &time, sizeof(time)) >= 0 && time.rate.denom != 0;
        int64_t latency = timed ? LatencyOf(entry, &time, frames, output) : 0;
        if (timed)
        {
            CheckTicks(entry, &time, frames);
        }
        if (output)
        {
            maudStampOutputClock(core, latency);
        }
        else
        {
            maudStampInputClock(core, latency);
        }
        atomic_fetch_add_explicit(&core->position, frames, memory_order_release);
    }
    if (output)
    {
        plane->chunk->offset = 0;
        plane->chunk->stride = (int32_t)stride;
        plane->chunk->size = frames * stride;
    }
    api->streamQueueBuffer(entry->stream, buffer);
}

// Counts itself in before looking at closing, as detaching marks
// closing before counting the callbacks in: one or the other sees.
static void OnProcess(void* data)
{
    maudPipewireStream* entry = data;
    atomic_fetch_add(&entry->inside, 1);
    if (!atomic_load(&entry->closing))
    {
        Process(entry);
    }
    atomic_fetch_sub(&entry->inside, 1);
}

static const struct pw_stream_events s_streamEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .state_changed = OnStateChanged,
    .process = OnProcess,
};

// The stream's properties: what it is, its period as a latency hint,
// and, for a stream opened on a device, that device and no other.
static struct pw_properties* StreamProperties(const maudContext* context,
                                              const maudStreamCore* core)
{
    const maudPipewireApi* api = &((maudPipewire*)context->native)->api;
    bool output = core->def.direction == maud_directionOutput;
    struct pw_properties* props = api->propertiesNew(
        PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, output ? "Playback" : "Capture",
        PW_KEY_MEDIA_ROLE, core->def.role == maud_roleCommunications ? "Communication" : "Game",
        nullptr);
    if (props == nullptr)
    {
        return nullptr;
    }
    api->propertiesSetf(props, PW_KEY_NODE_LATENCY, "%u/%u", core->format.periodFrames,
                        core->format.sampleRate);
    const maudDeviceSlot* device = maudFindDevice(context, core->binding.requested);
    if (device != nullptr)
    {
        api->propertiesSetf(props, PW_KEY_TARGET_OBJECT, "%.*s", (int)device->key.length,
                            device->key.bytes);
        api->propertiesSetf(props, PW_KEY_NODE_DONT_RECONNECT, "true");
    }
    // A duplex stream's halves share a driver, so one clock runs both.
    if (core->duplexGroup != 0)
    {
        api->propertiesSetf(props, PW_KEY_NODE_GROUP, "maud-duplex-%p-%u", (const void*)context,
                            core->duplexGroup);
    }
    return props;
}

// Iterates the loop until the stream has a format or failed, up to the
// deadline.
static bool WaitForFormat(maudPipewire* pipewire, const maudPipewireStream* entry)
{
    int64_t deadline = maudPipewireNow() + MAUD_PIPEWIRE_DEADLINE_NS;
    maudPipewireConnection* connection = &pipewire->connection;
    pw_loop_enter(connection->loop);
    while (entry->state != PW_STREAM_STATE_PAUSED && entry->state != PW_STREAM_STATE_STREAMING &&
           entry->state != PW_STREAM_STATE_ERROR && !connection->lost)
    {
        int64_t remaining = deadline - maudPipewireNow();
        if (remaining <= 0)
        {
            break;
        }
        pw_loop_iterate(connection->loop, (int)(remaining / 1000000 + 1));
    }
    pw_loop_leave(connection->loop);
    return entry->state == PW_STREAM_STATE_PAUSED || entry->state == PW_STREAM_STATE_STREAMING;
}

// Creates and connects the slot's pw_stream, inactive. With wait, it
// waits up to the deadline for PipeWire to accept the format, unless
// the stream has no device to negotiate with yet.
static maudResult ConnectStream(maudContext* context, maudStreamSlot* slot, bool wait)
{
    maudPipewire* pipewire = context->native;
    maudPipewireStream* entry = EntryOf(context, slot);
    maudStreamCore* core = &slot->core;
    // While the daemon is away the stream waits without a pw_stream; it
    // gets one when a new core connects.
    if (pipewire->connection.core == nullptr)
    {
        return maud_success;
    }
    struct pw_properties* props = StreamProperties(context, core);
    if (props == nullptr)
    {
        return maud_errorPlatform;
    }
    *entry = (maudPipewireStream){.owner = pipewire, .core = core};
    entry->stream = pipewire->api.streamNew(pipewire->connection.core, "Maul Audio", props);
    if (entry->stream == nullptr)
    {
        return maud_errorPlatform;
    }
    entry->used = true;
    pipewire->api.streamAddListener(entry->stream, &entry->listener, &s_streamEvents, entry);
    uint8_t buffer[FORMAT_POD_BYTES];
    const struct spa_pod* params[] = {BuildFormat(core, buffer)};
    enum pw_stream_flags flags = PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                                 PW_STREAM_FLAG_RT_PROCESS | PW_STREAM_FLAG_INACTIVE;
    bool output = core->def.direction == maud_directionOutput;
    int connected = pipewire->api.streamConnect(entry->stream,
                                                output ? PW_DIRECTION_OUTPUT : PW_DIRECTION_INPUT,
                                                PW_ID_ANY, flags, params, 1);
    bool waiting = !wait || core->binding.current.index1 == 0;
    if (connected < 0 || (!waiting && !WaitForFormat(pipewire, entry)))
    {
        maudPipewireDetachStream(context, slot);
        return maud_errorPlatform;
    }
    // A stream that runs already, as after a new rate or a new core,
    // starts active; a new one waits for its start.
    if (atomic_load_explicit(&core->state, memory_order_acquire) == maud_streamRunning)
    {
        pipewire->api.streamSetActive(entry->stream, true);
    }
    return maud_success;
}

maudResult maudPipewireAttachStream(maudContext* context, maudStreamSlot* slot)
{
    return ConnectStream(context, slot, true);
}

void maudPipewireDetachStream(maudContext* context, maudStreamSlot* slot)
{
    maudPipewire* pipewire = context->native;
    maudPipewireStream* entry = EntryOf(context, slot);
    if (!entry->used)
    {
        return;
    }
    // No callback may reach the host once this returns.
    atomic_store(&entry->closing, true);
    while (atomic_load(&entry->inside) != 0)
    {
        sched_yield();
    }
    spa_hook_remove(&entry->listener);
    pipewire->api.streamDestroy(entry->stream);
    *entry = (maudPipewireStream){0};
}

void maudPipewireSetStreamActive(maudContext* context, maudStreamSlot* slot, bool active)
{
    maudPipewireStream* entry = EntryOf(context, slot);
    if (entry->used)
    {
        // The gap since the stream last ran is not an xrun.
        atomic_store_explicit(&entry->forgetTicks, true, memory_order_release);
        entry->owner->api.streamSetActive(entry->stream, active);
    }
}

void maudPipewireRetargetStream(maudContext* context, maudStreamSlot* slot)
{
    // A negotiated pw_stream keeps its format when it is offered others,
    // so a new rate takes a new stream.
    if (!EntryOf(context, slot)->used)
    {
        return;
    }
    maudPipewireDetachStream(context, slot);
    // A failure leaves the entry unused; the next drain tries again.
    maudResult result = ConnectStream(context, slot, false);
    (void)result;
}

void maudPipewireDropStreams(maudContext* context)
{
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        if (context->streams.slots[i].live)
        {
            maudPipewireDetachStream(context, &context->streams.slots[i]);
        }
    }
}

void maudPipewireReconnectStreams(maudContext* context)
{
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        maudStreamSlot* slot = &context->streams.slots[i];
        const maudStreamBinding* binding = &slot->core.binding;
        bool wanted = binding->requested.index1 == 0 || binding->suspension == maud_suspendNone;
        if (slot->live && wanted && !EntryOf(context, slot)->used)
        {
            // A failure leaves the entry unused, so the next drain tries
            // again.
            maudResult result = ConnectStream(context, slot, false);
            (void)result;
        }
    }
}
