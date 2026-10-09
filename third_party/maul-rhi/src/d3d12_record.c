// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's passes (d3d12_record.h). A pass's barriers come
// first, with its declared buffers moved to their states, then its
// debug group, then, for a pass with targets, a render target view of
// each target's mip and layer and a depth stencil view, read-only for a
// read-only depth target, taken from the slot's rings, with the clears
// its loads ask for; its viewport and scissor start as its area. Its
// commands follow, each pass setting its own pipeline, tables and
// buffers (d3d12_bind.c), then draws and queries (d3d12_draw.c), copies
// (d3d12_copy.c) and labels; at its end its multisampled targets
// resolve, each moved to the resolve source state and back. A pass's
// timestamps are written after its barriers and after its resolves, the
// span of its work. Discarding loads and stores keep the contents,
// which D3D12 allows.

#include "d3d12_record.h"

#include "capabilities_core.h"
#include "d3d12_barrier.h"
#include "d3d12_bind.h"
#include "d3d12_copy.h"
#include "d3d12_draw.h"
#include "d3d12_names.h"
#include "invariant.h"

#include <string.h>

// A descriptor of a ring; the ring is sized so that no frame runs out.
static D3D12_CPU_DESCRIPTOR_HANDLE Take(mrhiD3d12Ring* ring)
{
    MRHI_ASSERT(ring->taken < ring->capacity);
    uint32_t index = ring->taken++;
    return (D3D12_CPU_DESCRIPTOR_HANDLE){.ptr = ring->start.ptr + (SIZE_T)ring->step * index};
}

static const mrhiD3d12Object* TargetOf(const mrhiD3d12Recorder* recorder, mrhiResourceId resource)
{
    MRHI_ASSERT(resource.index1 != 0 && resource.index1 <= recorder->frame->resourceCount);
    const mrhiD3d12Object* object = &recorder->table[resource.index1 - 1];
    MRHI_ASSERT(object->texture != nullptr);
    return object;
}

static D3D12_CPU_DESCRIPTOR_HANDLE ColorView(mrhiD3d12Recorder* recorder,
                                             const mrhiColorTarget* target)
{
    const mrhiD3d12Object* object = TargetOf(recorder, target->resource);
    const mrhiTextureDef* def = object->texture;
    // The target's view format, which the core always names.
    D3D12_RENDER_TARGET_VIEW_DESC desc = {.Format = mrhiD3d12Format(target->viewFormat)};
    if (def->kind == mrhi_texture3d)
    {
        desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
        desc.Texture3D =
            (D3D12_TEX3D_RTV){.MipSlice = target->mip, .FirstWSlice = target->layer, .WSize = 1};
    }
    else if (def->sampleCount > 1)
    {
        desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
        desc.Texture2DMSArray = (D3D12_TEX2DMS_ARRAY_RTV){.FirstArraySlice = target->layer,
                                                          .ArraySize = recorder->pass->viewCount};
    }
    else
    {
        desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        // A layer per view (mrhi-0020).
        desc.Texture2DArray = (D3D12_TEX2D_ARRAY_RTV){.MipSlice = target->mip,
                                                      .FirstArraySlice = target->layer,
                                                      .ArraySize = recorder->pass->viewCount};
    }
    D3D12_CPU_DESCRIPTOR_HANDLE view = Take(&recorder->targets);
    ID3D12Device_CreateRenderTargetView(recorder->device, object->resource, &desc, view);
    return view;
}

