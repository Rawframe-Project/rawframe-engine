// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A D3D12 device's lanes (d3d12_lanes.h). Batches run in the order they
// open, which is the frame's order of their first passes, and a batch
// waits only for passes earlier in the frame, whose batches have closed
// by then: every wait names a value whose signal is queued before it.
// Each batch starts its lists after its lane's work before has finished,
// so buffers start each part in the common state they decayed to.

#include "d3d12_lanes.h"

#include "d3d12_barrier.h"
#include "d3d12_copy.h"
#include "invariant.h"

// The states a compute list takes, and a copy list.
#define COMPUTE_STATES                                                                             \
    (D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_UNORDERED_ACCESS |     \
     D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT |     \
     D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_COPY_SOURCE)
#define COPY_STATES (D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_COPY_SOURCE)

D3D12_COMMAND_LIST_TYPE mrhiD3d12ListType(mrhiD3d12Lane lane)
{
    static const D3D12_COMMAND_LIST_TYPE types[] = {
        [mrhiD3d12LaneDirect] = D3D12_COMMAND_LIST_TYPE_DIRECT,
        [mrhiD3d12LaneCompute] = D3D12_COMMAND_LIST_TYPE_COMPUTE,
        [mrhiD3d12LaneCopy] = D3D12_COMMAND_LIST_TYPE_COPY,
    };
    return types[lane];
}

// The states a lane's lists take: every state on the direct lane.
static D3D12_RESOURCE_STATES StatesOf(mrhiD3d12Lane lane)
{
    switch (lane)
    {
    case mrhiD3d12LaneCompute:
        return COMPUTE_STATES;
    case mrhiD3d12LaneCopy:
        return COPY_STATES;
    default:
        return (D3D12_RESOURCE_STATES)~0;
    }
}

// Notes a failure of the frame, the first only.
static void Fail(mrhiD3d12Recorder* recorder, mrhiResult status)
{
    recorder->status = recorder->status == mrhi_success ? status : recorder->status;
}

static mrhiD3d12Lane LaneOfClass(mrhiPassClass passClass)
{
    switch (passClass)
    {
    case mrhi_passAsyncCompute:
        return mrhiD3d12LaneCompute;
    case mrhi_passTransfer:
        return mrhiD3d12LaneCopy;
    default:
        return mrhiD3d12LaneDirect;
    }
}

// Keeps a pass on a lane no later than one: an earlier lane's lists take
// more.
static void Lower(mrhiD3d12Lanes* lanes, mrhiPassId pass, mrhiD3d12Lane most)
{
    mrhiD3d12Lane* lane = &lanes->passLanes[pass.index1];
    *lane = *lane < most ? *lane : most;
}

// The latest lane whose lists make a pass's clears: a copy list has no
// ClearUnorderedAccessViewUint, and only a direct list clears a render
// target's or a depth texture's view.
static mrhiD3d12Lane ClearLane(const mrhiD3d12Recorder* recorder, const mrhiDriverPass* pass)
{
    const mrhiDriverFrame* frame = recorder->frame;
    mrhiD3d12Lane lane = mrhiD3d12LaneCopy;
    for (uint32_t chunk = pass->firstChunk; chunk != 0; chunk = frame->chunks[chunk - 1].next)
    {
        const mrhiCommandChunk* at = &frame->chunks[chunk - 1];
        for (uint32_t i = 0; i < at->count; i += 1u + at->commands[i].payload)
        {
            const mrhiCommand* command = &at->commands[i];
            if (command->type == mrhiCommandClearTexture &&
                mrhiD3d12ClearsOnDirect(recorder->table[command->a - 1].texture))
            {
                return mrhiD3d12LaneDirect;
            }
            if (command->type == mrhiCommandClearBuffer || command->type == mrhiCommandClearTexture)
            {
                lane = mrhiD3d12LaneCompute;
            }
        }
    }
    return lane;
}

