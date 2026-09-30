// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's barriers (d3d12_barrier.h), D3D12's classic
// transitions. A texture's state follows the core's plan, which gives a
// barrier at each of its changes; a transition between two uses of the
// same unordered access state is a UAV barrier. A buffer's state is the
// driver's: the plan gives no barrier between two reads, which D3D12's
// states tell apart, so each use moves the buffer where its state lacks
// the use's, gathering read states together, and a plan's barrier
// between storage writes on a buffer in the unordered access state is a
// UAV barrier.

#include "d3d12_barrier.h"

#include "capabilities_core.h"
#include "invariant.h"

#define SHADER_READ                                                                                \
    (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)

// The states that write, which gather with no other.
#define WRITES                                                                                     \
    (D3D12_RESOURCE_STATE_UNORDERED_ACCESS | D3D12_RESOURCE_STATE_COPY_DEST |                      \
     D3D12_RESOURCE_STATE_RENDER_TARGET | D3D12_RESOURCE_STATE_DEPTH_WRITE |                       \
     D3D12_RESOURCE_STATE_RESOLVE_DEST | D3D12_RESOURCE_STATE_STREAM_OUT)

D3D12_RESOURCE_STATES mrhiD3d12TextureState(mrhiResourceState state)
{
    switch (state)
    {
    case mrhi_stateSampled:
    case mrhi_stateSealed:
        return SHADER_READ;
    case mrhi_stateStorageRead:
    case mrhi_stateStorageWrite:
    case mrhi_stateStorageReadWrite:
        return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case mrhi_stateCopySource:
        return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case mrhi_stateCopyDestination:
        return D3D12_RESOURCE_STATE_COPY_DEST;
    case mrhi_stateColorTarget:
        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case mrhi_stateResolve:
        return D3D12_RESOURCE_STATE_RESOLVE_DEST;
    case mrhi_stateDepthTarget:
        return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case mrhi_stateDepthRead:
        return D3D12_RESOURCE_STATE_DEPTH_READ | SHADER_READ;
    default:
        // Undefined and present: a new texture's state, and a surface
        // image's.
        return D3D12_RESOURCE_STATE_COMMON;
    }
}

void mrhiD3d12FlushBarriers(mrhiD3d12Recorder* recorder)
{
    if (recorder->barrierCount > 0)
    {
        ID3D12GraphicsCommandList_ResourceBarrier(recorder->list, recorder->barrierCount,
                                                  recorder->barriers);
        recorder->barrierCount = 0;
    }
}

static void Queue(mrhiD3d12Recorder* recorder, const D3D12_RESOURCE_BARRIER* barrier)
{
    if (recorder->barrierCount == MRHI_D3D12_BARRIERS)
    {
        mrhiD3d12FlushBarriers(recorder);
    }
    recorder->barriers[recorder->barrierCount++] = *barrier;
}

static void QueueUav(mrhiD3d12Recorder* recorder, ID3D12Resource* resource)
{
    D3D12_RESOURCE_BARRIER barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV,
                                      .UAV = {.pResource = resource}};
    Queue(recorder, &barrier);
}

// Makes a placed resource the one its memory holds, after whatever the
// frame placed there before.
static void QueueAliasing(mrhiD3d12Recorder* recorder, ID3D12Resource* resource)
{
    D3D12_RESOURCE_BARRIER barrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING,
                                      .Aliasing = {.pResourceAfter = resource}};
    Queue(recorder, &barrier);
}

// Discards a placed target whole, in the target state its kind takes,
// which its undefined parts then start from.
static void Discard(mrhiD3d12Recorder* recorder, mrhiD3d12Object* object)
{
    bool depth = mrhiFormatHasDepth(object->texture->format);
    D3D12_RESOURCE_STATES target =
        depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET;
    mrhiD3d12Transition(recorder, object->resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                        object->initial, target);
    mrhiD3d12FlushBarriers(recorder);
    ID3D12GraphicsCommandList_DiscardResource(recorder->list, object->resource, nullptr);
    object->initial = target;
    object->discard = false;
}

