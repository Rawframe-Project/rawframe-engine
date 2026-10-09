// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Streams on WASAPI. Each stream has a shared-mode, event-driven audio
// client on its device. While it runs, one library thread, in the MTA
// and registered with MMCSS, waits on the client's event and moves the
// buffer's free space or every captured packet through the stream's
// fixed-period adapter.

#include "wasapi_stream.h"

#include "clock.h"
#include "context.h"
#include "period.h"
#include "thread.h"
#include "voice.h"
#include "wasapi_core.h"
#include "wasapi_exclusive.h"
#include "wasapi_format.h"
#include "wasapi_voice.h"
#include "xrun.h"

#include <avrt.h>
#include <mmreg.h>
#include <string.h>

static const GUID s_iidAudioClient = {
    0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
static const GUID s_iidRenderClient = {
    0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}};
static const GUID s_iidCaptureClient = {
    0xC8ADBD64, 0xE71E, 0x48A0, {0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17}};
static const GUID s_iidAudioClock = {
    0xCD63314F, 0x3FBA, 0x4A1B, {0x81, 0x2C, 0xEF, 0x96, 0x35, 0x87, 0x28, 0xE7}};
static const GUID s_subtypeFloat = {
    0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

// Periods of buffer asked for; WASAPI raises it to its minimum.
#define BUFFER_PERIODS 2u

static maudWasapiStream* EntryOf(maudContext* context, const maudStreamSlot* slot)
{
    maudWasapi* wasapi = context->native;
    return &wasapi->streams[slot - context->streams.slots];
}

// Moves frames between the device's buffer and the adapter: into out
// for render, from in for capture.
static void Render(maudWasapiStream* entry, float* out, const float* in, uint32_t frames,
                   int64_t latency)
{
    maudStreamCore* core = entry->core;
    bool running = atomic_load_explicit(&core->state, memory_order_acquire) == maud_streamRunning;
    atomic_store_explicit(&core->renderingThread, maudCurrentThread(), memory_order_release);
    core->period.sampleRate = atomic_load_explicit(&core->blockRate, memory_order_acquire);
    if (out != nullptr && running)
    {
        maudPullPeriod(&core->period, out, frames);
    }
    else if (out != nullptr)
    {
        memset(out, 0, (size_t)frames * core->period.channelCount * sizeof(float));
    }
    else if (running)
    {
        maudPushPeriod(&core->period, in, frames);
    }
    atomic_store_explicit(&core->renderingThread, 0, memory_order_release);
    if (out != nullptr)
    {
        maudStampOutputClock(core, latency);
    }
    else
    {
        maudStampInputClock(core, latency);
    }
    atomic_fetch_add_explicit(&core->position, frames, memory_order_release);
}

// A performance-counter time in 100-nanosecond units as host
// nanoseconds, or fallback when it is 0 or more than a second from now
// (wine's counter times overflow).
static int64_t CounterTime(UINT64 counter, int64_t now, int64_t fallback)
{
    int64_t time = (int64_t)counter * 100;
    bool plausible = counter != 0 && time > now - 1000000000 && time < now + 1000000000;
    return plausible ? time : fallback;
}

// How far from now the next frame written is heard: the device's
// position, at the performance-counter time it was read, is behind the
// frames written by what is still to play. 0 when the clock cannot say.
static int64_t OutputLatency(const maudWasapiStream* entry)
{
    UINT64 position = 0;
    UINT64 counter = 0;
    if (entry->clock == nullptr || entry->clockFrequency == 0 ||
        FAILED(IAudioClock_GetPosition(entry->clock, &position, &counter)))
    {
        return 0;
    }
    uint32_t rate = entry->core->format.sampleRate;
    uint64_t heard = position * rate / entry->clockFrequency;
    int64_t ahead = entry->written > heard ? (int64_t)(entry->written - heard) : 0;
    int64_t now = maudNowNanoseconds();
    return CounterTime(counter, now, now) - now + ahead * 1000000000 / rate;
}

// Fills the render buffer's free space.
// One pass of an object stream: the period's bed and objects to the
// spatial render stream. Windows says nothing of its latency here.
static HRESULT FillObjects(maudWasapiStream* entry)
{
    maudStreamCore* core = entry->core;
    bool running = atomic_load_explicit(&core->state, memory_order_acquire) == maud_streamRunning;
    atomic_store_explicit(&core->renderingThread, maudCurrentThread(), memory_order_release);
    core->period.sampleRate = atomic_load_explicit(&core->blockRate, memory_order_acquire);
    UINT32 frames = 0;
    HRESULT result = maudWasapiRenderObjects(&entry->objects, core, running, &frames);
    atomic_store_explicit(&core->renderingThread, 0, memory_order_release);
    maudStampOutputClock(core, 0);
    atomic_fetch_add_explicit(&core->position, frames, memory_order_release);
    return result;
}

static HRESULT Fill(maudWasapiStream* entry)
{
    if (entry->objects.stream != nullptr)
    {
        return FillObjects(entry);
    }
    // An exclusive client takes a whole buffer at each event.
    UINT32 padding = 0;
    HRESULT result =
        entry->exclusive ? S_OK : IAudioClient_GetCurrentPadding(entry->client, &padding);
    UINT32 frames = entry->bufferFrames - padding;
    if (FAILED(result) || frames == 0)
    {
        return result;
    }
    // Empty once playing: the engine played what the stream did not give.
    if (!entry->exclusive && padding == 0 && entry->written > 0)
    {
        maudCountXrun(entry->core);
    }
    BYTE* data = nullptr;
    result = IAudioRenderClient_GetBuffer(entry->render, frames, &data);
    if (FAILED(result))
    {
        return result;
    }
    if (entry->scratch == nullptr)
    {
        Render(entry, (float*)data, nullptr, frames, OutputLatency(entry));
    }
    else
    {
        Render(entry, entry->scratch, nullptr, frames, OutputLatency(entry));
        maudFloatToSamples(data, entry->scratch, (size_t)frames * entry->core->period.channelCount,
                           entry->sampleKind);
    }
    entry->written += frames;
    return IAudioRenderClient_ReleaseBuffer(entry->render, frames, 0);
}

// Takes every captured packet; a silent one passes on zeros.
static HRESULT Drain(maudWasapiStream* entry)
{
    UINT32 packet = 0;
    HRESULT result = S_OK;
    while (SUCCEEDED(result = IAudioCaptureClient_GetNextPacketSize(entry->capture, &packet)) &&
           packet > 0)
    {
        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        UINT64 counter = 0;
        result = IAudioCaptureClient_GetBuffer(entry->capture, &data, &frames, &flags, nullptr,
                                               &counter);
        if (FAILED(result))
        {
            return result;
        }
        // A discontinuity past the first packet lost data; the first
        // follows the start, a state transition.
        if ((flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0 && entry->written > 0)
        {
            maudCountXrun(entry->core);
        }
        entry->written += frames;
        for (UINT32 done = 0; done < frames;)
        {
            UINT32 chunk =
                frames - done < entry->bufferFrames ? frames - done : entry->bufferFrames;
            size_t channels = entry->core->period.channelCount;
            const float* samples = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0
                                       ? entry->zeros
                                       : (const float*)data + (size_t)done * channels;
            if (entry->scratch != nullptr && samples != entry->zeros)
            {
                const BYTE* source =
                    data + (size_t)done * channels * maudSampleBytes(entry->sampleKind);
                maudSamplesToFloat(entry->scratch, source, (size_t)chunk * channels,
                                   entry->sampleKind);
                samples = entry->scratch;
            }
            // The packet's first frame was captured at the performance
            // counter's time, or at least the packet's length ago; later
            // chunks were captured later by what came before them.
            uint32_t rate = entry->core->format.sampleRate;
            int64_t now = maudNowNanoseconds();
            int64_t start = CounterTime(counter, now, now - (int64_t)frames * 1000000000 / rate);
            int64_t captured = start + (int64_t)done * 1000000000 / rate;
            Render(entry, nullptr, samples, chunk, now - captured);
            done += chunk;
        }
        result = IAudioCaptureClient_ReleaseBuffer(entry->capture, frames);
        if (FAILED(result))
        {
            return result;
        }
    }
    return result;
}

// Runs in the MTA under MMCSS: fills a render buffer before starting,
// then serves each period until the stop event or a failure.
static void Serve(maudWasapiStream* entry)
{
    bool objects = entry->objects.stream != nullptr;
    bool output = entry->render != nullptr || objects;
    // An object stream's passes begin once its stream has started.
    HRESULT result = output && !objects ? Fill(entry) : S_OK;
    if (SUCCEEDED(result))
    {
        result = objects ? ISpatialAudioObjectRenderStream_Start(entry->objects.stream)
                         : IAudioClient_Start(entry->client);
    }
    HANDLE events[2] = {entry->stopEvent, entry->bufferEvent};
    while (SUCCEEDED(result) &&
           WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0 + 1)
    {
        result = output ? Fill(entry) : Drain(entry);
    }
    atomic_store_explicit(&entry->failed, FAILED(result), memory_order_release);
}

static void RunStream(void* user)
{
    maudWasapiStream* entry = user;
    bool joined = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    DWORD task = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);
    Serve(entry);
    if (mmcss != nullptr)
    {
        AvRevertMmThreadCharacteristics(mmcss);
    }
    if (joined)
    {
        CoUninitialize();
    }
}

static void Disconnect(maudContext* context, maudWasapiStream* entry)
{
    maudWasapiCloseObjects(context, &entry->objects);
    if (entry->render != nullptr)
    {
        IAudioRenderClient_Release(entry->render);
    }
    if (entry->clock != nullptr)
    {
        IAudioClock_Release(entry->clock);
    }
    if (entry->capture != nullptr)
    {
        IAudioCaptureClient_Release(entry->capture);
    }
    if (entry->client != nullptr)
    {
        IAudioClient_Release(entry->client);
    }
    if (entry->bufferEvent != nullptr)
    {
        CloseHandle(entry->bufferEvent);
    }
    if (entry->stopEvent != nullptr)
    {
        CloseHandle(entry->stopEvent);
    }
    if (entry->zeros != nullptr)
    {
        maudContextRelease(context, entry->zeros, entry->zeroBytes, alignof(float));
    }
    if (entry->scratch != nullptr)
    {
        maudContextRelease(context, entry->scratch, entry->scratchBytes, alignof(float));
    }
    maudStreamCore* core = entry->core;
    *entry = (maudWasapiStream){.core = core};
}

// The stream's device as an IMMDevice, or NULL.
static IMMDevice* OpenDevice(maudContext* context, const maudStreamCore* core)
{
    maudWasapi* wasapi = context->native;
    const maudDeviceSlot* slot = maudFindDevice(context, core->binding.current);
    wchar_t id[MAUD_WASAPI_KEY_BYTES];
    IMMDevice* device = nullptr;
    int length = slot != nullptr
                     ? MultiByteToWideChar(CP_UTF8, 0, slot->key.bytes, (int)slot->key.length, id,
                                           MAUD_WASAPI_KEY_BYTES - 1)
                     : 0;
    if (length <= 0)
    {
        return nullptr;
    }
    id[length] = 0;
    return SUCCEEDED(IMMDeviceEnumerator_GetDevice(wasapi->enumerator, id, &device)) ? device
                                                                                     : nullptr;
}

// The stream's wave format: 32-bit float at its rate with its layout's
// speaker mask.
static WAVEFORMATEXTENSIBLE FormatOf(const maudStreamCore* core)
{
    uint32_t channels = core->period.channelCount;
    uint32_t rate = core->format.sampleRate;
    return (WAVEFORMATEXTENSIBLE){
        .Format =
            {
                .wFormatTag = WAVE_FORMAT_EXTENSIBLE,
                .nChannels = (WORD)channels,
                .nSamplesPerSec = rate,
                .nAvgBytesPerSec = rate * channels * (DWORD)sizeof(float),
                .nBlockAlign = (WORD)(channels * sizeof(float)),
                .wBitsPerSample = 32,
                .cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX),
            },
        .Samples = {.wValidBitsPerSample = 32},
        .dwChannelMask = maudWasapiMaskOfLayout(core->format.layout),
        .SubFormat = s_subtypeFloat,
    };
}