static bool ComputeTakes(D3D12_RESOURCE_STATES state)
{
    return (state & ~COMPUTE_STATES) == 0;
}

// Places each pass on its class's lane, or an earlier one where its
// clears, its textures, their barriers or an aliasing barrier ask it. A placed
// target's first use discards it in a target state.
static void Place(mrhiD3d12Lanes* lanes)
{
    const mrhiD3d12Recorder* recorder = lanes->recorder;
    const mrhiDriverFrame* frame = recorder->frame;
    for (uint32_t p = 0; p < frame->passCount; ++p)
    {
        const mrhiDriverPass* pass = &frame->passes[p];
        lanes->passLanes[pass->id.index1] =
            pass->native ? mrhiD3d12LaneDirect : LaneOfClass(pass->passClass);
        Lower(lanes, pass->id, ClearLane(recorder, pass));
        // A timed pass runs where its times tick as the direct queue's,
        // whose frequency is the device's timestamp period: on the compute
        // queue where its frequency is the same, which also takes every
        // copy of a transfer pass, and on the direct queue otherwise. The
        // copy queue would need a query heap of its own type.
        if (pass->timestampSet != 0)
        {
            Lower(lanes, pass->id,
                  lanes->computeTimes ? mrhiD3d12LaneCompute : mrhiD3d12LaneDirect);
        }
        for (uint32_t i = 0; i < pass->accessCount; ++i)
        {
            const mrhiD3d12Object* object = &recorder->table[pass->accesses[i].resource - 1];
            if (object->resource != nullptr && object->texture != nullptr)
            {
                Lower(lanes, pass->id, mrhiD3d12LaneCompute);
                if (!ComputeTakes(mrhiD3d12TextureState(pass->accesses[i].state)))
                {
                    Lower(lanes, pass->id, mrhiD3d12LaneDirect);
                }
            }
        }
    }
    for (size_t i = 0; i < frame->barrierCount; ++i)
    {
        const mrhiBarrier* barrier = &frame->barriers[i];
        if (barrier->pass.index1 == 0)
        {
            continue;
        }
        const mrhiD3d12Object* object = &recorder->table[barrier->resource.index1 - 1];
        if (object->resource == nullptr)
        {
            continue;
        }
        if (barrier->aliasing || object->texture != nullptr)
        {
            Lower(lanes, barrier->pass, mrhiD3d12LaneCompute);
        }
        if (object->texture == nullptr)
        {
            continue;
        }
        bool takes = barrier->before == mrhi_stateUndefined
                         ? !object->discard && ComputeTakes(object->initial)
                         : ComputeTakes(mrhiD3d12TextureState(barrier->before));
        if (!takes || !ComputeTakes(mrhiD3d12TextureState(barrier->after)))
        {
            Lower(lanes, barrier->pass, mrhiD3d12LaneDirect);
        }
    }
}

void mrhiD3d12BeginLanes(mrhiD3d12Lanes* lanes, mrhiD3d12LaneSlot* slot,
                         ID3D12DescriptorHeap* views, ID3D12DescriptorHeap* samplers,
                         uint64_t frameWait)
{
    lanes->slot = slot;
    lanes->heaps[0] = views;
    lanes->heaps[1] = samplers;
    lanes->frameWait = frameWait;
    lanes->several = false;
    lanes->current = mrhiD3d12LaneDirect;
    for (int lane = 0; lane < mrhiD3d12LaneCount; ++lane)
    {
        lanes->open[lane] = 0;
        lanes->lists[lane] = nullptr;
        lanes->laneRunCount[lane] = 0;
        lanes->last[lane] = 0;
        lanes->started[lane] = false;
        slot->listsUsed[lane] = 0;
        if (FAILED(ID3D12CommandAllocator_Reset(slot->allocators[lane])))
        {
            Fail(lanes->recorder, mrhi_errorDeviceLost);
        }
    }
    for (uint32_t c = 0; c < MRHI_PASS_CLASSES; ++c)
    {
        lanes->lastOf[c] = 0;
    }
    slot->runCount = 0;
    slot->batchCount = 0;
    const mrhiDriverFrame* frame = lanes->recorder->frame;
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
        mrhiD3d12Lane lane = lanes->passLanes[pass->id.index1];
        lanes->several = lanes->several || lane != mrhiD3d12LaneDirect;
        for (uint32_t c = 0; c <= MRHI_PASS_CLASSES; ++c)
        {
            uint32_t followed =
                c < MRHI_PASS_CLASSES ? pass->waitFor[c].index1 : lastOf[pass->passClass];
            lanes->followed[followed] =
                lanes->followed[followed] || (followed != 0 && lanes->passLanes[followed] != lane);
        }
        lastOf[pass->passClass] = pass->id.index1;
    }
}

