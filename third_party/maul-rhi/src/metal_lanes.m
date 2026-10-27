// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Metal device's lanes (metal_lanes.h). A batch is committed when it
// closes, and waits only for batches closed before it opened: every wait
// names a value whose signal is committed before it. Each event is
// signalled in the order of its values, since every batch on a lane, and
// every native pass's command buffer, waits for the one before.

#include "metal_lanes.h"

#include "invariant.h"

#include <string.h>

// Raises a wait to a value.
static void Raise(uint64_t* wait, uint64_t value)
{
    *wait = value > *wait ? value : *wait;
}

bool mrhiMetalOpenLanes(mrhiMetalLanes* lanes, id<MTLDevice> device, id<MTLCommandQueue> queue,
                        NSString* label)
{
    static NSString* const names[] = {
        [mrhiMetalLaneCompute] = @"async compute",
        [mrhiMetalLaneTransfer] = @"transfer",
    };
    lanes->queues[mrhiMetalLaneGraphics] = queue;
    bool made = true;
    @autoreleasepool
    {
        for (int lane = mrhiMetalLaneCompute; lane < mrhiMetalLaneCount; ++lane)
        {
            lanes->queues[lane] = [device newCommandQueue];
            lanes->queues[lane].label =
                label != nil ? [NSString stringWithFormat:@"%@ (%@)", label, names[lane]]
                             : names[lane];
            made = made && lanes->queues[lane] != nil;
        }
        for (int event = 0; event < MRHI_METAL_EVENTS; ++event)
        {
            lanes->events[event] = [device newEvent];
            lanes->values[event] = 0;
            made = made && lanes->events[event] != nil;
        }
    }
    if (!made)
    {
        mrhiMetalCloseLanes(lanes);
    }
    return made;
}

void mrhiMetalCloseLanes(mrhiMetalLanes* lanes)
{
    for (int lane = mrhiMetalLaneCompute; lane < mrhiMetalLaneCount; ++lane)
    {
        [lanes->queues[lane] release];
        lanes->queues[lane] = nil;
    }
    for (int event = 0; event < MRHI_METAL_EVENTS; ++event)
    {
        [lanes->events[event] release];
        lanes->events[event] = nil;
    }
}

void mrhiMetalCommit(mrhiMetalCommits* commits, id<MTLCommandBuffer> commands)
{
    dispatch_semaphore_t done = commits->done;
    _Atomic uint32_t* running = &commits->running;
    atomic_fetch_add_explicit(running, 1, memory_order_relaxed);
    [commands addCompletedHandler:^(id<MTLCommandBuffer> finished) {
      (void)finished;
      if (atomic_fetch_sub_explicit(running, 1, memory_order_acq_rel) == 1)
      {
          dispatch_semaphore_signal(done);
      }
    }];
    commits->buffers[commits->count++] = [commands retain];
    [commands commit];
}

static mrhiMetalLane LaneOfClass(mrhiPassClass passClass)
{
    switch (passClass)
    {
    case mrhi_passAsyncCompute:
        return mrhiMetalLaneCompute;
    case mrhi_passTransfer:
        return mrhiMetalLaneTransfer;
    default:
        return mrhiMetalLaneGraphics;
    }
}

// Places each pass on its class's lane, or on the graphics lane for a
// native pass and a pass touching a surface image: the frame presents
// there, after the lane's work.
static void Place(mrhiMetalLanes* lanes)
{
    const mrhiDriverFrame* frame = lanes->frame;
    for (uint32_t p = 0; p < frame->passCount; ++p)
    {
        const mrhiDriverPass* pass = &frame->passes[p];
        mrhiMetalLane lane = pass->native ? mrhiMetalLaneGraphics : LaneOfClass(pass->passClass);
        for (uint32_t i = 0; i < pass->accessCount; ++i)
        {
            if (frame->resources[pass->accesses[i].resource - 1].kind == mrhiDriverSurfaceImage)
            {
                lane = mrhiMetalLaneGraphics;
            }
        }
        lanes->passLanes[pass->id.index1] = lane;
    }
}

// The pass a pass follows by class c, or, at MRHI_PASS_CLASSES, the pass
// of its own class before it: its id's index, 0 for none.
static uint32_t Followed(const mrhiDriverPass* pass, const uint32_t* lastOf, uint32_t c)
{
    return c < MRHI_PASS_CLASSES ? pass->waitFor[c].index1 : lastOf[pass->passClass];
}

// Encodes a command buffer's waits.
static void Wait(const mrhiMetalLanes* lanes, id<MTLCommandBuffer> commands, const uint64_t* waits)
{
    for (int event = 0; event < MRHI_METAL_EVENTS; ++event)
    {
        if (waits[event] != 0)
        {
            [commands encodeWaitForEvent:lanes->events[event] value:waits[event]];
        }
    }
}

