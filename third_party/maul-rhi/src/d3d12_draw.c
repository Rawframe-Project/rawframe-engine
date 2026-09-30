// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's draws (d3d12_draw.h). D3D12's vertex and instance
// ids leave out the first vertex and instance, so a pipeline whose
// vertex entry reads them gets them as root constants: set before a
// direct draw, and for an indirect one copied from its arguments into
// the slot's scratch buffer ahead of them, where the pipeline's command
// signature sets them. Other indirect draws and dispatches read their
// arguments in place. A resolve copies the queries the frame wrote,
// occlusion as D3D12's binary occlusion (0 or 1) and timestamps in the
// queue's ticks, and zeros for the others, which D3D12 would leave
// undefined.

#include "d3d12_draw.h"

#include "d3d12_barrier.h"
#include "d3d12_bind.h"
#include "d3d12_resource.h"
#include "invariant.h"

#include "maul-rhi/encoder.h"

#include <string.h>

void mrhiD3d12SetState(const mrhiD3d12Recorder* recorder, const mrhiCommand* command)
{
    ID3D12GraphicsCommandList* list = recorder->list;
    switch (command->type)
    {
    case mrhiCommandViewport:
    {
        mrhiViewport viewport;
        memcpy(&viewport, &command[1], sizeof(viewport));
        const D3D12_VIEWPORT set = {viewport.x,      viewport.y,        viewport.width,
                                    viewport.height, viewport.minDepth, viewport.maxDepth};
        ID3D12GraphicsCommandList_RSSetViewports(list, 1, &set);
        break;
    }
    case mrhiCommandScissor:
    {
        const D3D12_RECT scissor = {(LONG)command->a, (LONG)command->b,
                                    (LONG)(command->a + command->c),
                                    (LONG)(command->b + command->d)};
        ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &scissor);
        break;
    }
    case mrhiCommandBlendConstant:
    {
        mrhiClearColor color;
        memcpy(&color, &command[1], sizeof(color));
        const FLOAT factor[4] = {color.red, color.green, color.blue, color.alpha};
        ID3D12GraphicsCommandList_OMSetBlendFactor(list, factor);
        break;
    }
    default:
        MRHI_ASSERT(command->type == mrhiCommandStencilReference);
        ID3D12GraphicsCommandList_OMSetStencilRef(list, command->a);
        break;
    }
}

// Sets the vertex information a direct draw's pipeline reads.
static void SetVertexInfo(const mrhiD3d12Recorder* recorder, int32_t baseVertex,
                          uint32_t firstInstance)
{
    const mrhiD3d12Pipeline* pipeline = recorder->pipeline;
    if (pipeline->vertexInfo)
    {
        const uint32_t words[2] = {(uint32_t)baseVertex, firstInstance};
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(
            recorder->list, pipeline->layout.vertexInfoParameter, 2, words, 0);
    }
}

// Takes bytes of the slot's scratch buffer, making it at its first use:
// false, failing the frame, when it cannot.
static bool TakeScratch(mrhiD3d12Recorder* recorder, uint64_t bytes, uint64_t* offsetOut)
{
    if (*recorder->scratch == nullptr)
    {
        const mrhiBufferDef def = {.size = recorder->scratchBytes};
        *recorder->scratch = mrhiD3d12CommitBuffer(recorder->objects, &def);
    }
    if (*recorder->scratch == nullptr || recorder->scratchBytes - recorder->scratchUsed < bytes)
    {
        recorder->status = mrhi_errorCapacity;
        return false;
    }
    *offsetOut = recorder->scratchUsed;
    recorder->scratchUsed += bytes;
    return true;
}

static void MoveScratch(mrhiD3d12Recorder* recorder, D3D12_RESOURCE_STATES state)
{
    if (recorder->scratchState != state)
    {
        mrhiD3d12Transition(recorder, *recorder->scratch, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                            recorder->scratchState, state);
        recorder->scratchState = state;
    }
}

