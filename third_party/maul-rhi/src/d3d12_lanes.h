// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A D3D12 device's lanes (d3d12_lanes.c): the direct, compute and copy
// queues a frame's passes run on, with a fence each, and a frame's
// batches of work on them. A pass runs on its class's queue where that
// queue's lists can record it, else on the next one up: the copy queue
// takes a transfer pass that touches no texture and has no aliasing
// barrier, and the compute queue an async compute or transfer pass whose
// textures are used and moved only in the states a compute list takes;
// the rest, native passes included, run on the direct queue. A pass
// follows the passes its waitFor names and the pass of its class before
// it, which keeps a class in order across lanes. A lane's passes go in
// the frame's order into batches, a new one where a pass follows another
// lane's pass and after a pass another lane's follows; each batch waits
// for the lanes' batches its first pass follows, and the compute and
// copy lanes' first batch waits for the frame before. With work beyond
// the direct lane, the frame's end barriers go in a last batch on the
// direct lane after every lane's last; without, a frame is one batch.
// Included by the driver's files only.

#ifndef MAUL_RHI_SRC_D3D12_LANES_H
#define MAUL_RHI_SRC_D3D12_LANES_H

#include "d3d12_state.h"

// A batch of a frame's work on one lane: the lists it runs, each in its
// own ExecuteCommandLists, as a range of its slot's runs; the other
// lanes' fence values it waits for first (0 for none), and the frame
// fence's; and the value of its lane's fence it signals after (0 for
// none).
typedef struct mrhiD3d12Batch
{
    mrhiD3d12Lane lane;
    uint32_t firstRun;
    uint32_t runCount;
    uint64_t waits[mrhiD3d12LaneCount];
    uint64_t frameWait;
    uint64_t signal;
} mrhiD3d12Batch;

// A frame slot's lanes: each lane's allocator and the lists the frame
// records into on it, one after another, a batch's first, then after
// each native pass the next; the lists the frame runs, each batch's
// together, its parts with the native passes' between; and the frame's
// batches, in the order they run.
typedef struct mrhiD3d12LaneSlot
{
    ID3D12CommandAllocator* allocators[mrhiD3d12LaneCount];
    ID3D12GraphicsCommandList** lists[mrhiD3d12LaneCount];
    uint32_t listsUsed[mrhiD3d12LaneCount];
    ID3D12CommandList** runs;
    uint32_t runCount;
    mrhiD3d12Batch* batches;
    uint32_t batchCount;
} mrhiD3d12LaneSlot;

// The lists one batch runs at most: its parts with the native passes'
// between.
#define MRHI_D3D12_BATCH_RUNS (1 + 2 * MRHI_NATIVE_PASSES)

typedef struct mrhiD3d12Lanes
{
    ID3D12Device* device;
    // The queues by lane, the direct one the device's own, each lane's
    // fence, and the value it last took.
    ID3D12CommandQueue* queues[mrhiD3d12LaneCount];
    ID3D12Fence* fences[mrhiD3d12LaneCount];
    uint64_t values[mrhiD3d12LaneCount];
    // Whether the compute queue's timestamps tick as the direct queue's,
    // whose frequency is the device's timestamp period.
    bool computeTimes;
    // The lists a slot records on each lane at most, its runs and its
    // batches.
    uint32_t listLimit[mrhiD3d12LaneCount];
    uint32_t runLimit;
    uint32_t batchLimit;
    // By kept pass, by its id's index: its lane, its batch's index plus
    // one, and whether a pass on another lane follows it.
    mrhiD3d12Lane* passLanes;
    uint32_t* passBatches;
    bool* followed;
    // The frame recording: its recorder, slot and shader-visible heaps;
    // the frame fence's value the compute and copy lanes' first batch
    // waits for, 0 for none; whether a pass runs beyond the direct lane;
    // each lane's open batch (its index plus one, 0 for none), the list
    // recording on it, the lists its open batch runs so far, the value
    // its last batch signals and whether it took a batch; the last pass
    // of each class so far; and the lane of the pass recording.
    mrhiD3d12Recorder* recorder;
    mrhiD3d12LaneSlot* slot;
    ID3D12DescriptorHeap* heaps[2];
    uint64_t frameWait;
    bool several;
    uint32_t open[mrhiD3d12LaneCount];
    ID3D12GraphicsCommandList* lists[mrhiD3d12LaneCount];
    ID3D12CommandList* laneRuns[mrhiD3d12LaneCount][MRHI_D3D12_BATCH_RUNS];
    uint32_t laneRunCount[mrhiD3d12LaneCount];
    uint64_t last[mrhiD3d12LaneCount];
    bool started[mrhiD3d12LaneCount];
    uint32_t lastOf[MRHI_PASS_CLASSES];
    mrhiD3d12Lane current;
} mrhiD3d12Lanes;

// The kind of list a lane records, and its queue runs.
D3D12_COMMAND_LIST_TYPE mrhiD3d12ListType(mrhiD3d12Lane lane);

// Begins a frame's recording in a slot whose shader-visible heaps are
// views and samplers: places each pass on its lane and marks the passes
// another lane follows. The recorder holds the frame and its objects.
void mrhiD3d12BeginLanes(mrhiD3d12Lanes* lanes, mrhiD3d12LaneSlot* slot,
                         ID3D12DescriptorHeap* views, ID3D12DescriptorHeap* samplers,
                         uint64_t frameWait);

// Before a pass records: makes its lane's list the recorder's, in a new
// batch where it follows another lane's pass. A list D3D12 makes not or
// fails to reset fails the frame.
void mrhiD3d12EnterPass(mrhiD3d12Lanes* lanes, const mrhiDriverPass* pass);

// After a pass recorded: ends its batch when another lane's pass follows
// it.
void mrhiD3d12LeavePass(mrhiD3d12Lanes* lanes, const mrhiDriverPass* pass);

// Ends the passes' batches and makes the frame's last batch the
// recorder's, for its end barriers: with work beyond the direct lane, a
// new batch on it after every lane's last.
void mrhiD3d12EndLanes(mrhiD3d12Lanes* lanes);

// Ends every batch still open, the frame's last among them, after a
// failure too, since a list is closed even when it is not run so that it
// resets: false when D3D12 fails to close one.
bool mrhiD3d12CloseLanes(mrhiD3d12Lanes* lanes);

// Places a native pass's list (NULL for none) in the direct lane's batch
// after the part recorded so far, and begins the batch's next part.
void mrhiD3d12NextPart(mrhiD3d12Lanes* lanes, void* native);

// Runs a recorded frame's batches on their lanes' queues, in order, each
// after its waits, on other lanes' fences and on the frame fence, and
// before its signal: success, or mrhi_errorDeviceLost.
mrhiResult mrhiD3d12RunLanes(const mrhiD3d12Lanes* lanes, const mrhiD3d12LaneSlot* slot,
                             ID3D12Fence* frameFence, const mrhiDriverFrame* frame);

#endif // MAUL_RHI_SRC_D3D12_LANES_H
