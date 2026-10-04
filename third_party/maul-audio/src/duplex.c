// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Duplex streams. The input half's callback writes what it captures
// into a ring; the output half's callback, once per period, takes a
// period of input from the ring and calls the host with both. The two
// halves may run on two clocks, so the ring is held near two periods:
// short, the missing input is silence; past four periods, the oldest
// frames beyond two are dropped. Both count as slipped.

#include "duplex.h"

#include "context.h"
#include "stream_open.h"

#include "maul-audio/layout.h"

#include <string.h>

// Ring periods: kept, and the most before dropping.
#define KEEP_PERIODS 2u
#define HIGH_PERIODS 4u

// Runs on the input half's thread: queues what it captured, dropping
// what does not fit.
static void Capture(const maudStreamBlock* block, void* user)
{
    maudDuplex* duplex = user;
    uint32_t written = atomic_load_explicit(&duplex->written, memory_order_relaxed);
    uint32_t read = atomic_load_explicit(&duplex->read, memory_order_acquire);
    uint32_t room = duplex->capacity - (written - read);
    uint32_t frames = block->frameCount < room ? block->frameCount : room;
    for (uint32_t i = 0; i < frames; ++i)
    {
        uint32_t at = (written + i) & (duplex->capacity - 1);
        memcpy(duplex->ring + (size_t)at * duplex->channels,
               block->input + (size_t)i * duplex->channels, duplex->channels * sizeof(float));
    }
    atomic_store_explicit(&duplex->written, written + frames, memory_order_release);
    atomic_store_explicit(&duplex->primed, true, memory_order_release);
    if (frames < block->frameCount)
    {
        atomic_fetch_add_explicit(&duplex->slipped, block->frameCount - frames,
                                  memory_order_relaxed);
    }
}

// Runs on the output half's thread: takes a period of input, slipping
// as the policy says, and calls the host with both.
static void Play(const maudStreamBlock* block, void* user)
{
    maudDuplex* duplex = user;
    uint32_t read = atomic_load_explicit(&duplex->read, memory_order_relaxed);
    uint32_t waiting = atomic_load_explicit(&duplex->written, memory_order_acquire) - read;
    uint64_t slipped = 0;
    if (waiting > HIGH_PERIODS * duplex->periodFrames)
    {
        uint32_t dropped = waiting - KEEP_PERIODS * duplex->periodFrames;
        read += dropped;
        waiting -= dropped;
        slipped += dropped;
    }
    uint32_t frames = block->frameCount < waiting ? block->frameCount : waiting;
    for (uint32_t i = 0; i < frames; ++i)
    {
        uint32_t at = (read + i) & (duplex->capacity - 1);
        memcpy(duplex->block + (size_t)i * duplex->channels,
               duplex->ring + (size_t)at * duplex->channels, duplex->channels * sizeof(float));
    }
    memset(duplex->block + (size_t)frames * duplex->channels, 0,
           (size_t)(block->frameCount - frames) * duplex->channels * sizeof(float));
    atomic_store_explicit(&duplex->read, read + frames, memory_order_release);
    if (atomic_load_explicit(&duplex->primed, memory_order_acquire))
    {
        slipped += block->frameCount - frames;
    }
    if (slipped != 0)
    {
        atomic_fetch_add_explicit(&duplex->slipped, slipped, memory_order_relaxed);
    }
    maudStreamBlock both = *block;
    both.input = duplex->block;
    duplex->callback(&both, duplex->user);
}

// The joint's block: the struct, the input block for a period, and the
// ring, eight periods rounded up to a power of two.
static maudDuplex* AllocateJoint(maudContext* context, const maudStreamDef* def,
                                 uint32_t periodFrames)
{
    uint32_t channels = maudGetLayoutChannelCount(def->layout);
    uint32_t capacity = 1;
    while (capacity < 8u * periodFrames)
    {
        capacity <<= 1;
    }
    size_t bytes =
        sizeof(maudDuplex) + ((size_t)periodFrames + capacity) * channels * sizeof(float);
    maudDuplex* duplex = maudContextAllocate(context, bytes, alignof(maudDuplex));
    if (duplex == nullptr)
    {
        return nullptr;
    }
    *duplex = (maudDuplex){
        .callback = def->callback,
        .user = def->user,
        .capacity = capacity,
        .channels = channels,
        .periodFrames = periodFrames,
        .block = (float*)(duplex + 1),
        .bytes = bytes,
    };
    duplex->ring = duplex->block + (size_t)periodFrames * channels;
    atomic_init(&duplex->written, 0);
    atomic_init(&duplex->read, 0);
    atomic_init(&duplex->primed, false);
    atomic_init(&duplex->slipped, 0);
    return duplex;
}

// Opens the input half at the output's rate and period: required, or
// converted by the platform where the device runs at another rate.
static maudResult OpenInput(maudContext* context, const maudStreamDef* def,
                            const maudStreamSlot* output, maudDuplex* duplex,
                            maudStreamSlot** slotOut)
{
    maudStreamDef input = *def;
    input.direction = maud_directionInput;
    input.device = def->inputDevice;
    input.ratePolicy = maud_rateRequired;
    input.sampleRate = output->core.format.sampleRate;
    input.periodFrames = output->core.format.periodFrames;
    input.callback = Capture;
    input.user = duplex;
    uint32_t group = output->core.duplexGroup;
    maudResult result = maudOpenStream(context, &input, group, slotOut);
    if (result == maud_errorUnsupported)
    {
        input.ratePolicy = maud_ratePlatformConverted;
        result = maudOpenStream(context, &input, group, slotOut);
    }
    return result;
}

maudResult maudCreateDuplex(maudContext* context, const maudStreamDef* def,
                            maudStreamId* streamIdOut)
{
    // The output opens first, without its joint: nothing calls back
    // before the host starts the stream, and the ring's size follows the
    // output's period, known only once it is open.
    maudStreamDef output = *def;
    output.direction = maud_directionOutput;
    output.callback = Play;
    output.user = nullptr;
    // The pair's number lets the backend put both halves on one clock.
    uint32_t group = ++context->streams.duplexGroups;
    group = group != 0 ? group : ++context->streams.duplexGroups;
    maudStreamSlot* played = nullptr;
    maudResult result = maudOpenStream(context, &output, group, &played);
    if (result != maud_success)
    {
        return result;
    }
    maudDuplex* duplex = AllocateJoint(context, def, played->core.format.periodFrames);
    maudStreamSlot* captured = nullptr;
    result =
        duplex == nullptr ? maud_errorCapacity : OpenInput(context, def, played, duplex, &captured);
    if (result != maud_success)
    {
        if (duplex != nullptr)
        {
            maudContextRelease(context, duplex, duplex->bytes, alignof(maudDuplex));
        }
        maudReleaseStream(context, played);
        return result;
    }
    played->core.def.user = duplex;
    played->core.period.user = duplex;
    duplex->output = played;
    duplex->input = captured;
    played->duplex = duplex;
    captured->duplex = duplex;
    captured->hidden = true;
    *streamIdOut = maudStreamIdOf(context, played);
    return maud_success;
}