// Starts a part on a lane: its list begun, which has no root signature
// until a pipeline sets its own, and on the direct and compute lanes the
// slot's shader-visible heaps set, and its sealed buffers moved into
// every read a heap may make of them on the lane, since a heap may read
// one in any pass without it being declared. Buffers start the part in
// the common state they decayed to.
static void BeginPart(mrhiD3d12Lanes* lanes, mrhiD3d12Lane lane)
{
    mrhiD3d12Recorder* recorder = lanes->recorder;
    mrhiD3d12LaneSlot* slot = lanes->slot;
    // A batch takes a list, and each native pass one more.
    MRHI_ASSERT(slot->listsUsed[lane] < lanes->listLimit[lane]);
    ID3D12GraphicsCommandList** list = &slot->lists[lane][slot->listsUsed[lane]];
    if (*list == nullptr)
    {
        // A list is made recording.
        if (FAILED(ID3D12Device_CreateCommandList(lanes->device, 0, mrhiD3d12ListType(lane),
                                                  slot->allocators[lane], nullptr,
                                                  &IID_ID3D12GraphicsCommandList, (void**)list)))
        {
            *list = nullptr;
            mrhiD3d12RunOut(recorder, mrhi_diagnosticSubmitCommandLists);
            return;
        }
    }
    else if (FAILED(ID3D12GraphicsCommandList_Reset(*list, slot->allocators[lane], nullptr)))
    {
        Fail(recorder, mrhi_errorDeviceLost);
        return;
    }
    ++slot->listsUsed[lane];
    lanes->lists[lane] = *list;
    lanes->current = lane;
    recorder->list = *list;
    recorder->lane = lane;
    recorder->laneStates = StatesOf(lane);
    recorder->graphicsRoot = nullptr;
    recorder->computeRoot = nullptr;
    const mrhiDriverFrame* frame = recorder->frame;
    for (uint32_t i = 0; i < frame->resourceCount; ++i)
    {
        recorder->table[i].state[lane] = D3D12_RESOURCE_STATE_COMMON;
    }
    if (lane == mrhiD3d12LaneDirect)
    {
        recorder->scratchState = D3D12_RESOURCE_STATE_COMMON;
    }
    if (lane == mrhiD3d12LaneCopy)
    {
        return;
    }
    ID3D12GraphicsCommandList_SetDescriptorHeaps(recorder->list, 2, lanes->heaps);
    for (uint32_t i = 0; i < frame->resourceCount; ++i)
    {
        if (frame->resources[i].sealed && recorder->table[i].resource != nullptr &&
            recorder->table[i].texture == nullptr)
        {
            mrhiD3d12Use(recorder, i + 1, mrhiD3d12BufferState(mrhi_stateSealed));
        }
    }
    mrhiD3d12FlushBarriers(recorder);
}

