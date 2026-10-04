// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Streams on ALSA. Each stream opens its PCM when it is created. While
// it runs, one library thread polls the PCM's descriptors and an
// eventfd, and moves what the PCM can take or has through the stream's
// fixed-period adapter, reordering channels when the PCM's order is not
// the stream's. Stopping writes the eventfd, joins the thread and drops
// what the PCM had queued.

#include "alsa_stream.h"

#include "alsa_core.h"
#include "clock.h"
#include "context.h"
#include "period.h"
#include "thread.h"
#include "xrun.h"

#include <errno.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

// Periods in the device buffer.
#define BUFFER_PERIODS 3u

static maudAlsaStream* EntryOf(maudContext* context, const maudStreamSlot* slot)
{
    maudAlsa* alsa = context->native;
    return &alsa->streams[slot - context->streams.slots];
}

// Moves frames between samples and the adapter, as the stream runs or
// not: playback fills samples, capture empties them.
// Stamps the clock from the frames between the application and the
// device: ahead of a rendered buffer, or behind a captured one, which
// adds its own length back to its first frame.
static void Stamp(maudAlsaStream* entry, uint32_t frames, bool output)
{
    snd_pcm_sframes_t delay = 0;
    if (entry->api->pcmDelay(entry->pcm, &delay) != 0 || delay < 0)
    {
        delay = 0;
    }
    uint64_t behind = (uint64_t)delay + (output ? 0u : frames);
    int64_t latency = (int64_t)(behind * 1000000000u / entry->core->format.sampleRate);
    if (output)
    {
        maudStampOutputClock(entry->core, latency);
    }
    else
    {
        maudStampInputClock(entry->core, latency);
    }
}

static void Render(maudAlsaStream* entry, float* samples, uint32_t frames, bool output)
{
    maudStreamCore* core = entry->core;
    bool running = atomic_load_explicit(&core->state, memory_order_acquire) == maud_streamRunning;
    atomic_store_explicit(&core->renderingThread, maudCurrentThread(), memory_order_release);
    core->period.sampleRate = atomic_load_explicit(&core->blockRate, memory_order_acquire);
    if (output && running)
    {
        maudPullPeriod(&core->period, samples, frames);
    }
    else if (output)
    {
        memset(samples, 0, (size_t)frames * core->period.channelCount * sizeof(float));
    }
    else if (running)
    {
        maudPushPeriod(&core->period, samples, frames);
    }
    atomic_store_explicit(&core->renderingThread, 0, memory_order_release);
    Stamp(entry, frames, output);
    atomic_fetch_add_explicit(&core->position, frames, memory_order_release);
}

// Copies frames from one channel order to the other: to the PCM's for
// playback, from it for capture.
static void Reorder(const maudAlsaStream* entry, uint32_t frames, bool output)
{
    uint32_t channels = entry->core->period.channelCount;
    for (uint32_t f = 0; f < frames; ++f)
    {
        float* stream = entry->samples + (size_t)f * channels;
        float* pcm = entry->reordered + (size_t)f * channels;
        for (uint32_t c = 0; c < channels; ++c)
        {
            if (output)
            {
                pcm[c] = stream[entry->order[c]];
            }
            else
            {
                stream[entry->order[c]] = pcm[c];
            }
        }
    }
}

// Writes what the PCM can take: first what an earlier write left
// pending, then newly rendered frames.
static snd_pcm_sframes_t WriteOut(maudAlsaStream* entry, snd_pcm_sframes_t avail)
{
    uint32_t channels = entry->core->period.channelCount;
    float* pcmSamples = entry->reordered != nullptr ? entry->reordered : entry->samples;
    while (avail > 0)
    {
        if (entry->pendingFrames == 0)
        {
            uint32_t frames =
                (uint32_t)avail < entry->bufferFrames ? (uint32_t)avail : entry->bufferFrames;
            Render(entry, entry->samples, frames, true);
            if (entry->reordered != nullptr)
            {
                Reorder(entry, frames, true);
            }
            entry->pendingOffset = 0;
            entry->pendingFrames = frames;
        }
        snd_pcm_sframes_t moved = entry->api->pcmWritei(
            entry->pcm, pcmSamples + (size_t)entry->pendingOffset * channels, entry->pendingFrames);
        if (moved <= 0)
        {
            return moved;
        }
        entry->pendingOffset += (uint32_t)moved;
        entry->pendingFrames -= (uint32_t)moved;
        avail -= moved;
    }
    return 0;
}

