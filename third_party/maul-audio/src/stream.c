// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Streams: checking defs, settling formats through the backend,
// starting and stopping, and rendering on the caller's thread for
// backends that render there.

#include "maul-audio/stream.h"

#include "backend.h"
#include "clock.h"
#include "context.h"
#include "duplex.h"
#include "follow.h"
#include "period.h"
#include "stream_open.h"
#include "thread.h"

#include "maul-audio/objects.h"

#include <stdckdint.h>

#define STREAM_DEF_COOKIE 0x6D617364u
#define MIN_RATE          8000u
#define MAX_RATE          384000u

maudStreamDef maudDefaultStreamDef(void)
{
    return (maudStreamDef){
        .cookie = STREAM_DEF_COOKIE,
        .direction = maud_directionOutput,
        .mode = maud_modeCallback,
        .ratePolicy = maud_rateNative,
        .layout = maud_layoutStereo,
        .sampleRate = 0,
        .periodFrames = 0,
        .device = {0, 0},
        .inputDevice = {0, 0},
        .voice = maud_voiceNone,
        .share = maud_shareShared,
        .role = maud_roleGeneral,
        .callback = nullptr,
        .user = nullptr,
    };
}

static bool DefValid(const maudStreamDef* def)
{
    if (def->cookie != STREAM_DEF_COOKIE || def->callback == nullptr ||
        def->direction > maud_directionDuplex || def->mode > maud_modePull ||
        def->ratePolicy > maud_ratePlatformConverted || def->role > maud_roleCommunications ||
        maudGetLayoutChannelCount(def->layout) == 0)
    {
        return false;
    }
    const maudVoiceProcessing parts =
        maud_voiceEchoCancellation | maud_voiceNoiseSuppression | maud_voiceGainControl;
    if ((def->voice & ~parts) != 0 ||
        (def->voice != maud_voiceNone && def->direction == maud_directionOutput))
    {
        return false;
    }
    if (def->share > maud_shareExclusive ||
        (def->share == maud_shareExclusive && def->device.index1 == 0) ||
        (def->contentSpatialized && def->direction == maud_directionInput))
    {
        return false;
    }
    if (def->objectCount > MAUD_MAX_STREAM_OBJECTS ||
        (def->objectCount > 0 &&
         (def->direction != maud_directionOutput || def->share != maud_shareShared)))
    {
        return false;
    }
    if (def->ratePolicy == maud_rateNative)
    {
        return def->sampleRate == 0;
    }
    return def->sampleRate >= MIN_RATE && def->sampleRate <= MAX_RATE;
}

maudResult maudCreateStream(maudContext* context, const maudStreamDef* def,
                            maudStreamId* streamIdOut)
{
    if (streamIdOut != nullptr)
    {
        *streamIdOut = (maudStreamId){0, 0};
    }
    if (context == nullptr)
    {
        return maud_errorInvalid;
    }
    if (maudIsRenderingThread(context))
    {
        maudCountMisuse(context);
        return maud_errorState;
    }
    if (def == nullptr || streamIdOut == nullptr || !DefValid(def))
    {
        maudCountMisuse(context);
        return maud_errorInvalid;
    }
    if (def->direction == maud_directionDuplex && def->share == maud_shareExclusive)
    {
        return maud_errorUnsupported;
    }
    if (def->direction == maud_directionDuplex)
    {
        maudResult result = maudCreateDuplex(context, def, streamIdOut);
        if (result == maud_errorInvalid)
        {
            maudCountMisuse(context);
        }
        return result;
    }
    maudStreamSlot* slot = nullptr;
    maudResult result = maudOpenStream(context, def, 0, &slot);
    if (result == maud_errorInvalid)
    {
        maudCountMisuse(context);
    }
    if (result != maud_success)
    {
        return result;
    }
    *streamIdOut = maudStreamIdOf(context, slot);
    return maud_success;
}

// Finds the stream a control call names, refusing calls from a thread
// that renders one of the context's streams.
static maudResult FindForControl(maudContext* context, maudStreamId stream,
                                 maudStreamSlot** slotOut)
{
    if (context == nullptr)
    {
        return maud_errorInvalid;
    }
    if (maudIsRenderingThread(context))
    {
        maudCountMisuse(context);
        return maud_errorState;
    }
    *slotOut = maudFindStream(context, stream);
    return *slotOut != nullptr ? maud_success : maud_errorStale;
}