// Ends a lane's part, if one is recording, and adds it, then a list of
// the program's (none when NULL), to the lane's batch: whether D3D12
// closed it.
static bool EndPart(mrhiD3d12Lanes* lanes, mrhiD3d12Lane lane, void* native)
{
    ID3D12GraphicsCommandList* list = lanes->lists[lane];
    if (list == nullptr)
    {
        return true;
    }
    lanes->lists[lane] = nullptr;
    bool closed = SUCCEEDED(ID3D12GraphicsCommandList_Close(list));
    uint32_t* count = &lanes->laneRunCount[lane];
    MRHI_ASSERT(*count + (native != nullptr ? 2u : 1u) <= MRHI_D3D12_BATCH_RUNS);
    lanes->laneRuns[lane][(*count)++] = (ID3D12CommandList*)list;
    if (native != nullptr)
    {
        lanes->laneRuns[lane][(*count)++] = (ID3D12CommandList*)native;
    }
    return closed;
}

// Opens a batch on a lane that waits for the other lanes' given values,
// and for the frame before where it is the lane's first beside the
// direct lane.
static void Open(mrhiD3d12Lanes* lanes, mrhiD3d12Lane lane, const uint64_t* waits)
{
    mrhiD3d12LaneSlot* slot = lanes->slot;
    // A pass opens one batch at most, beside the frame's last.
    MRHI_ASSERT(slot->batchCount < lanes->batchLimit);
    mrhiD3d12Batch* batch = &slot->batches[slot->batchCount++];
    *batch = (mrhiD3d12Batch){.lane = lane};
    for (int l = 0; l < mrhiD3d12LaneCount; ++l)
    {
        batch->waits[l] = l == (int)lane ? 0 : waits[l];
    }
    if (lane != mrhiD3d12LaneDirect && !lanes->started[lane])
    {
        batch->frameWait = lanes->frameWait;
    }
    lanes->started[lane] = true;
    lanes->open[lane] = slot->batchCount;
    lanes->laneRunCount[lane] = 0;
    BeginPart(lanes, lane);
}

// Closes a lane's open batch, if any: its lists join the slot's runs,
// and with work beyond the direct lane it signals the lane's next value.
// After a failure its list is only closed, and nothing runs: false when
// D3D12 fails to close it.
static bool Close(mrhiD3d12Lanes* lanes, mrhiD3d12Lane lane)
{
    if (lanes->open[lane] == 0)
    {
        return true;
    }
    bool closed = EndPart(lanes, lane, nullptr);
    mrhiD3d12LaneSlot* slot = lanes->slot;
    mrhiD3d12Batch* batch = &slot->batches[lanes->open[lane] - 1];
    lanes->open[lane] = 0;
    if (lanes->recorder->status != mrhi_success)
    {
        return closed;
    }
    MRHI_ASSERT(slot->runCount + lanes->laneRunCount[lane] <= lanes->runLimit);
    batch->firstRun = slot->runCount;
    batch->runCount = lanes->laneRunCount[lane];
    for (uint32_t i = 0; i < batch->runCount; ++i)
    {
        slot->runs[slot->runCount++] = lanes->laneRuns[lane][i];
    }
    if (lanes->several)
    {
        batch->signal = ++lanes->values[lane];
        lanes->last[lane] = batch->signal;
    }
    return closed;
}

// Makes a lane's list the recorder's. The roots the recorder knows are
// those set on its list, so they are forgotten when the list changes.
static void Switch(mrhiD3d12Lanes* lanes, mrhiD3d12Lane lane)
{
    mrhiD3d12Recorder* recorder = lanes->recorder;
    if (lanes->current != lane)
    {
        recorder->graphicsRoot = nullptr;
        recorder->computeRoot = nullptr;
    }
    lanes->current = lane;
    recorder->list = lanes->lists[lane];
    recorder->lane = lane;
    recorder->laneStates = StatesOf(lane);
}