void mrhiD3d12Transition(mrhiD3d12Recorder* recorder, ID3D12Resource* resource, UINT subresource,
                         D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    if (before == after)
    {
        if (before == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        {
            QueueUav(recorder, resource);
        }
        return;
    }
    D3D12_RESOURCE_BARRIER barrier = {
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Transition = {.pResource = resource,
                       .Subresource = subresource,
                       .StateBefore = before,
                       .StateAfter = after},
    };
    Queue(recorder, &barrier);
}

// The planes a texture has, and those an aspect covers: depth is the
// first, stencil the second, and a format with one aspect has one plane.
static uint32_t PlanesOf(const mrhiTextureDef* def, mrhiTextureAspect aspect, uint32_t* firstOut,
                         uint32_t* countOut)
{
    bool both = mrhiFormatHasDepth(def->format) && mrhiFormatHasStencil(def->format);
    *firstOut = both && aspect == mrhi_aspectStencilOnly ? 1 : 0;
    *countOut = both && aspect == mrhi_aspectAll ? 2 : 1;
    return both ? 2 : 1;
}

// Queues a texture's transitions over a range, the whole texture at
// once when the range covers it.
static void TextureBarrier(mrhiD3d12Recorder* recorder, mrhiD3d12Object* object,
                           const mrhiBarrier* barrier)
{
    const mrhiTextureDef* def = object->texture;
    const mrhiTextureRange* range = &barrier->range;
    bool undefined = barrier->before == mrhi_stateUndefined;
    if (undefined && object->discard)
    {
        Discard(recorder, object);
    }
    D3D12_RESOURCE_STATES before =
        undefined ? object->initial : mrhiD3d12TextureState(barrier->before);
    D3D12_RESOURCE_STATES after = mrhiD3d12TextureState(barrier->after);
    uint32_t layers = def->kind == mrhi_texture3d ? 1 : def->depthOrLayers;
    uint32_t firstPlane = 0;
    uint32_t planes = 0;
    uint32_t allPlanes = PlanesOf(def, range->aspect, &firstPlane, &planes);
    bool whole = range->baseMip == 0 && range->mipCount == def->mipLevels &&
                 range->baseLayer == 0 && range->layerCount >= layers && planes == allPlanes;
    if (whole)
    {
        mrhiD3d12Transition(recorder, object->resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                            before, after);
        return;
    }
    for (uint32_t plane = firstPlane; plane < firstPlane + planes; ++plane)
    {
        for (uint32_t layer = range->baseLayer; layer < range->baseLayer + range->layerCount;
             ++layer)
        {
            for (uint32_t mip = range->baseMip; mip < range->baseMip + range->mipCount; ++mip)
            {
                UINT subresource = mip + (layer + plane * layers) * def->mipLevels;
                mrhiD3d12Transition(recorder, object->resource, subresource, before, after);
            }
        }
    }
}

// Whether a state writes, as the core's plan counts writes.
static bool Writes(mrhiResourceState state)
{
    return state == mrhi_stateStorageWrite || state == mrhi_stateStorageReadWrite ||
           state == mrhi_stateCopyDestination || state == mrhi_stateColorTarget ||
           state == mrhi_stateResolve || state == mrhi_stateDepthTarget ||
           state == mrhi_stateQueryResolve;
}

static bool SamePass(mrhiPassId a, mrhiPassId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

void mrhiD3d12RecordBarriers(mrhiD3d12Recorder* recorder, mrhiPassId pass)
{
    const mrhiDriverFrame* frame = recorder->frame;
    while (recorder->barrierAt < frame->barrierCount &&
           SamePass(frame->barriers[recorder->barrierAt].pass, pass))
    {
        const mrhiBarrier* barrier = &frame->barriers[recorder->barrierAt++];
        MRHI_ASSERT(barrier->resource.index1 != 0 &&
                    barrier->resource.index1 <= frame->resourceCount);
        mrhiD3d12Object* object = &recorder->table[barrier->resource.index1 - 1];
        if (object->resource == nullptr)
        {
            continue;
        }
        if (barrier->aliasing)
        {
            QueueAliasing(recorder, object->resource);
        }
        if (object->texture != nullptr)
        {
            TextureBarrier(recorder, object, barrier);
        }
        else if (Writes(barrier->before) && object->state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        {
            QueueUav(recorder, object->resource);
        }
    }
    mrhiD3d12FlushBarriers(recorder);
}

D3D12_RESOURCE_STATES mrhiD3d12BufferState(mrhiResourceState state)
{
    switch (state)
    {
    case mrhi_stateUniform:
    case mrhi_stateVertex:
        return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    case mrhi_stateIndex:
        return D3D12_RESOURCE_STATE_INDEX_BUFFER;
    case mrhi_stateIndirect:
        return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    case mrhi_stateStorageRead:
        return SHADER_READ;
    case mrhi_stateStorageWrite:
    case mrhi_stateStorageReadWrite:
        return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case mrhi_stateCopySource:
        return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case mrhi_stateCopyDestination:
    case mrhi_stateQueryResolve:
        return D3D12_RESOURCE_STATE_COPY_DEST;
    case mrhi_stateSealed:
        return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER |
               D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT | SHADER_READ;
    default:
        return D3D12_RESOURCE_STATE_COMMON;
    }
}

void mrhiD3d12UseDeclared(mrhiD3d12Recorder* recorder, const mrhiDriverPass* pass)
{
    for (uint32_t i = 0; i < pass->accessCount; ++i)
    {
        const mrhiDriverAccess* access = &pass->accesses[i];
        const mrhiD3d12Object* object = &recorder->table[access->resource - 1];
        D3D12_RESOURCE_STATES state = mrhiD3d12BufferState(access->state);
        if (object->resource != nullptr && object->texture == nullptr &&
            state != D3D12_RESOURCE_STATE_COMMON)
        {
            mrhiD3d12Use(recorder, access->resource, state);
        }
    }
    mrhiD3d12FlushBarriers(recorder);
}

static bool IsRead(D3D12_RESOURCE_STATES state)
{
    return state != D3D12_RESOURCE_STATE_COMMON && (state & WRITES) == 0;
}

void mrhiD3d12Use(mrhiD3d12Recorder* recorder, uint32_t object, D3D12_RESOURCE_STATES needed)
{
    MRHI_ASSERT(object != 0 && object <= recorder->frame->resourceCount);
    mrhiD3d12Object* buffer = &recorder->table[object - 1];
    MRHI_ASSERT(buffer->texture == nullptr && buffer->resource != nullptr);
    D3D12_RESOURCE_STATES current = buffer->state;
    bool gather = IsRead(current) && IsRead(needed);
    if (current == needed || (gather && (current & needed) == needed))
    {
        return;
    }
    D3D12_RESOURCE_STATES next = gather ? current | needed : needed;
    mrhiD3d12Transition(recorder, buffer->resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                        current, next);
    buffer->state = next;
}