// Reads what the PCM has and passes it on.
static snd_pcm_sframes_t ReadIn(maudAlsaStream* entry, snd_pcm_sframes_t avail)
{
    float* pcmSamples = entry->reordered != nullptr ? entry->reordered : entry->samples;
    while (avail > 0)
    {
        uint32_t frames =
            (uint32_t)avail < entry->bufferFrames ? (uint32_t)avail : entry->bufferFrames;
        snd_pcm_sframes_t moved = entry->api->pcmReadi(entry->pcm, pcmSamples, frames);
        if (moved <= 0)
        {
            return moved;
        }
        if (entry->reordered != nullptr)
        {
            Reorder(entry, (uint32_t)moved, false);
        }
        Render(entry, entry->samples, (uint32_t)moved, false);
        avail -= moved;
    }
    return 0;
}

// Moves what the PCM can take or has, recovering from an xrun; a
// recovered capture is started again. False when the PCM failed past
// recovery, as when its card went away.
static bool Transfer(maudAlsaStream* entry)
{
    const maudAlsaApi* api = entry->api;
    bool output = entry->core->def.direction == maud_directionOutput;
    snd_pcm_sframes_t result = api->pcmAvailUpdate(entry->pcm);
    if (result > 0)
    {
        result = output ? WriteOut(entry, result) : ReadIn(entry, result);
    }
    if (result >= 0 || result == -EAGAIN)
    {
        return true;
    }
    // -EPIPE is an xrun: the PCM ran dry, or overflowed.
    if (result == -EPIPE)
    {
        maudCountXrun(entry->core);
    }
    if (api->pcmRecover(entry->pcm, (int)result, 1) < 0)
    {
        return false;
    }
    if (!output)
    {
        api->pcmStart(entry->pcm);
    }
    return true;
}

// The stream's thread: polls until the eventfd is written, or the PCM
// fails past recovery.
static void RunStream(void* user)
{
    maudAlsaStream* entry = user;
    const maudAlsaApi* api = entry->api;
    maudAlsaQuiet(api);
    if (entry->core->def.direction == maud_directionInput)
    {
        api->pcmStart(entry->pcm);
    }
    for (;;)
    {
        if (poll(entry->fds, entry->fdCount, -1) < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return;
        }
        if (entry->fds[0].revents != 0)
        {
            return;
        }
        unsigned short events = 0;
        api->pollDescriptorsRevents(entry->pcm, entry->fds + 1, entry->fdCount - 1, &events);
        // A PCM failed past recovery stays silent until the stream stops.
        if ((events & (POLLIN | POLLOUT | POLLERR | POLLHUP | POLLNVAL)) != 0 && !Transfer(entry))
        {
            return;
        }
    }
}

static bool Hardware(const maudDeviceSlot* device)
{
    return device != nullptr && device->key.length >= 3 && memcmp(device->key.bytes, "hw:", 3) == 0;
}

// The PCM name a device key stands for: plughw for a hardware endpoint.
static void PcmName(const maudContext* context, const maudStreamCore* core, char* name)
{
    const maudDeviceSlot* device = maudFindDevice(context, core->binding.current);
    if (!Hardware(device) || device->key.length + 4 >= MAUD_ALSA_NAME_BYTES)
    {
        memcpy(name, "default", 8);
        return;
    }
    memcpy(name, "plug", 4);
    memcpy(name + 4, device->key.bytes, device->key.length);
    name[4 + device->key.length] = '\0';
}