// Initializes the client in shared, event-driven mode, converting
// channels always and the rate only for a platform-converted stream.
static maudResult Initialize(maudWasapiStream* entry)
{
    const maudStreamCore* core = entry->core;
    WAVEFORMATEXTENSIBLE format = FormatOf(core);
    DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;
    if (core->format.ratePolicy == maud_ratePlatformConverted)
    {
        flags |= AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    }
    REFERENCE_TIME duration = (REFERENCE_TIME)core->format.periodFrames * BUFFER_PERIODS *
                              10000000 / core->format.sampleRate;
    HRESULT result = IAudioClient_Initialize(entry->client, AUDCLNT_SHAREMODE_SHARED, flags,
                                             duration, 0, (WAVEFORMATEX*)&format, nullptr);
    if (result == AUDCLNT_E_UNSUPPORTED_FORMAT)
    {
        return maud_errorUnsupported;
    }
    return SUCCEEDED(result) ? maud_success : maud_errorPlatform;
}

// A buffer of floats for an integer format, and an input's buffer of
// silence, each a buffer's worth of frames.
static maudResult AllocateBuffers(maudContext* context, maudWasapiStream* entry, UINT32 frames,
                                  bool output)
{
    size_t bytes = (size_t)frames * entry->core->period.channelCount * sizeof(float);
    if (entry->sampleKind != maud_sampleFloat32)
    {
        entry->scratchBytes = bytes;
        entry->scratch = maudContextAllocate(context, bytes, alignof(float));
        if (entry->scratch == nullptr)
        {
            return maud_errorCapacity;
        }
    }
    if (!output)
    {
        entry->zeroBytes = bytes;
        entry->zeros = maudContextAllocate(context, bytes, alignof(float));
        if (entry->zeros == nullptr)
        {
            return maud_errorCapacity;
        }
        memset(entry->zeros, 0, bytes);
    }
    return maud_success;
}