// Opens a batch on a lane: a command buffer that waits for the given
// values, for the lane's batch before (on the graphics lane, for the
// native pass that may have run after it too) and, as the lane's first in
// the frame, for every event's value before the frame.
static void Open(mrhiMetalLanes* lanes, mrhiMetalLane lane, const uint64_t* waits)
{
    // A pass opens one batch at most, beside the frame's first and last.
    MRHI_ASSERT(lanes->batchCount < lanes->batchLimit);
    mrhiMetalBatch* batch = &lanes->batches[lanes->batchCount++];
    *batch = (mrhiMetalBatch){.event = lane};
    for (int event = 0; event < MRHI_METAL_EVENTS; ++event)
    {
        batch->waits[event] = waits[event];
        if (!lanes->started[lane])
        {
            Raise(&batch->waits[event], lanes->before[event]);
        }
    }
    Raise(&batch->waits[lane], lanes->values[lane]);
    if (lane == mrhiMetalLaneGraphics)
    {
        Raise(&batch->waits[MRHI_METAL_NATIVE], lanes->values[MRHI_METAL_NATIVE]);
    }
    lanes->started[lane] = true;
    lanes->open[lane] = lanes->batchCount;
    lanes->commands[lane] = [lanes->queues[lane] commandBuffer];
    Wait(lanes, lanes->commands[lane], batch->waits);
}

// Closes a lane's open batch, if any: it signals the lane's next value
// and is committed.
static void Close(mrhiMetalLanes* lanes, mrhiMetalLane lane)
{
    if (lanes->open[lane] == 0)
    {
        return;
    }
    mrhiMetalBatch* batch = &lanes->batches[lanes->open[lane] - 1];
    lanes->open[lane] = 0;
    batch->signal = ++lanes->values[lane];
    [lanes->commands[lane] encodeSignalEvent:lanes->events[lane] value:batch->signal];
    MRHI_ASSERT(lanes->commits->count < lanes->commitLimit);
    mrhiMetalCommit(lanes->commits, lanes->commands[lane]);
    lanes->commands[lane] = nil;
}

id<MTLCommandBuffer> mrhiMetalBeginLanes(mrhiMetalLanes* lanes, const mrhiDriverFrame* frame,
                                         mrhiMetalCommits* commits)
{
    lanes->frame = frame;
    lanes->commits = commits;
    lanes->batchCount = 0;
    for (int event = 0; event < MRHI_METAL_EVENTS; ++event)
    {
        lanes->before[event] = lanes->values[event];
    }
    for (int lane = 0; lane < mrhiMetalLaneCount; ++lane)
    {
        lanes->open[lane] = 0;
        lanes->started[lane] = false;
    }
    for (uint32_t p = 0; p < frame->passCount; ++p)
    {
        lanes->passBatches[frame->passes[p].id.index1] = 0;
        lanes->followed[frame->passes[p].id.index1] = false;
    }
    Place(lanes);
    // Each pass follows those its waitFor names and the last of its class.
    uint32_t lastOf[MRHI_PASS_CLASSES] = {0};
    for (uint32_t p = 0; p < frame->passCount; ++p)
    {
        const mrhiDriverPass* pass = &frame->passes[p];
        mrhiMetalLane lane = lanes->passLanes[pass->id.index1];
        for (uint32_t c = 0; c <= MRHI_PASS_CLASSES; ++c)
        {
            uint32_t followed = Followed(pass, lastOf, c);
            lanes->followed[followed] =
                lanes->followed[followed] || (followed != 0 && lanes->passLanes[followed] != lane);
        }
        lastOf[pass->passClass] = pass->id.index1;
    }
    for (uint32_t c = 0; c < MRHI_PASS_CLASSES; ++c)
    {
        lanes->lastOf[c] = 0;
    }
    const uint64_t none[MRHI_METAL_EVENTS] = {0};
    Open(lanes, mrhiMetalLaneGraphics, none);
    return lanes->commands[mrhiMetalLaneGraphics];
}

// Raises waits to the batches on other lanes that a pass on a lane
// follows, and notes it as the last of its class: whether there is one.
static bool Follow(mrhiMetalLanes* lanes, const mrhiDriverPass* pass, mrhiMetalLane lane,
                   uint64_t* waits)
{
    bool follows = false;
    for (uint32_t c = 0; c <= MRHI_PASS_CLASSES; ++c)
    {
        uint32_t followed = Followed(pass, lanes->lastOf, c);
        if (followed == 0 || lanes->passLanes[followed] == lane)
        {
            continue;
        }
        // Each pass another lane follows closes its batch, and a native
        // pass's command buffer signals as it is committed.
        const mrhiMetalBatch* batch = &lanes->batches[lanes->passBatches[followed] - 1];
        MRHI_ASSERT(batch->signal != 0);
        Raise(&waits[batch->event], batch->signal);
        follows = true;
    }
    lanes->lastOf[pass->passClass] = pass->id.index1;
    return follows;
}

id<MTLCommandBuffer> mrhiMetalEnterPass(mrhiMetalLanes* lanes, const mrhiDriverPass* pass)
{
    mrhiMetalLane lane = lanes->passLanes[pass->id.index1];
    uint64_t waits[MRHI_METAL_EVENTS] = {0};
    if (Follow(lanes, pass, lane, waits))
    {
        Close(lanes, lane);
    }
    if (lanes->open[lane] == 0)
    {
        Open(lanes, lane, waits);
    }
    lanes->passBatches[pass->id.index1] = lanes->open[lane];
    return lanes->commands[lane];
}