// Sets the hardware parameters: float frames in the stream's channel
// count, the rate by its policy, and three periods of buffer. A native
// stream takes the rate the hardware has nearest the preferred one.
static maudResult SetHardware(maudAlsaStream* entry, snd_pcm_uframes_t* periodOut,
                              snd_pcm_uframes_t* bufferOut)
{
    const maudAlsaApi* api = entry->api;
    maudStreamCore* core = entry->core;
    alignas(max_align_t) unsigned char bytes[MAUD_ALSA_STRUCT_BYTES];
    memset(bytes, 0, sizeof(bytes));
    snd_pcm_hw_params_t* params = (snd_pcm_hw_params_t*)bytes;
    unsigned int rate = core->format.sampleRate;
    bool native = core->format.ratePolicy == maud_rateNative;
    snd_pcm_uframes_t period = core->format.periodFrames;
    snd_pcm_uframes_t buffer = period * BUFFER_PERIODS;
    if (api->hwParamsAny(entry->pcm, params) < 0 ||
        api->hwParamsSetAccess(entry->pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0 ||
        api->hwParamsSetFormat(entry->pcm, params, SND_PCM_FORMAT_FLOAT) < 0 ||
        api->hwParamsSetChannels(entry->pcm, params, core->period.channelCount) < 0)
    {
        return maud_errorUnsupported;
    }
    bool resample = core->format.ratePolicy == maud_ratePlatformConverted;
    api->hwParamsSetRateResample(entry->pcm, params, resample ? 1 : 0);
    int set = native ? api->hwParamsSetRateNear(entry->pcm, params, &rate, nullptr)
                     : api->hwParamsSetRate(entry->pcm, params, rate, 0);
    if (set < 0)
    {
        return maud_errorUnsupported;
    }
    api->hwParamsSetPeriodSizeNear(entry->pcm, params, &period, nullptr);
    api->hwParamsSetBufferSizeNear(entry->pcm, params, &buffer);
    if (api->hwParams(entry->pcm, params) < 0)
    {
        return maud_errorPlatform;
    }
    api->hwParamsGetPeriodSize(params, periodOut, nullptr);
    api->hwParamsGetBufferSize(params, bufferOut);
    core->format.sampleRate = rate;
    atomic_store_explicit(&core->blockRate, rate, memory_order_release);
    return maud_success;
}

// Starts playback once a period is queued. The thread wakes when a
// period can move, which is ALSA's default.
static maudResult SetSoftware(maudAlsaStream* entry, snd_pcm_uframes_t period)
{
    const maudAlsaApi* api = entry->api;
    alignas(max_align_t) unsigned char bytes[MAUD_ALSA_STRUCT_BYTES];
    memset(bytes, 0, sizeof(bytes));
    snd_pcm_sw_params_t* params = (snd_pcm_sw_params_t*)bytes;
    bool ok = api->swParamsCurrent(entry->pcm, params) == 0 &&
              api->swParamsSetStartThreshold(entry->pcm, params, period) == 0 &&
              api->swParams(entry->pcm, params) == 0;
    return ok ? maud_success : maud_errorPlatform;
}

// Finds the PCM's channel order against the stream's.
static maudResult SetOrder(maudAlsaStream* entry, bool* reorderOut)
{
    const maudAlsaApi* api = entry->api;
    uint32_t channels = entry->core->period.channelCount;
    snd_pcm_chmap_t* map = api->pcmGetChmap(entry->pcm);
    const unsigned int* positions =
        map != nullptr && map->channels == channels ? map->pos : nullptr;
    bool found =
        maudAlsaChannelOrder(entry->core->format.layout, positions, channels, entry->order);
    api->chmapFree(map);
    *reorderOut = found && !maudAlsaOrderIsIdentity(entry->order, channels);
    return found ? maud_success : maud_errorUnsupported;
}

static void Close(maudContext* context, maudAlsaStream* entry)
{
    if (entry->pcm != nullptr)
    {
        entry->api->pcmClose(entry->pcm);
    }
    if (entry->fds != nullptr && entry->fds[0].fd >= 0)
    {
        close(entry->fds[0].fd);
    }
    if (entry->bytes != 0)
    {
        maudContextRelease(context, entry->fds, entry->bytes, alignof(max_align_t));
    }
    *entry = (maudAlsaStream){0};
}

// Sets aside the descriptors and sample buffers, and makes the eventfd.
static maudResult Allocate(maudContext* context, maudAlsaStream* entry, bool reorder)
{
    const maudAlsaApi* api = entry->api;
    int count = api->pollDescriptorsCount(entry->pcm);
    if (count <= 0)
    {
        return maud_errorPlatform;
    }
    size_t samples = (size_t)entry->bufferFrames * entry->core->period.channelCount;
    size_t fdBytes = ((size_t)count + 1) * sizeof(struct pollfd);
    fdBytes = (fdBytes + alignof(max_align_t) - 1) / alignof(max_align_t) * alignof(max_align_t);
    entry->bytes = fdBytes + samples * sizeof(float) * (reorder ? 2 : 1);
    unsigned char* block = maudContextAllocate(context, entry->bytes, alignof(max_align_t));
    if (block == nullptr)
    {
        entry->bytes = 0;
        return maud_errorCapacity;
    }
    entry->fds = (struct pollfd*)block;
    entry->samples = (float*)(block + fdBytes);
    entry->reordered = reorder ? entry->samples + samples : nullptr;
    entry->fdCount = (uint32_t)count + 1;
    entry->fds[0] = (struct pollfd){.fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC), .events = POLLIN};
    if (entry->fds[0].fd < 0)
    {
        return maud_errorPlatform;
    }
    return api->pollDescriptors(entry->pcm, entry->fds + 1, (unsigned int)count) == count
               ? maud_success
               : maud_errorPlatform;
}

