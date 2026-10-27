// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Metal device's lanes (metal_lanes.m): the graphics, async compute and
// transfer command queues a frame's passes run on, an event each, and a
// frame's batches of work on them. A pass runs on its class's queue,
// except that a pass touching a surface image and a native pass run on
// the graphics queue, where the frame presents. A pass follows the passes
// its waitFor names and the pass of its class before it, which keeps a
// class in order across lanes. A lane's passes go in the frame's order
// into batches, a new one where a pass follows another lane's pass and
// after a pass another lane's follows. A batch is one command buffer: it
// waits first for the batch before it on its lane, for the other lanes'
// batches its first pass follows and, as its lane's first in the frame,
// for every lane's work before the frame; and it signals its lane's
// event last. Metal signals an event once the command buffer's earlier
// commands finish, not the queue's, so the wait for the lane's batch
// before is what makes a signal stand for the lane's work up to it.
// A native pass's command buffer is the program's, which takes no wait
// at its start: a command buffer of the library's that only waits runs
// before it on the graphics queue, which blocks there until the waits
// are met, and the native pass's command buffer signals the native
// event last. Included by the driver's Objective-C files only.

#ifndef MAUL_RHI_SRC_METAL_LANES_H
#define MAUL_RHI_SRC_METAL_LANES_H

#include "driver.h"

#import <Metal/Metal.h>
#include <dispatch/dispatch.h>
#include <stdatomic.h>

typedef enum mrhiMetalLane
{
    mrhiMetalLaneGraphics,
    mrhiMetalLaneCompute,
    mrhiMetalLaneTransfer,
    mrhiMetalLaneCount,
} mrhiMetalLane;

// The events batches wait for and signal: each lane's, then the one
// native passes' command buffers signal.
#define MRHI_METAL_NATIVE mrhiMetalLaneCount
#define MRHI_METAL_EVENTS (mrhiMetalLaneCount + 1)

// The command buffers a frame committed, retained, in the order they
// were committed, the native passes' among them. The frame is done when
// all have completed; the last completion handler to run signals done.
typedef struct mrhiMetalCommits
{
    id<MTLCommandBuffer>* buffers;
    uint32_t count;
    _Atomic uint32_t running;
    dispatch_semaphore_t done;
} mrhiMetalCommits;

// A batch of a frame's work: the event it signals (its lane's, or the
// native event for a native pass's command buffer), the values of each
// event it waits for first (0 for none), and the value it signals.
typedef struct mrhiMetalBatch
{
    uint32_t event;
    uint64_t waits[MRHI_METAL_EVENTS];
    uint64_t signal;
} mrhiMetalBatch;

typedef struct mrhiMetalLanes
{
    // The queues by lane, the graphics one the device's own; the events,
    // and the value each last took.
    id<MTLCommandQueue> queues[mrhiMetalLaneCount];
    id<MTLEvent> events[MRHI_METAL_EVENTS];
    uint64_t values[MRHI_METAL_EVENTS];
    // The command buffers a frame commits at most, and its batches.
    uint32_t commitLimit;
    uint32_t batchLimit;
    mrhiMetalBatch* batches;
    uint32_t batchCount;
    // By kept pass, by its id's index: its lane, its batch's index plus
    // one, and whether a pass on another lane follows it.
    mrhiMetalLane* passLanes;
    uint32_t* passBatches;
    bool* followed;
    // The frame recording: its view and the commits it adds to; each
    // event's value before the frame; each lane's open batch (its index
    // plus one, 0 for none), its command buffer and whether it took a
    // batch; and the last pass of each class so far.
    const mrhiDriverFrame* frame;
    mrhiMetalCommits* commits;
    uint64_t before[MRHI_METAL_EVENTS];
    uint32_t open[mrhiMetalLaneCount];
    id<MTLCommandBuffer> commands[mrhiMetalLaneCount];
    bool started[mrhiMetalLaneCount];
    uint32_t lastOf[MRHI_PASS_CLASSES];
} mrhiMetalLanes;

// Makes the async compute and transfer queues beside the device's, each
// labelled as the device is with its lane's name after, and the events:
// false when Metal makes one not, with those made released.
bool mrhiMetalOpenLanes(mrhiMetalLanes* lanes, id<MTLDevice> device, id<MTLCommandQueue> queue,
                        NSString* label);

// Releases the queues and events the lanes made.
void mrhiMetalCloseLanes(mrhiMetalLanes* lanes);

// Commits a command buffer of a frame, retained, its completion counted.
void mrhiMetalCommit(mrhiMetalCommits* commits, id<MTLCommandBuffer> commands);

// Begins a frame's recording: places each pass on its lane, marks the
// passes another lane follows, and opens the graphics lane's first batch,
// whose command buffer it returns.
id<MTLCommandBuffer> mrhiMetalBeginLanes(mrhiMetalLanes* lanes, const mrhiDriverFrame* frame,
                                         mrhiMetalCommits* commits);

// Before a pass records: returns its lane's command buffer, in a new
// batch where it follows another lane's pass.
id<MTLCommandBuffer> mrhiMetalEnterPass(mrhiMetalLanes* lanes, const mrhiDriverPass* pass);

// After a pass recorded: commits its batch when another lane's pass
// follows it.
void mrhiMetalLeavePass(mrhiMetalLanes* lanes, const mrhiDriverPass* pass);

// Runs a native pass: commits the graphics lane's batch, a command
// buffer that waits for what the pass follows, and the pass's command
// buffer (one of the library's when the program gave none), which
// signals the native event.
void mrhiMetalNativePass(mrhiMetalLanes* lanes, const mrhiDriverPass* pass);

// Ends the passes: for a frame that presents, returns the graphics
// lane's command buffer, in a new batch when it has none open; nil
// otherwise.
id<MTLCommandBuffer> mrhiMetalEndLanes(mrhiMetalLanes* lanes, bool presents);

// Commits every batch still open.
void mrhiMetalFinishLanes(mrhiMetalLanes* lanes);

#endif // MAUL_RHI_SRC_METAL_LANES_H