maudResult maudDestroyStream(maudContext* context, maudStreamId stream)
{
    maudStreamSlot* slot = nullptr;
    maudResult result = FindForControl(context, stream, &slot);
    if (result != maud_success)
    {
        return result;
    }
    // Where the host renders, another of its threads may be inside the
    // stream; a platform's own thread is waited for by the backend.
    if (context->backend->rendersOnCaller &&
        atomic_load_explicit(&slot->core.renderingThread, memory_order_acquire) != 0)
    {
        maudCountMisuse(context);
        return maud_errorState;
    }
    maudReleaseStream(context, slot);
    return maud_success;
}

static maudResult SetStarted(maudContext* context, maudStreamId stream, bool started)
{
    maudStreamSlot* slot = nullptr;
    maudResult result = FindForControl(context, stream, &slot);
    if (result != maud_success)
    {
        return result;
    }
    maudSetStreamStarted(context, slot, started);
    if (slot->duplex != nullptr)
    {
        maudSetStreamStarted(context, slot->duplex->input, started);
    }
    return maud_success;
}

maudResult maudStartStream(maudContext* context, maudStreamId stream)
{
    return SetStarted(context, stream, true);
}

maudResult maudStopStream(maudContext* context, maudStreamId stream)
{
    return SetStarted(context, stream, false);
}

// Whether a duplex stream's halves run on one clock, as the backend
// says for their current devices.
static bool SharesClock(const maudContext* context, const maudDuplex* duplex)
{
    return context->backend->sharesClock != nullptr &&
           context->backend->sharesClock(context, duplex->output, duplex->input);
}

maudResult maudGetStreamStatus(const maudContext* context, maudStreamId stream,
                               maudStreamStatus* statusOut)
{
    if (context == nullptr || statusOut == nullptr)
    {
        return maud_errorInvalid;
    }
    const maudStreamSlot* slot = maudFindStream(context, stream);
    if (slot == nullptr)
    {
        return maud_errorStale;
    }
    const maudStreamBinding* binding = &slot->core.binding;
    const maudDuplex* duplex = slot->duplex;
    // A duplex stream's voice processing is its input half's.
    const maudStreamCore* captured = duplex != nullptr ? &duplex->input->core : &slot->core;
    *statusOut = (maudStreamStatus){
        .started = binding->started,
        .suspension = binding->suspension,
        .device = binding->current,
        .drift =
            duplex == nullptr || SharesClock(context, duplex) ? maud_driftNone : maud_driftSlip,
        .slippedFrames =
            duplex != nullptr ? atomic_load_explicit(&duplex->slipped, memory_order_relaxed) : 0,
        .voiceReported = atomic_load_explicit(&captured->voiceReported, memory_order_acquire),
        .voiceActive = atomic_load_explicit(&captured->voiceActive, memory_order_relaxed),
        .underruns = atomic_load_explicit(&slot->core.underruns, memory_order_relaxed),
        .overruns = atomic_load_explicit(&captured->overruns, memory_order_relaxed),
        .exclusive = slot->core.exclusive,
        .spatialMark = atomic_load_explicit(&slot->core.spatialMark, memory_order_acquire),
    };
    return maud_success;
}

maudResult maudGetStreamFormat(const maudContext* context, maudStreamId stream,
                               maudStreamFormat* formatOut)
{
    if (context == nullptr || formatOut == nullptr)
    {
        return maud_errorInvalid;
    }
    const maudStreamSlot* slot = maudFindStream(context, stream);
    if (slot == nullptr)
    {
        return maud_errorStale;
    }
    *formatOut = slot->core.format;
    return maud_success;
}

maudResult maudGetStreamPosition(const maudContext* context, maudStreamId stream,
                                 uint64_t* framesOut)
{
    if (context == nullptr || framesOut == nullptr)
    {
        return maud_errorInvalid;
    }
    const maudStreamSlot* slot = maudFindStream(context, stream);
    if (slot == nullptr)
    {
        return maud_errorStale;
    }
    *framesOut = atomic_load_explicit(&slot->core.position, memory_order_acquire);
    return maud_success;
}

maudResult maudGetStreamClock(const maudContext* context, maudStreamId stream,
                              maudStreamClock* clockOut)
{
    if (context == nullptr || clockOut == nullptr)
    {
        return maud_errorInvalid;
    }
    const maudStreamSlot* slot = maudFindStream(context, stream);
    if (slot == nullptr)
    {
        return maud_errorStale;
    }
    maudStreamClock clock = {0};
    maudReadClock(&slot->core, &clock.position, &clock.hostNanoseconds, &clock.latencyNanoseconds);
    *clockOut = clock;
    return maud_success;
}