static D3D12_CPU_DESCRIPTOR_HANDLE DepthView(mrhiD3d12Recorder* recorder,
                                             const mrhiDepthTarget* target)
{
    const mrhiD3d12Object* object = TargetOf(recorder, target->resource);
    const mrhiTextureDef* def = object->texture;
    D3D12_DEPTH_STENCIL_VIEW_DESC desc = {.Format = mrhiD3d12Format(def->format)};
    if (target->readOnly)
    {
        desc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH |
                     (mrhiFormatHasStencil(def->format) ? D3D12_DSV_FLAG_READ_ONLY_STENCIL
                                                        : D3D12_DSV_FLAG_NONE);
    }
    if (def->sampleCount > 1)
    {
        desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
        desc.Texture2DMSArray = (D3D12_TEX2DMS_ARRAY_DSV){.FirstArraySlice = target->layer,
                                                          .ArraySize = recorder->pass->viewCount};
    }
    else
    {
        desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray = (D3D12_TEX2D_ARRAY_DSV){.MipSlice = target->mip,
                                                      .FirstArraySlice = target->layer,
                                                      .ArraySize = recorder->pass->viewCount};
    }
    D3D12_CPU_DESCRIPTOR_HANDLE view = Take(&recorder->depths);
    ID3D12Device_CreateDepthStencilView(recorder->device, object->resource, &desc, view);
    return view;
}

// Clears what the depth target's loads ask for.
static void ClearDepth(const mrhiD3d12Recorder* recorder, D3D12_CPU_DESCRIPTOR_HANDLE view)
{
    const mrhiDepthTarget* target = &recorder->pass->depthTarget;
    mrhiFormat format = TargetOf(recorder, target->resource)->texture->format;
    D3D12_CLEAR_FLAGS flags = 0;
    if (mrhiFormatHasDepth(format) && target->depthLoad == mrhi_loadClear)
    {
        flags |= D3D12_CLEAR_FLAG_DEPTH;
    }
    if (mrhiFormatHasStencil(format) && target->stencilLoad == mrhi_loadClear)
    {
        flags |= D3D12_CLEAR_FLAG_STENCIL;
    }
    if (flags != 0 && !target->readOnly)
    {
        ID3D12GraphicsCommandList_ClearDepthStencilView(recorder->list, view, flags,
                                                        target->clearDepth,
                                                        (UINT8)target->clearStencil, 0, nullptr);
    }
}