// An object stream's events and spatial render stream, on device.
static maudResult ConnectObjects(maudContext* context, maudWasapiStream* entry, IMMDevice* device)
{
    entry->bufferEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    entry->stopEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (entry->bufferEvent == nullptr || entry->stopEvent == nullptr)
    {
        return maud_errorPlatform;
    }
    return maudWasapiOpenObjects(context, device, entry->core, entry->bufferEvent, &entry->objects);
}

// Opens and initializes the client and its service, and the events.
static maudResult Connect(maudContext* context, maudWasapiStream* entry)
{
    // A new client on a new device reports again, if it can.
    maudResetVoice(entry->core);
    IMMDevice* device = OpenDevice(context, entry->core);
    if (device == nullptr)
    {
        return maud_errorPlatform;
    }
    if (entry->core->def.objectCount > 0)
    {
        maudResult connected = ConnectObjects(context, entry, device);
        IMMDevice_Release(device);
        return connected;
    }
    HRESULT activated =
        IMMDevice_Activate(device, &s_iidAudioClient, CLSCTX_ALL, nullptr, (void**)&entry->client);
    if (FAILED(activated))
    {
        IMMDevice_Release(device);
        entry->client = nullptr;
        return maud_errorPlatform;
    }
    // An exclusive client bypasses the audio engine and its processing.
    entry->exclusive = entry->core->def.share == maud_shareExclusive;
    if (!entry->exclusive)
    {
        maudWasapiAskForVoice(entry->client, entry->core);
    }
    maudResult result =
        entry->exclusive ? maudWasapiInitializeExclusive(entry, device) : Initialize(entry);
    IMMDevice_Release(device);
    if (result != maud_success)
    {
        return result;
    }
    if (!entry->exclusive)
    {
        maudWasapiReportVoice(entry->client, entry->core);
    }
    else if (entry->core->def.direction == maud_directionInput)
    {
        maudReportVoice(entry->core, maud_voiceNone);
    }
    entry->bufferEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    entry->stopEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    UINT32 frames = 0;
    bool output = entry->core->def.direction == maud_directionOutput;
    bool ok = entry->bufferEvent != nullptr && entry->stopEvent != nullptr &&
              SUCCEEDED(IAudioClient_SetEventHandle(entry->client, entry->bufferEvent)) &&
              SUCCEEDED(IAudioClient_GetBufferSize(entry->client, &frames)) &&
              SUCCEEDED(output ? IAudioClient_GetService(entry->client, &s_iidRenderClient,
                                                         (void**)&entry->render)
                               : IAudioClient_GetService(entry->client, &s_iidCaptureClient,
                                                         (void**)&entry->capture));
    if (!ok)
    {
        return maud_errorPlatform;
    }
    entry->bufferFrames = frames;
    // The clock is for the stream clock only; without it the latency
    // reads 0.
    if (output &&
        FAILED(IAudioClient_GetService(entry->client, &s_iidAudioClock, (void**)&entry->clock)))
    {
        entry->clock = nullptr;
    }
    if (entry->clock != nullptr &&
        FAILED(IAudioClock_GetFrequency(entry->clock, &entry->clockFrequency)))
    {
        entry->clockFrequency = 0;
    }
    return AllocateBuffers(context, entry, frames, output);
}