int64_t maudGetHostNanoseconds(void)
{
    return maudNowNanoseconds();
}

// Checks a render or feed call and claims the stream for the calling
// thread. On success the caller must release it with EndRender.
// Takes a running offline stream of the direction and object count for
// rendering on the calling thread.
static maudResult BeginRender(maudContext* context, maudStreamId stream, maudDirection direction,
                              uint32_t objects, bool haveFrames, uint32_t frameCount,
                              maudStreamCore** coreOut)
{
    if (context == nullptr)
    {
        return maud_errorInvalid;
    }
    maudStreamSlot* slot = maudFindStream(context, stream);
    if (slot == nullptr)
    {
        return maud_errorStale;
    }
    // A duplex stream is fed through its input half.
    if (slot->duplex != nullptr && direction == maud_directionInput)
    {
        slot = slot->duplex->input;
    }
    maudStreamCore* core = &slot->core;
    size_t samples;
    if (core->def.direction != direction || core->def.objectCount != objects ||
        (frameCount != 0 && !haveFrames) ||
        ckd_mul(&samples, (size_t)frameCount, (size_t)core->period.channelCount) ||
        ckd_mul(&samples, samples, sizeof(float)))
    {
        maudCountMisuse(context);
        return maud_errorInvalid;
    }
    if (!context->backend->rendersOnCaller)
    {
        return maud_errorUnsupported;
    }
    if (atomic_load_explicit(&core->state, memory_order_acquire) != maud_streamRunning)
    {
        return maud_errorState;
    }
    uintptr_t idle = 0;
    if (!atomic_compare_exchange_strong_explicit(&core->renderingThread, &idle, maudCurrentThread(),
                                                 memory_order_acq_rel, memory_order_acquire))
    {
        return maud_errorState;
    }
    core->period.sampleRate = atomic_load_explicit(&core->blockRate, memory_order_acquire);
    *coreOut = core;
    return maud_success;
}

static void EndRender(maudStreamCore* core, uint32_t frameCount)
{
    atomic_fetch_add_explicit(&core->position, frameCount, memory_order_release);
    atomic_store_explicit(&core->renderingThread, 0, memory_order_release);
}

maudResult maudRenderStream(maudContext* context, maudStreamId stream, float* framesOut,
                            uint32_t frameCount)
{
    maudStreamCore* core = nullptr;
    maudResult result = BeginRender(context, stream, maud_directionOutput, 0, framesOut != nullptr,
                                    frameCount, &core);
    if (result != maud_success)
    {
        return result;
    }
    maudPullPeriod(&core->period, framesOut, frameCount);
    EndRender(core, frameCount);
    return maud_success;
}

maudResult maudFeedStream(maudContext* context, maudStreamId stream, const float* frames,
                          uint32_t frameCount)
{
    maudStreamCore* core = nullptr;
    maudResult result =
        BeginRender(context, stream, maud_directionInput, 0, frames != nullptr, frameCount, &core);
    if (result != maud_success)
    {
        return result;
    }
    maudPushPeriod(&core->period, frames, frameCount);
    EndRender(core, frameCount);
    return maud_success;
}

maudResult maudRenderObjects(maudContext* context, maudStreamId stream, float* bedOut,
                             maudStreamObject* objectsOut, uint32_t objectCount,
                             uint32_t frameCount)
{
    // The objects' records are written whatever the frame count.
    if (context != nullptr && objectsOut == nullptr)
    {
        maudCountMisuse(context);
        return maud_errorInvalid;
    }
    bool haveFrames = bedOut != nullptr;
    for (uint32_t i = 0; haveFrames && i < objectCount; ++i)
    {
        haveFrames = objectsOut[i].samples != nullptr;
    }
    // 0 objects names no object stream.
    maudStreamCore* core = nullptr;
    maudResult result =
        BeginRender(context, stream, maud_directionOutput,
                    objectCount == 0 ? UINT32_MAX : objectCount, haveFrames, frameCount, &core);
    if (result != maud_success)
    {
        return result;
    }
    core->period.objectsAvailable = objectCount;
    maudPullObjects(&core->period, bedOut, objectsOut, frameCount);
    EndRender(core, frameCount);
    return maud_success;
}