void mrhiMetalLeavePass(mrhiMetalLanes* lanes, const mrhiDriverPass* pass)
{
    if (lanes->followed[pass->id.index1])
    {
        Close(lanes, lanes->passLanes[pass->id.index1]);
    }
}

void mrhiMetalNativePass(mrhiMetalLanes* lanes, const mrhiDriverPass* pass)
{
    uint64_t waits[MRHI_METAL_EVENTS] = {0};
    (void)Follow(lanes, pass, mrhiMetalLaneGraphics, waits);
    Close(lanes, mrhiMetalLaneGraphics);
    Raise(&waits[mrhiMetalLaneGraphics], lanes->values[mrhiMetalLaneGraphics]);
    Raise(&waits[MRHI_METAL_NATIVE], lanes->values[MRHI_METAL_NATIVE]);
    id<MTLCommandQueue> queue = lanes->queues[mrhiMetalLaneGraphics];
    // The program's command buffer takes no wait at its start, so one of
    // the library's that only waits goes before it on the queue.
    id<MTLCommandBuffer> gate = [queue commandBuffer];
    Wait(lanes, gate, waits);
    MRHI_ASSERT(lanes->commits->count + 2 <= lanes->commitLimit);
    mrhiMetalCommit(lanes->commits, gate);
    id<MTLCommandBuffer> commands = pass->nativeCommands != nullptr
                                        ? (id<MTLCommandBuffer>)pass->nativeCommands
                                        : [queue commandBuffer];
    MRHI_ASSERT(lanes->batchCount < lanes->batchLimit);
    mrhiMetalBatch* batch = &lanes->batches[lanes->batchCount++];
    *batch = (mrhiMetalBatch){.event = MRHI_METAL_NATIVE};
    memcpy(batch->waits, waits, sizeof(batch->waits));
    batch->signal = ++lanes->values[MRHI_METAL_NATIVE];
    [commands encodeSignalEvent:lanes->events[MRHI_METAL_NATIVE] value:batch->signal];
    mrhiMetalCommit(lanes->commits, commands);
    lanes->passBatches[pass->id.index1] = lanes->batchCount;
}

id<MTLCommandBuffer> mrhiMetalEndLanes(mrhiMetalLanes* lanes, bool presents)
{
    if (!presents)
    {
        return nil;
    }
    if (lanes->open[mrhiMetalLaneGraphics] == 0)
    {
        const uint64_t none[MRHI_METAL_EVENTS] = {0};
        Open(lanes, mrhiMetalLaneGraphics, none);
    }
    return lanes->commands[mrhiMetalLaneGraphics];
}

// Whether a frame's batches order every pass after the passes it
// follows, worked out again from the frame view: those its waitFor names
// and the pass of its class before it, in the same batch or through a
// wait of its batch (a native pass's, the command buffer's before it) on
// the event the earlier batch signals, its lane's batch before included.
// Each lane's first batch waits for every event's value before the
// frame. Only an assertion calls it.
[[maybe_unused]] static bool HoldsFollows(const mrhiMetalLanes* lanes)
{
    const mrhiDriverFrame* frame = lanes->frame;
    uint32_t lastOf[MRHI_PASS_CLASSES] = {0};
    for (uint32_t p = 0; p < frame->passCount; ++p)
    {
        const mrhiDriverPass* pass = &frame->passes[p];
        uint32_t held = lanes->passBatches[pass->id.index1];
        if (held == 0)
        {
            return false;
        }
        const mrhiMetalBatch* batch = &lanes->batches[held - 1];
        for (uint32_t c = 0; c <= MRHI_PASS_CLASSES; ++c)
        {
            uint32_t followed = Followed(pass, lastOf, c);
            uint32_t earlier = followed == 0 ? held : lanes->passBatches[followed];
            if (earlier == 0 || earlier > held)
            {
                return false;
            }
            const mrhiMetalBatch* before = &lanes->batches[earlier - 1];
            if (earlier != held && batch->waits[before->event] < before->signal)
            {
                return false;
            }
        }
        lastOf[pass->passClass] = pass->id.index1;
    }
    bool seen[mrhiMetalLaneCount] = {0};
    for (uint32_t b = 0; b < lanes->batchCount; ++b)
    {
        const mrhiMetalBatch* batch = &lanes->batches[b];
        if (batch->event == MRHI_METAL_NATIVE || seen[batch->event])
        {
            continue;
        }
        seen[batch->event] = true;
        for (int event = 0; event < MRHI_METAL_EVENTS; ++event)
        {
            if (batch->waits[event] < lanes->before[event])
            {
                return false;
            }
        }
    }
    return true;
}

void mrhiMetalFinishLanes(mrhiMetalLanes* lanes)
{
    for (int lane = 0; lane < mrhiMetalLaneCount; ++lane)
    {
        Close(lanes, (mrhiMetalLane)lane);
    }
    MRHI_ASSERT(HoldsFollows(lanes));
}