// Connects the stream's client if it has a device; a stream without one
// waits.
static maudResult Open(maudContext* context, maudStreamSlot* slot)
{
    maudWasapiStream* entry = EntryOf(context, slot);
    *entry = (maudWasapiStream){.core = &slot->core};
    if (slot->core.binding.current.index1 == 0)
    {
        return maud_success;
    }
    maudResult result = Connect(context, entry);
    if (result != maud_success)
    {
        Disconnect(context, entry);
        return result;
    }
    entry->device = slot->core.binding.current;
    return maud_success;
}

// Whether the stream has a client, or an object stream its spatial
// render stream.
static bool Connected(const maudWasapiStream* entry)
{
    return entry->client != nullptr || entry->objects.stream != nullptr;
}

static void Start(maudWasapiStream* entry)
{
    if (!Connected(entry) || entry->threadRunning)
    {
        return;
    }
    atomic_store_explicit(&entry->failed, false, memory_order_release);
    entry->threadRunning = true;
    if (!maudStartWorker(&entry->worker, RunStream, entry, "maud-wasapi"))
    {
        entry->threadRunning = false;
    }
}

// Stops the thread and the client, dropping what it had queued.
static void Stop(maudWasapiStream* entry)
{
    if (!entry->threadRunning)
    {
        return;
    }
    SetEvent(entry->stopEvent);
    maudJoinWorker(&entry->worker);
    entry->threadRunning = false;
    if (entry->objects.stream != nullptr)
    {
        ISpatialAudioObjectRenderStream_Stop(entry->objects.stream);
        ISpatialAudioObjectRenderStream_Reset(entry->objects.stream);
    }
    else
    {
        IAudioClient_Stop(entry->client);
        IAudioClient_Reset(entry->client);
    }
    entry->written = 0;
}