void mrhiD3d12EnterPass(mrhiD3d12Lanes* lanes, const mrhiDriverPass* pass)
{
    mrhiD3d12Recorder* recorder = lanes->recorder;
    mrhiD3d12LaneSlot* slot = lanes->slot;
    mrhiD3d12Lane lane = lanes->passLanes[pass->id.index1];
    uint64_t waits[mrhiD3d12LaneCount] = {0};
    bool follows = false;
    for (uint32_t c = 0; c <= MRHI_PASS_CLASSES; ++c)
    {
        uint32_t followed =
            c < MRHI_PASS_CLASSES ? pass->waitFor[c].index1 : lanes->lastOf[pass->passClass];
        if (followed == 0 || lanes->passLanes[followed] == lane)
        {
            continue;
        }
        // Each pass another lane follows ends its batch.
        const mrhiD3d12Batch* batch = &slot->batches[lanes->passBatches[followed] - 1];
        MRHI_ASSERT(batch->signal != 0);
        mrhiD3d12Lane other = lanes->passLanes[followed];
        waits[other] = batch->signal > waits[other] ? batch->signal : waits[other];
        follows = true;
    }
    lanes->lastOf[pass->passClass] = pass->id.index1;
    if (follows && !Close(lanes, lane))
    {
        Fail(recorder, mrhi_errorDeviceLost);
    }
    if (recorder->status != mrhi_success)
    {
        return;
    }
    if (lanes->open[lane] == 0)
    {
        Open(lanes, lane, waits);
    }
    lanes->passBatches[pass->id.index1] = lanes->open[lane];
    if (recorder->status == mrhi_success)
    {
        Switch(lanes, lane);
    }
}

void mrhiD3d12LeavePass(mrhiD3d12Lanes* lanes, const mrhiDriverPass* pass)
{
    if (lanes->followed[pass->id.index1] && lanes->recorder->status == mrhi_success &&
        !Close(lanes, lanes->passLanes[pass->id.index1]))
    {
        Fail(lanes->recorder, mrhi_errorDeviceLost);
    }
}

void mrhiD3d12EndLanes(mrhiD3d12Lanes* lanes)
{
    uint64_t waits[mrhiD3d12LaneCount] = {0};
    for (int lane = 0; lanes->several && lane < mrhiD3d12LaneCount; ++lane)
    {
        if (!Close(lanes, (mrhiD3d12Lane)lane))
        {
            Fail(lanes->recorder, mrhi_errorDeviceLost);
        }
        waits[lane] = lanes->last[lane];
    }
    if (lanes->recorder->status != mrhi_success)
    {
        return;
    }
    if (lanes->open[mrhiD3d12LaneDirect] == 0)
    {
        Open(lanes, mrhiD3d12LaneDirect, waits);
    }
    if (lanes->recorder->status == mrhi_success)
    {
        Switch(lanes, mrhiD3d12LaneDirect);
    }
}

bool mrhiD3d12CloseLanes(mrhiD3d12Lanes* lanes)
{
    bool closed = true;
    for (int lane = 0; lane < mrhiD3d12LaneCount; ++lane)
    {
        closed = Close(lanes, (mrhiD3d12Lane)lane) && closed;
    }
    return closed;
}

void mrhiD3d12NextPart(mrhiD3d12Lanes* lanes, void* native)
{
    MRHI_ASSERT(lanes->current == mrhiD3d12LaneDirect);
    if (!EndPart(lanes, mrhiD3d12LaneDirect, native))
    {
        Fail(lanes->recorder, mrhi_errorDeviceLost);
        return;
    }
    BeginPart(lanes, mrhiD3d12LaneDirect);
}

// Whether a batch waits on a lane for a batch that runs before it, for
// at least the value that one signals.
static bool WaitsFor(const mrhiD3d12LaneSlot* slot, uint32_t held, uint32_t earlier,
                     mrhiD3d12Lane lane)
{
    if (earlier == 0 || earlier >= held)
    {
        return false;
    }
    const mrhiD3d12Batch* before = &slot->batches[earlier - 1];
    return before->lane == lane && before->signal != 0 &&
           slot->batches[held - 1].waits[lane] >= before->signal;
}