// An indirect draw whose pipeline reads the vertex information: its
// base vertex and first instance copied ahead of its arguments.
static void DrawWithInfo(mrhiD3d12Recorder* recorder, uint32_t object, uint64_t offset,
                         bool indexed)
{
    // The draw's arguments, and where the base vertex and first
    // instance lie in them.
    uint64_t bytes = indexed ? sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) : sizeof(D3D12_DRAW_ARGUMENTS);
    uint64_t info = indexed ? 3 * sizeof(uint32_t) : 2 * sizeof(uint32_t);
    uint64_t at = 0;
    if (!TakeScratch(recorder, 2 * sizeof(uint32_t) + bytes, &at))
    {
        return;
    }
    mrhiD3d12Use(recorder, object, D3D12_RESOURCE_STATE_COPY_SOURCE);
    MoveScratch(recorder, D3D12_RESOURCE_STATE_COPY_DEST);
    mrhiD3d12FlushBarriers(recorder);
    ID3D12Resource* arguments = recorder->table[object - 1].resource;
    ID3D12Resource* scratch = *recorder->scratch;
    ID3D12GraphicsCommandList_CopyBufferRegion(recorder->list, scratch, at, arguments,
                                               offset + info, 2 * sizeof(uint32_t));
    ID3D12GraphicsCommandList_CopyBufferRegion(recorder->list, scratch, at + 2 * sizeof(uint32_t),
                                               arguments, offset, bytes);
    MoveScratch(recorder, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    mrhiD3d12Ready(recorder);
    const mrhiD3d12Pipeline* pipeline = recorder->pipeline;
    ID3D12GraphicsCommandList_ExecuteIndirect(recorder->list,
                                              indexed ? pipeline->drawIndexed : pipeline->draw, 1,
                                              scratch, at, nullptr, 0);
}

