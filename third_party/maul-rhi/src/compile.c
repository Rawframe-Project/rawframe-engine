// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The frame's compile: passes are culled from the frame's outputs
// backwards, at the granularity of whole resources, the kept ones are
// planned, and the usages they make of each declared texture are
// checked against its format. Declaration order stays execution
// order. The frame's first pass is the library's own: it clears each
// buffer and texture no frame has run with yet, the first time a frame
// imports it, so that every backend's new object reads as zeros. A
// texture of one mip and layer whose first use clears it whole as a
// target is left out, since nothing could see its zeros.

#include "capabilities_core.h"
#include "device_core.h"
#include "encoder_core.h"
#include "invariant.h"
#include "label.h"

#include <string.h>

// The library's first pass's label, which debuggers show.
static const char s_zeroLabel[] = "maul-rhi: zero new objects";

void mrhiBeginZeroPass(mrhiDevice* device)
{
    device->framePasses[0] = (mrhiFramePass){
        .passClass = mrhi_passTransfer,
        .firstUse = device->deviceLimits.frameAccesses,
        .labelLength = MAUL_RHI_LABELS ? (uint32_t)(sizeof(s_zeroLabel) - 1) : 0,
    };
    if (MAUL_RHI_LABELS)
    {
        memcpy(device->frameLabels, s_zeroLabel, sizeof(s_zeroLabel) - 1);
    }
    device->framePassCount = MRHI_LIBRARY_PASSES;
}

void mrhiZeroOnFirstUse(mrhiDevice* device, uint32_t resource)
{
    mrhiFramePass* pass = &device->framePasses[0];
    // A frame imports each object once, so its fresh ones fit.
    MRHI_ASSERT(pass->useCount < device->deviceLimits.frameResources);
    const mrhiFrameResource* object = &device->frameResources[resource - 1];
    const mrhiTextureDef* def = &object->texture;
    bool texture = object->kind == mrhiImportedTexture;
    device->frameUses[pass->firstUse + pass->useCount++] = (mrhiFrameUse){
        .resource = resource,
        .use = mrhi_accessCopyDestination,
        .state = mrhi_stateCopyDestination,
        .planes = texture ? mrhiFormatPlanes(def->format) : 1,
        .writes = true,
        .mipCount = texture ? def->mipLevels : 1,
        .layerCount = texture && def->kind != mrhi_texture3d ? def->depthOrLayers : 1,
    };
}

// Whether a texture's first use in the frame overwrites all of it with a
// clear it keeps: one mip and layer, which a pass's color or depth
// target loads with a clear and stores, its stencil too.
static bool IsClearedFirst(const mrhiDevice* device, uint32_t resource)
{
    const mrhiTextureDef* def = &device->frameResources[resource - 1].texture;
    if (def->mipLevels != 1 || def->depthOrLayers != 1 || def->kind == mrhi_texture3d)
    {
        return false;
    }
    for (uint32_t p = MRHI_LIBRARY_PASSES; p < device->framePassCount; ++p)
    {
        const mrhiFramePass* pass = &device->framePasses[p];
        const mrhiFrameUse* uses = &device->frameUses[pass->firstUse];
        bool used = false;
        for (uint32_t i = 0; i < pass->useCount; ++i)
        {
            used = used || uses[i].resource == resource;
        }
        if (!used)
        {
            continue;
        }
        // A pass uses a target's one subresource in no other way, as its
        // uses' scope allows.
        for (uint32_t c = 0; c < pass->colorTargetCount; ++c)
        {
            const mrhiColorTarget* color = &pass->colorTargets[c];
            if (color->resource.index1 == resource)
            {
                return color->load == mrhi_loadClear && color->store == mrhi_storeKeep;
            }
        }
        const mrhiDepthTarget* depth = &pass->depthTarget;
        return depth->resource.index1 == resource && !depth->readOnly &&
               depth->depthLoad == mrhi_loadClear && depth->depthStore == mrhi_storeKeep &&
               (!mrhiFormatHasStencil(def->format) ||
                (depth->stencilLoad == mrhi_loadClear && depth->stencilStore == mrhi_storeKeep));
    }
    return false;
}

// Leaves out of the first pass the textures whose first use clears them
// whole anyway.
static void ElideZeroes(mrhiDevice* device)
{
    mrhiFramePass* pass = &device->framePasses[0];
    mrhiFrameUse* uses = &device->frameUses[pass->firstUse];
    uint32_t count = 0;
    for (uint32_t i = 0; i < pass->useCount; ++i)
    {
        uint32_t resource = uses[i].resource;
        if (device->frameResources[resource - 1].kind != mrhiImportedTexture ||
            !IsClearedFirst(device, resource))
        {
            uses[count++] = uses[i];
        }
    }
    pass->useCount = count;
}

