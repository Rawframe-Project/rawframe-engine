// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The view of a submitted frame (mrhi-0013): the frame's resources, kept
// passes with the resources they declare, barriers, commands, uploads
// and readback ring, built in tables the device allocated for them, as
// its driver reads them.

#include "device_core.h"
#include "invariant.h"

#include <stdatomic.h>

// A frame resource as its driver sees it.
static mrhiDriverResource ViewResource(const mrhiFrameResource* resource)
{
    mrhiDriverResource view = {
        .needed = resource->firstPass != 0,
        .sealed = resource->initialState == mrhi_stateSealed,
        .handle = resource->handle,
        .size = resource->size,
        .usage = resource->usage,
        .memoryOffset = resource->memoryOffset,
        .memoryBytes = resource->memoryBytes,
    };
    switch (resource->kind)
    {
    case mrhiFrameTexture:
        view.kind = mrhiDriverTransientTexture;
        view.texture = &resource->texture;
        break;
    case mrhiFrameBuffer:
        view.kind = mrhiDriverTransientBuffer;
        break;
    case mrhiImportedTexture:
        view.kind = mrhiDriverDeviceTexture;
        view.texture = &resource->texture;
        break;
    case mrhiImportedBuffer:
        view.kind = mrhiDriverDeviceBuffer;
        break;
    case mrhiSurfaceImage:
        view.kind = mrhiDriverSurfaceImage;
        view.texture = &resource->texture;
        view.image = resource->image;
        break;
    }
    return view;
}

// A kept pass as its driver sees it, its declared resources copied from
// the next free entry of the accesses' table on.
static mrhiDriverPass ViewPass(const mrhiDevice* device, uint32_t index, uint32_t* accessAt)
{
    const mrhiFramePass* pass = &device->framePasses[index];
    mrhiDriverAccess* accesses = &device->driverAccesses[*accessAt];
    for (uint32_t i = 0; i < pass->useCount; ++i)
    {
        const mrhiFrameUse* use = &device->frameUses[pass->firstUse + i];
        accesses[i] = (mrhiDriverAccess){.resource = use->resource, .state = use->state};
    }
    *accessAt += pass->useCount;
    mrhiDriverPass view = {
        .id = {index + 1, device->frameSerial},
        .passClass = pass->passClass,
        .label = pass->labelLength > 0 ? &device->frameLabels[(size_t)index * MRHI_LABEL_BYTES]
                                       : nullptr,
        .labelLength = pass->labelLength,
        .colorTargetCount = pass->colorTargetCount,
        .depthTarget = pass->depthTarget,
        .depthStore = pass->depthStore,
        .stencilStore = pass->stencilStore,
        .width = pass->width,
        .height = pass->height,
        .occlusionSet = pass->occlusionHandle,
        .timestampSet = pass->timestampSet,
        .heap = pass->heap,
        .accesses = accesses,
        .accessCount = pass->useCount,
        .timestampBegin = pass->timestampBegin,
        .timestampEnd = pass->timestampEnd,
        .firstChunk = pass->firstChunk,
    };
    for (uint32_t i = 0; i < pass->colorTargetCount; ++i)
    {
        view.colorTargets[i] = pass->colorTargets[i];
        view.colorStores[i] = pass->colorStores[i];
    }
    return view;
}

void mrhiViewFrame(mrhiDevice* device, mrhiDriverFrame* frameOut)
{
    for (uint32_t i = 0; i < device->frameResourceCount; ++i)
    {
        device->driverResources[i] = ViewResource(&device->frameResources[i]);
    }
    uint32_t kept = 0;
    // Every pass's uses fit the table, which holds a frame's accesses.
    uint32_t accessAt = 0;
    for (uint32_t i = 0; i < device->framePassCount; ++i)
    {
        if (device->framePasses[i].kept)
        {
            device->driverPasses[kept++] = ViewPass(device, i, &accessAt);
        }
    }
    // A pass that found the arena or the staging full refused the frame
    // before this, so neither counter is past its room.
    uint64_t room = device->deviceLimits.frameUploadBytes;
    uint64_t staged = atomic_load_explicit(&device->stagingTaken, memory_order_relaxed);
    uint32_t chunks = atomic_load_explicit(&device->frameChunksTaken, memory_order_relaxed);
    MRHI_ASSERT(staged <= room && chunks <= device->frameChunkCount);
    *frameOut = (mrhiDriverFrame){
        .resources = device->driverResources,
        .resourceCount = device->frameResourceCount,
        .passes = device->driverPasses,
        .passCount = kept,
        .barriers = device->frameBarriers,
        .barrierCount = device->frameBarrierCount,
        .chunks = device->frameChunks,
        .chunkCount = chunks,
        .staging = device->frameStaging + (size_t)device->stagingRegion * room,
        .stagingBytes = staged,
        .readbackRing = device->readbackRing,
        .readbackBytes = device->deviceLimits.readbackBytes,
        .memoryBytes = device->frameMemory,
    };
}