static bool Running(const maudStreamSlot* slot)
{
    return atomic_load_explicit(&slot->core.state, memory_order_acquire) == maud_streamRunning;
}

maudResult maudWasapiAttachStream(maudContext* context, maudStreamSlot* slot)
{
    return Open(context, slot);
}

void maudWasapiDetachStream(maudContext* context, maudStreamSlot* slot)
{
    maudWasapiStream* entry = EntryOf(context, slot);
    Stop(entry);
    Disconnect(context, entry);
}

void maudWasapiRetargetStream(maudContext* context, maudStreamSlot* slot)
{
    maudWasapiDetachStream(context, slot);
    // A failure leaves the stream without a client; the drain tries
    // again while it runs.
    if (Open(context, slot) == maud_success && Running(slot))
    {
        Start(EntryOf(context, slot));
    }
}

void maudWasapiSetStreamActive(maudContext* context, maudStreamSlot* slot, bool active)
{
    maudWasapiStream* entry = EntryOf(context, slot);
    if (!active)
    {
        Stop(entry);
        return;
    }
    if (!Connected(entry))
    {
        maudResult result = Open(context, slot);
        (void)result;
    }
    Start(entry);
}

// A live running stream whose thread ended on a failure, or that has
// no client.
static bool ToResume(maudContext* context, maudStreamSlot* slot)
{
    const maudWasapiStream* entry = EntryOf(context, slot);
    bool ended = entry->threadRunning && atomic_load_explicit(&entry->failed, memory_order_acquire);
    return slot->live && Running(slot) && (ended || !Connected(entry));
}

bool maudWasapiStreamsToResume(maudContext* context)
{
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        if (ToResume(context, &context->streams.slots[i]))
        {
            return true;
        }
    }
    return false;
}

void maudWasapiResumeStreams(maudContext* context)
{
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        maudStreamSlot* slot = &context->streams.slots[i];
        if (ToResume(context, slot))
        {
            maudWasapiRetargetStream(context, slot);
        }
    }
}