static maudResult Open(maudContext* context, maudAlsaStream* entry)
{
    const maudAlsaApi* api = entry->api;
    maudStreamCore* core = entry->core;
    char name[MAUD_ALSA_NAME_BYTES];
    PcmName(context, core, name);
    // A hardware PCM keeps every other client off while it is open.
    core->exclusive = core->exclusive || Hardware(maudFindDevice(context, core->binding.current));
    bool output = core->def.direction == maud_directionOutput;
    if (api->pcmOpen(&entry->pcm, name, output ? SND_PCM_STREAM_PLAYBACK : SND_PCM_STREAM_CAPTURE,
                     SND_PCM_NONBLOCK) < 0)
    {
        entry->pcm = nullptr;
        return maud_errorPlatform;
    }
    snd_pcm_uframes_t period = 0;
    snd_pcm_uframes_t buffer = 0;
    bool reorder = false;
    maudResult result = SetHardware(entry, &period, &buffer);
    result = result == maud_success ? SetSoftware(entry, period) : result;
    result = result == maud_success ? SetOrder(entry, &reorder) : result;
    entry->bufferFrames = (uint32_t)buffer;
    return result == maud_success ? Allocate(context, entry, reorder) : result;
}

maudResult maudAlsaAttachStream(maudContext* context, maudStreamSlot* slot)
{
    maudAlsa* alsa = context->native;
    maudAlsaStream* entry = EntryOf(context, slot);
    *entry = (maudAlsaStream){.api = &alsa->api, .core = &slot->core};
    snd_local_error_handler_t previous = maudAlsaQuiet(&alsa->api);
    maudResult result = Open(context, entry);
    alsa->api.libErrorSetLocal(previous);
    if (result != maud_success)
    {
        Close(context, entry);
    }
    return result;
}

static void Stop(maudAlsaStream* entry)
{
    if (!entry->threadRunning)
    {
        return;
    }
    uint64_t one = 1;
    ssize_t written = write(entry->fds[0].fd, &one, sizeof(one));
    (void)written;
    maudJoinWorker(&entry->worker);
    entry->threadRunning = false;
    entry->pendingFrames = 0;
    ssize_t read_ = read(entry->fds[0].fd, &one, sizeof(one));
    (void)read_;
    snd_local_error_handler_t previous = maudAlsaQuiet(entry->api);
    entry->api->pcmDrop(entry->pcm);
    entry->api->libErrorSetLocal(previous);
}

void maudAlsaDetachStream(maudContext* context, maudStreamSlot* slot)
{
    const maudAlsaApi* api = &((maudAlsa*)context->native)->api;
    maudAlsaStream* entry = EntryOf(context, slot);
    Stop(entry);
    snd_local_error_handler_t previous = maudAlsaQuiet(api);
    Close(context, entry);
    api->libErrorSetLocal(previous);
}

void maudAlsaSetStreamActive(maudContext* context, maudStreamSlot* slot, bool active)
{
    maudAlsaStream* entry = EntryOf(context, slot);
    if (!active)
    {
        Stop(entry);
        return;
    }
    if (entry->threadRunning || entry->pcm == nullptr)
    {
        return;
    }
    snd_local_error_handler_t previous = maudAlsaQuiet(entry->api);
    entry->api->pcmPrepare(entry->pcm);
    entry->api->libErrorSetLocal(previous);
    entry->threadRunning = true;
    if (!maudStartWorker(&entry->worker, RunStream, entry, "maud-alsa"))
    {
        entry->threadRunning = false;
    }
}