// Sets the pass's targets, clears them as its loads ask, and starts its
// viewport and scissor as its area.
static void StartTargets(mrhiD3d12Recorder* recorder)
{
    const mrhiDriverPass* pass = recorder->pass;
    D3D12_CPU_DESCRIPTOR_HANDLE colors[MRHI_COLOR_TARGETS] = {{0}};
    for (uint32_t i = 0; i < pass->colorTargetCount; ++i)
    {
        const mrhiColorTarget* target = &pass->colorTargets[i];
        if (target->resource.index1 == 0)
        {
            // A hole takes a null view, which D3D12 writes nothing to.
            const D3D12_RENDER_TARGET_VIEW_DESC none = {
                .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
            };
            colors[i] = Take(&recorder->targets);
            ID3D12Device_CreateRenderTargetView(recorder->device, nullptr, &none, colors[i]);
            continue;
        }
        colors[i] = ColorView(recorder, target);
        if (target->load == mrhi_loadClear)
        {
            const FLOAT clear[4] = {target->clear.red, target->clear.green, target->clear.blue,
                                    target->clear.alpha};
            ID3D12GraphicsCommandList_ClearRenderTargetView(recorder->list, colors[i], clear, 0,
                                                            nullptr);
        }
    }
    bool depth = pass->depthTarget.resource.index1 != 0;
    D3D12_CPU_DESCRIPTOR_HANDLE depthView = {0};
    if (depth)
    {
        depthView = DepthView(recorder, &pass->depthTarget);
        ClearDepth(recorder, depthView);
    }
    ID3D12GraphicsCommandList_OMSetRenderTargets(recorder->list, pass->colorTargetCount, colors,
                                                 FALSE, depth ? &depthView : nullptr);
    ID3D12GraphicsCommandList1* views = nullptr;
    if (pass->viewCount > 1 &&
        SUCCEEDED(ID3D12GraphicsCommandList_QueryInterface(
            recorder->list, &IID_ID3D12GraphicsCommandList1, (void**)&views)))
    {
        // Every view of a multiview pass (mrhi-0020).
        ID3D12GraphicsCommandList1_SetViewInstanceMask(views, (1u << pass->viewCount) - 1);
        ID3D12GraphicsCommandList1_Release(views);
    }
    D3D12_VIEWPORT viewport = {0.0f, 0.0f, (FLOAT)pass->width, (FLOAT)pass->height, 0.0f, 1.0f};
    D3D12_RECT scissor = {0, 0, (LONG)pass->width, (LONG)pass->height};
    ID3D12GraphicsCommandList_RSSetViewports(recorder->list, 1, &viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(recorder->list, 1, &scissor);
}

static UINT SubresourceOf(const mrhiTextureDef* def, uint32_t mip, uint32_t layer)
{
    uint32_t layers = def->kind == mrhi_texture3d ? 1 : def->depthOrLayers;
    MRHI_ASSERT(layer < layers);
    return mip + layer * def->mipLevels;
}

// Resolves the pass's multisampled targets into their resolve targets.
static void Resolve(mrhiD3d12Recorder* recorder)
{
    const mrhiDriverPass* pass = recorder->pass;
    for (uint32_t i = 0; i < pass->colorTargetCount; ++i)
    {
        const mrhiColorTarget* target = &pass->colorTargets[i];
        if (target->resource.index1 == 0 || target->resolve.index1 == 0)
        {
            continue;
        }
        const mrhiD3d12Object* source = TargetOf(recorder, target->resource);
        const mrhiD3d12Object* into = TargetOf(recorder, target->resolve);
        // Each view's layer (mrhi-0020).
        for (uint32_t view = 0; view < pass->viewCount; ++view)
        {
            UINT from = SubresourceOf(source->texture, target->mip, target->layer + view);
            UINT to = SubresourceOf(into->texture, target->resolveMip, target->resolveLayer + view);
            mrhiD3d12Transition(recorder, source->resource, from,
                                D3D12_RESOURCE_STATE_RENDER_TARGET,
                                D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
            mrhiD3d12FlushBarriers(recorder);
            ID3D12GraphicsCommandList_ResolveSubresource(recorder->list, into->resource, to,
                                                         source->resource, from,
                                                         mrhiD3d12Format(target->viewFormat));
            mrhiD3d12Transition(recorder, source->resource, from,
                                D3D12_RESOURCE_STATE_RESOLVE_SOURCE,
                                D3D12_RESOURCE_STATE_RENDER_TARGET);
            mrhiD3d12FlushBarriers(recorder);
        }
    }
}

// Opens a debug group, or marks a point, named by a label of length
// bytes; PIX reads it as a narrow string.
static void Mark(const mrhiD3d12Recorder* recorder, const char* label, size_t length, bool group)
{
    char name[MRHI_LABEL_BYTES + 1];
    length = length < MRHI_LABEL_BYTES ? length : MRHI_LABEL_BYTES;
    memcpy(name, label, length);
    name[length] = '\0';
    if (group)
    {
        ID3D12GraphicsCommandList_BeginEvent(recorder->list, 1, name, (UINT)length + 1);
    }
    else
    {
        ID3D12GraphicsCommandList_SetMarker(recorder->list, 1, name, (UINT)length + 1);
    }
}

static void Label(mrhiD3d12Recorder* recorder, const mrhiCommand* command)
{
    if (command->type == mrhiCommandPopDebugGroup)
    {
        MRHI_ASSERT(recorder->labelCount > 0);
        --recorder->labelCount;
        ID3D12GraphicsCommandList_EndEvent(recorder->list);
        return;
    }
    bool group = command->type == mrhiCommandPushDebugGroup;
    Mark(recorder, (const char*)&command[1], (size_t)command->b, group);
    recorder->labelCount += group ? 1 : 0;
}

static void Record(mrhiD3d12Recorder* recorder, const mrhiCommand* command)
{
    switch (command->type)
    {
    case mrhiCommandGraphicsPipeline:
    case mrhiCommandComputePipeline:
        mrhiD3d12SetPipeline(recorder, command->b);
        break;
    case mrhiCommandRootBlock:
        mrhiD3d12SetRootBlock(recorder, command);
        break;
    case mrhiCommandViewport:
    case mrhiCommandScissor:
    case mrhiCommandBlendConstant:
    case mrhiCommandStencilReference:
        mrhiD3d12SetState(recorder, command);
        break;
    case mrhiCommandBindings:
        mrhiD3d12BindTable(recorder, command);
        break;
    case mrhiCommandVertexBuffer:
    case mrhiCommandIndexBuffer:
        mrhiD3d12BindBuffer(recorder, command);
        break;
    case mrhiCommandDraw:
    case mrhiCommandDrawIndexed:
    case mrhiCommandDispatch:
    case mrhiCommandDrawIndirect:
    case mrhiCommandDrawIndexedIndirect:
    case mrhiCommandDispatchIndirect:
    case mrhiCommandDrawIndirectCount:
    case mrhiCommandDrawIndexedIndirectCount:
        mrhiD3d12Draw(recorder, command);
        break;
    case mrhiCommandBeginOcclusionQuery:
    case mrhiCommandEndOcclusionQuery:
    case mrhiCommandBeginStatisticsQuery:
    case mrhiCommandEndStatisticsQuery:
    case mrhiCommandResolveQueries:
        mrhiD3d12Query(recorder, command);
        break;
    case mrhiCommandPushDebugGroup:
    case mrhiCommandPopDebugGroup:
    case mrhiCommandDebugMarker:
        Label(recorder, command);
        break;
    default:
        MRHI_ASSERT(command->type >= mrhiCommandCopyBuffer && command->type < mrhiCommandTypeEnd);
        mrhiD3d12Copy(recorder, command);
        break;
    }
}

// Forgets what the last pass set: each pass sets its own pipeline,
// tables and buffers. The root signatures stay set on the list.
static void Forget(mrhiD3d12Recorder* recorder)
{
    recorder->pipeline = nullptr;
    memset(recorder->boundCounts, 0, sizeof(recorder->boundCounts));
    memset(recorder->vertices, 0, sizeof(recorder->vertices));
    recorder->verticesChanged = false;
    recorder->indexObject = 0;
}

void mrhiD3d12RecordPass(mrhiD3d12Recorder* recorder, const mrhiDriverPass* pass)
{
    mrhiD3d12RecordBarriers(recorder, pass->id);
    mrhiD3d12UseDeclared(recorder, pass);
    recorder->pass = pass;
    recorder->labelCount = 0;
    Forget(recorder);
    mrhiD3d12EnterHeap(recorder, pass->heap);
    if (pass->labelLength > 0)
    {
        Mark(recorder, pass->label, pass->labelLength, true);
    }
    mrhiD3d12PassTimestamp(recorder, false);
    bool targets = pass->colorTargetCount > 0 || pass->depthTarget.resource.index1 != 0;
    if (pass->passClass != mrhi_passTransfer && targets)
    {
        StartTargets(recorder);
    }
    for (uint32_t chunk = pass->firstChunk; chunk != 0 && recorder->status == mrhi_success;
         chunk = recorder->frame->chunks[chunk - 1].next)
    {
        const mrhiCommandChunk* at = &recorder->frame->chunks[chunk - 1];
        for (uint32_t i = 0; i < at->count; i += 1u + at->commands[i].payload)
        {
            Record(recorder, &at->commands[i]);
        }
    }
    for (; recorder->labelCount > 0; --recorder->labelCount)
    {
        ID3D12GraphicsCommandList_EndEvent(recorder->list);
    }
    if (pass->passClass != mrhi_passTransfer && targets)
    {
        Resolve(recorder);
    }
    mrhiD3d12PassTimestamp(recorder, true);
    if (pass->labelLength > 0)
    {
        ID3D12GraphicsCommandList_EndEvent(recorder->list);
    }
}