// An indirect draw or dispatch reading its arguments in place.
static void Indirect(mrhiD3d12Recorder* recorder, uint32_t object, uint64_t offset,
                     mrhiD3d12Indirect kind)
{
    mrhiD3d12Use(recorder, object, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    mrhiD3d12Ready(recorder);
    ID3D12GraphicsCommandList_ExecuteIndirect(recorder->list, recorder->signatures[kind], 1,
                                              recorder->table[object - 1].resource, offset, nullptr,
                                              0);
}

void mrhiD3d12Draw(mrhiD3d12Recorder* recorder, const mrhiCommand* command)
{
    ID3D12GraphicsCommandList* list = recorder->list;
    bool info = recorder->pipeline != nullptr && recorder->pipeline->vertexInfo;
    switch (command->type)
    {
    case mrhiCommandDraw:
        mrhiD3d12Ready(recorder);
        SetVertexInfo(recorder, (int32_t)command->c, (uint32_t)command->d);
        ID3D12GraphicsCommandList_DrawInstanced(list, command->a, (UINT)command->b,
                                                (UINT)command->c, (UINT)command->d);
        break;
    case mrhiCommandDrawIndexed:
    {
        int32_t baseVertex = (int32_t)(uint32_t)(command->c >> 32);
        mrhiD3d12Ready(recorder);
        SetVertexInfo(recorder, baseVertex, (uint32_t)command->d);
        ID3D12GraphicsCommandList_DrawIndexedInstanced(list, command->a, (UINT)command->b,
                                                       (UINT)(uint32_t)command->c, baseVertex,
                                                       (UINT)command->d);
        break;
    }
    case mrhiCommandDispatch:
        mrhiD3d12Ready(recorder);
        ID3D12GraphicsCommandList_Dispatch(list, command->a, (UINT)command->b, (UINT)command->c);
        break;
    case mrhiCommandDrawIndirect:
    case mrhiCommandDrawIndexedIndirect:
    {
        bool indexed = command->type == mrhiCommandDrawIndexedIndirect;
        if (info)
        {
            DrawWithInfo(recorder, command->a, command->c, indexed);
        }
        else
        {
            Indirect(recorder, command->a, command->c,
                     indexed ? mrhiD3d12IndirectDrawIndexed : mrhiD3d12IndirectDraw);
        }
        break;
    }
    default:
        MRHI_ASSERT(command->type == mrhiCommandDispatchIndirect);
        Indirect(recorder, command->a, command->c, mrhiD3d12IndirectDispatch);
        break;
    }
}

static mrhiD3d12QuerySet* SetOf(const mrhiD3d12Recorder* recorder, uint64_t handle)
{
    MRHI_ASSERT(handle != 0 && handle <= recorder->objects->querySetSlots.capacity);
    return &recorder->objects->querySets[handle - 1];
}

// Marks a query written by the frame, forgetting what earlier frames
// wrote.
static void Mark(const mrhiD3d12Recorder* recorder, mrhiD3d12QuerySet* set, uint32_t query)
{
    if (set->serial != recorder->serial)
    {
        set->serial = recorder->serial;
        memset(set->written, 0, sizeof(set->written));
    }
    set->written[query / 64] |= UINT64_C(1) << (query % 64);
}

static bool IsWritten(const mrhiD3d12Recorder* recorder, const mrhiD3d12QuerySet* set,
                      uint32_t query)
{
    return set->serial == recorder->serial && (set->written[query / 64] >> (query % 64) & 1u) != 0;
}

// Resolves the queries the frame wrote and copies zeros for the others,
// in runs of either.
static void Resolve(mrhiD3d12Recorder* recorder, const mrhiCommand* command)
{
    const mrhiD3d12QuerySet* set = SetOf(recorder, command->b);
    uint32_t object = command->a;
    mrhiD3d12Use(recorder, object, D3D12_RESOURCE_STATE_COPY_DEST);
    mrhiD3d12FlushBarriers(recorder);
    ID3D12Resource* buffer = recorder->table[object - 1].resource;
    uint32_t first = (uint32_t)command->c;
    uint32_t count = (uint32_t)(command->c >> 32);
    uint32_t i = 0;
    while (i < count)
    {
        bool written = IsWritten(recorder, set, first + i);
        uint32_t run = 1;
        while (i + run < count && IsWritten(recorder, set, first + i + run) == written)
        {
            ++run;
        }
        uint64_t offset = command->d + (uint64_t)i * sizeof(uint64_t);
        if (written)
        {
            ID3D12GraphicsCommandList_ResolveQueryData(recorder->list, set->heap, set->type,
                                                       first + i, run, buffer, offset);
        }
        else
        {
            ID3D12GraphicsCommandList_CopyBufferRegion(recorder->list, buffer, offset,
                                                       recorder->zeros, 0,
                                                       (uint64_t)run * sizeof(uint64_t));
        }
        i += run;
    }
}

void mrhiD3d12PassTimestamp(const mrhiD3d12Recorder* recorder, bool end)
{
    const mrhiDriverPass* pass = recorder->pass;
    uint32_t query = end ? pass->timestampEnd : pass->timestampBegin;
    if (pass->timestampSet == 0 || query == MRHI_NO_QUERY)
    {
        return;
    }
    mrhiD3d12QuerySet* set = SetOf(recorder, pass->timestampSet);
    Mark(recorder, set, query);
    ID3D12GraphicsCommandList_EndQuery(recorder->list, set->heap, D3D12_QUERY_TYPE_TIMESTAMP,
                                       query);
}

void mrhiD3d12Query(mrhiD3d12Recorder* recorder, const mrhiCommand* command)
{
    switch (command->type)
    {
    case mrhiCommandBeginOcclusionQuery:
    {
        mrhiD3d12QuerySet* set = SetOf(recorder, command->b);
        Mark(recorder, set, command->a);
        recorder->openQuery = command->a;
        ID3D12GraphicsCommandList_BeginQuery(recorder->list, set->heap,
                                             D3D12_QUERY_TYPE_BINARY_OCCLUSION, command->a);
        break;
    }
    case mrhiCommandEndOcclusionQuery:
        ID3D12GraphicsCommandList_EndQuery(recorder->list,
                                           SetOf(recorder, recorder->pass->occlusionSet)->heap,
                                           D3D12_QUERY_TYPE_BINARY_OCCLUSION, recorder->openQuery);
        break;
    default:
        Resolve(recorder, command);
        break;
    }
}