// Records the first pass's clears once the compile kept it, from the
// start of the arena: success, or mrhi_errorCapacity when it is full.
// Then the program's passes may take their own frameCommandBytes past
// them.
static mrhiResult RecordZeroes(mrhiDevice* device)
{
    mrhiFramePass* pass = &device->framePasses[0];
    MRHI_ASSERT(pass->firstChunk == 0);
    for (uint32_t i = 0; pass->kept && i < pass->useCount; ++i)
    {
        uint32_t object = device->frameUses[pass->firstUse + i].resource;
        mrhiCommand* record = mrhiTakeCommands(device, pass, 1);
        if (record == nullptr)
        {
            return mrhi_errorCapacity;
        }
        bool texture = device->frameResources[object - 1].kind == mrhiImportedTexture;
        record[0] = (mrhiCommand){
            .type = texture ? mrhiCommandClearTexture : mrhiCommandClearBuffer,
            .a = object,
            .d = texture ? 0 : device->frameResources[object - 1].size,
        };
    }
    device->frameChunkLimit = device->frameProgramChunks +
                              atomic_load_explicit(&device->frameChunksTaken, memory_order_relaxed);
    return mrhi_success;
}

void mrhiMarkZeroed(mrhiDevice* device)
{
    for (uint32_t i = 0; i < device->frameResourceCount; ++i)
    {
        const mrhiFrameResource* resource = &device->frameResources[i];
        // An object destroyed while the frame was open may have given its
        // slot to a new one, which is still fresh.
        if (resource->kind == mrhiImportedTexture &&
            mrhiPoolIsLive(&device->textures, resource->index1, resource->generation))
        {
            // Zeroed, or cleared whole by its first use.
            device->textureSlots[resource->index1 - 1].fresh = false;
        }
        if (resource->kind != mrhiImportedBuffer ||
            !mrhiPoolIsLive(&device->buffers, resource->index1, resource->generation))
        {
            continue;
        }
        mrhiBufferSlot* slot = &device->bufferSlots[resource->index1 - 1];
        // A fresh buffer the frame imported was cleared in it.
        slot->fresh = false;
        slot->zeroState = resource->zeroState && resource->lastPass <= MRHI_LIBRARY_PASSES &&
                          resource->finalState == mrhi_stateCopyDestination;
    }
}

// Culls the passes from the last: a pass is kept when it never culls, or
// writes an imported resource or one a kept pass after it reads. A kept
// pass needs what it reads, and gives each resource its usages.
static void Cull(mrhiDevice* device)
{
    for (uint32_t i = 0; i < device->frameResourceCount; ++i)
    {
        device->frameResources[i].needed = false;
        device->frameResources[i].usage = 0;
    }
    for (uint32_t p = device->framePassCount; p-- > 0;)
    {
        mrhiFramePass* pass = &device->framePasses[p];
        const mrhiFrameUse* uses = &device->frameUses[pass->firstUse];
        bool kept = pass->neverCull;
        for (uint32_t i = 0; i < pass->useCount; ++i)
        {
            const mrhiFrameResource* resource = &device->frameResources[uses[i].resource - 1];
            kept = kept || (uses[i].writes && (mrhiOutlivesFrame(resource) || resource->needed));
        }
        pass->kept = kept;
        for (uint32_t i = 0; i < pass->useCount && kept; ++i)
        {
            mrhiFrameResource* resource = &device->frameResources[uses[i].resource - 1];
            resource->needed = resource->needed || uses[i].reads;
            resource->usage |= mrhiUsageOf(resource, uses[i].use);
        }
    }
}

mrhiResult mrhiCompile(mrhiDevice* device)
{
    ElideZeroes(device);
    Cull(device);
    mrhiResult status = mrhiPlan(device);
    if (status != mrhi_success)
    {
        return status;
    }
    for (uint32_t i = 0; i < device->frameResourceCount; ++i)
    {
        mrhiFrameResource* resource = &device->frameResources[i];
        if (resource->kind != mrhiFrameTexture || resource->usage == 0)
        {
            continue;
        }
        resource->usage |= resource->transient ? mrhi_textureTransient : 0u;
        mrhiTextureDef def = resource->texture;
        def.usage = resource->usage;
        status = mrhiCheckTextureUsage(device, &def);
        if (status != mrhi_success)
        {
            return status;
        }
    }
    status = mrhiPlace(device);
    if (status == mrhi_success)
    {
        status = mrhiPlanAliasing(device);
    }
    if (status != mrhi_success)
    {
        return status;
    }
    mrhiOrderPasses(device);
    mrhiDeriveStores(device);
    status = RecordZeroes(device);
    device->frameCompiled = status == mrhi_success;
    return status;
}

mrhiResult mrhiCompileFrame(mrhiDevice* device)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (device->allocatorGuard.busy)
    {
        return mrhiDeviceBusy(device);
    }
    if (!device->frameOpen || device->frameCompiled)
    {
        return mrhi_errorState;
    }
    return mrhiCompile(device);
}

mrhiResult mrhiIsPassKept(mrhiDevice* device, mrhiPassId pass, bool* keptOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (keptOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    if (!device->frameOpen || !device->frameCompiled)
    {
        return mrhi_errorState;
    }
    if (pass.generation != device->frameSerial || pass.index1 <= MRHI_LIBRARY_PASSES ||
        pass.index1 > device->framePassCount)
    {
        return mrhi_errorStale;
    }
    *keptOut = device->framePasses[pass.index1 - 1].kept;
    return mrhi_success;
}