// Whether a recorded frame's batches order every pass after the passes
// it follows, worked out again from the frame view: those its waitFor
// names and the pass of its class before it, on its own lane in an
// earlier or the same batch, on another lane through its batch's wait.
// The compute and copy lanes' first batches wait for the frame before,
// and with work beyond the direct lane the frame's last batch is on it
// and waits for every lane's last. Only an assertion calls it.
[[maybe_unused]] static bool HoldsFollows(const mrhiD3d12Lanes* lanes,
                                          const mrhiD3d12LaneSlot* slot,
                                          const mrhiDriverFrame* frame)
{
    uint32_t lastOf[MRHI_PASS_CLASSES] = {0};
    for (uint32_t p = 0; p < frame->passCount; ++p)
    {
        const mrhiDriverPass* pass = &frame->passes[p];
        uint32_t held = lanes->passBatches[pass->id.index1];
        mrhiD3d12Lane lane = lanes->passLanes[pass->id.index1];
        if (held == 0 || slot->batches[held - 1].lane != lane)
        {
            return false;
        }
        for (uint32_t c = 0; c <= MRHI_PASS_CLASSES; ++c)
        {
            uint32_t followed =
                c < MRHI_PASS_CLASSES ? pass->waitFor[c].index1 : lastOf[pass->passClass];
            uint32_t earlier = followed == 0 ? 0 : lanes->passBatches[followed];
            bool ordered =
                followed == 0 || (lanes->passLanes[followed] == lane
                                      ? earlier != 0 && earlier <= held
                                      : WaitsFor(slot, held, earlier, lanes->passLanes[followed]));
            if (!ordered)
            {
                return false;
            }
        }
        lastOf[pass->passClass] = pass->id.index1;
    }
    bool seen[mrhiD3d12LaneCount] = {0};
    const mrhiD3d12Batch* end = &slot->batches[slot->batchCount - 1];
    for (uint32_t b = 0; b < slot->batchCount; ++b)
    {
        const mrhiD3d12Batch* batch = &slot->batches[b];
        bool other = batch->lane != mrhiD3d12LaneDirect;
        if (other && !seen[batch->lane] && batch->frameWait != lanes->frameWait)
        {
            return false;
        }
        seen[batch->lane] = true;
        if (lanes->several && other && end->waits[batch->lane] < batch->signal)
        {
            return false;
        }
    }
    return end->lane == mrhiD3d12LaneDirect;
}

mrhiResult mrhiD3d12RunLanes(const mrhiD3d12Lanes* lanes, const mrhiD3d12LaneSlot* slot,
                             ID3D12Fence* frameFence, [[maybe_unused]] const mrhiDriverFrame* frame)
{
    MRHI_ASSERT(HoldsFollows(lanes, slot, frame));
    // Stops at the first wait or signal that fails, on any lane: a batch
    // waits only for earlier batches, so every wait queued then refers to
    // a signal that went in, and what was queued finishes by itself.
    for (uint32_t b = 0; b < slot->batchCount; ++b)
    {
        const mrhiD3d12Batch* batch = &slot->batches[b];
        ID3D12CommandQueue* queue = lanes->queues[batch->lane];
        for (int lane = 0; lane < mrhiD3d12LaneCount; ++lane)
        {
            if (batch->waits[lane] != 0 &&
                FAILED(ID3D12CommandQueue_Wait(queue, lanes->fences[lane], batch->waits[lane])))
            {
                return mrhi_errorDeviceLost;
            }
        }
        if (batch->frameWait != 0 &&
            FAILED(ID3D12CommandQueue_Wait(queue, frameFence, batch->frameWait)))
        {
            return mrhi_errorDeviceLost;
        }
        for (uint32_t i = 0; i < batch->runCount; ++i)
        {
            ID3D12CommandQueue_ExecuteCommandLists(queue, 1, &slot->runs[batch->firstRun + i]);
        }
        if (batch->signal != 0 &&
            FAILED(ID3D12CommandQueue_Signal(queue, lanes->fences[batch->lane], batch->signal)))
        {
            return mrhi_errorDeviceLost;
        }
    }
    return mrhi_success;
}
