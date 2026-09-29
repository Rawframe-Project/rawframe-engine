// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The frame's memory and stores (mrhi-0008): declared resources are
// placed by lifetime, in first-use order, each at the lowest aligned
// offset clear of every placed resource whose kept passes it meets; and
// a target's store is kept only when something later reads it or the
// texture outlives the frame.

#include "device_core.h"

#include <stdckdint.h>

// Whether two resources' kept passes meet.
static bool Meets(const mrhiFrameResource* a, const mrhiFrameResource* b)
{
    return a->firstPass <= b->lastPass && b->firstPass <= a->lastPass;
}

// Rounds an offset up to an alignment, a power of two: false when it
// overflows.
static bool AlignUp(uint64_t offset, uint64_t alignment, uint64_t* out)
{
    uint64_t sum = 0;
    if (ckd_add(&sum, offset, alignment - 1))
    {
        return false;
    }
    *out = sum & ~(alignment - 1);
    return true;
}

// The bytes a declared resource takes with its derived usages, and their
// alignment.
static void Measure(const mrhiDevice* device, const mrhiFrameResource* resource, uint64_t* bytesOut,
                    uint64_t* alignmentOut)
{
    if (resource->kind == mrhiFrameTexture)
    {
        mrhiTextureDef def = resource->texture;
        def.usage = resource->usage;
        device->driver.vtable->textureMemory(device->driver.self, &def, bytesOut, alignmentOut);
        return;
    }
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = resource->size;
    def.usage = resource->usage;
    device->driver.vtable->bufferMemory(device->driver.self, &def, bytesOut, alignmentOut);
}

// Places one resource: the lowest aligned offset past the placed ones it
// meets, taken in offset order. False when the offsets overflow.
static bool PlaceOne(mrhiDevice* device, mrhiFrameResource* resource, uint64_t bytes,
                     uint64_t alignment)
{
    uint32_t* order = device->frameOrder;
    uint32_t count = 0;
    for (uint32_t i = 0; i < device->frameResourceCount; ++i)
    {
        const mrhiFrameResource* other = &device->frameResources[i];
        if (!other->placed || !Meets(resource, other))
        {
            continue;
        }
        // Insertion keeps them in offset order.
        uint32_t at = count++;
        while (at > 0 && device->frameResources[order[at - 1]].memoryOffset > other->memoryOffset)
        {
            order[at] = order[at - 1];
            --at;
        }
        order[at] = i;
    }
    uint64_t offset = 0;
    uint64_t end = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        const mrhiFrameResource* other = &device->frameResources[order[i]];
        if (ckd_add(&end, offset, bytes))
        {
            return false;
        }
        if (end <= other->memoryOffset)
        {
            break;
        }
        // What a new resource meets all meet each other, so they are apart
        // in memory and their ends rise with their offsets.
        if (!AlignUp(other->memoryOffset + other->memoryBytes, alignment, &offset))
        {
            return false;
        }
    }
    if (ckd_add(&end, offset, bytes))
    {
        return false;
    }
    resource->placed = true;
    resource->memoryOffset = offset;
    resource->memoryBytes = bytes;
    device->frameMemory = end > device->frameMemory ? end : device->frameMemory;
    return true;
}

mrhiResult mrhiPlace(mrhiDevice* device)
{
    device->frameMemory = 0;
    for (uint32_t i = 0; i < device->frameResourceCount; ++i)
    {
        mrhiFrameResource* resource = &device->frameResources[i];
        resource->placed = false;
        resource->memoryOffset = 0;
        resource->memoryBytes = 0;
    }
    for (uint32_t pass = 1; pass <= device->framePassCount; ++pass)
    {
        for (uint32_t i = 0; i < device->frameResourceCount; ++i)
        {
            mrhiFrameResource* resource = &device->frameResources[i];
            bool declared = resource->kind == mrhiFrameTexture || resource->kind == mrhiFrameBuffer;
            if (resource->firstPass != pass || !declared)
            {
                continue;
            }
            uint64_t bytes = 0;
            uint64_t alignment = 1;
            Measure(device, resource, &bytes, &alignment);
            if (bytes > 0 && !PlaceOne(device, resource, bytes, alignment))
            {
                return mrhi_errorCapacity;
            }
        }
    }
    return mrhi_success;
}

// Whether a kept pass after the one given reads a part of a resource.
static bool ReadLater(const mrhiDevice* device, uint32_t after, const mrhiFrameUse* part,
                      uint8_t planes)
{
    for (uint32_t p = after + 1; p < device->framePassCount; ++p)
    {
        const mrhiFramePass* pass = &device->framePasses[p];
        const mrhiFrameUse* uses = &device->frameUses[pass->firstUse];
        for (uint32_t i = 0; i < pass->useCount && pass->kept; ++i)
        {
            const mrhiFrameUse* use = &uses[i];
            bool meets = use->resource == part->resource && (use->planes & planes) != 0 &&
                         use->baseMip < part->baseMip + part->mipCount &&
                         part->baseMip < use->baseMip + use->mipCount &&
                         use->baseLayer < part->baseLayer + part->layerCount &&
                         part->baseLayer < use->baseLayer + use->layerCount;
            if (meets && use->reads)
            {
                return true;
            }
        }
    }
    return false;
}

// A target's store: its own, kept only when the texture outlives the
// frame or a later kept pass reads the part.
static mrhiStoreOp Store(const mrhiDevice* device, uint32_t pass, const mrhiFrameUse* use,
                         mrhiStoreOp asked, uint8_t planes)
{
    const mrhiFrameResource* resource = &device->frameResources[use->resource - 1];
    bool kept = mrhiOutlivesFrame(resource) || ReadLater(device, pass, use, planes);
    return asked == mrhi_storeKeep && kept ? mrhi_storeKeep : mrhi_storeDiscard;
}

void mrhiDeriveStores(mrhiDevice* device)
{
    for (uint32_t p = 0; p < device->framePassCount; ++p)
    {
        mrhiFramePass* pass = &device->framePasses[p];
        const mrhiFrameUse* uses = &device->frameUses[pass->firstUse];
        uint32_t color = 0;
        for (uint32_t i = 0; i < pass->useCount; ++i)
        {
            if (uses[i].use == mrhiUseColorTarget)
            {
                pass->colorStores[color] =
                    Store(device, p, &uses[i], pass->colorTargets[color].store, 1);
                ++color;
            }
            else if (uses[i].use == mrhiUseDepthTarget)
            {
                pass->depthStore = Store(device, p, &uses[i], pass->depthTarget.depthStore, 1);
                pass->stencilStore = Store(device, p, &uses[i], pass->depthTarget.stencilStore, 2);
            }
        }
    }
}

mrhiResult mrhiGetPassPlan(mrhiDevice* device, mrhiPassId pass, mrhiPassPlan* planOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (planOut == nullptr)
    {
        return mrhiDeviceMisuse(device);
    }
    if (!device->frameOpen || !device->frameCompiled)
    {
        return mrhi_errorState;
    }
    if (pass.generation != device->frameSerial || pass.index1 == 0 ||
        pass.index1 > device->framePassCount)
    {
        return mrhi_errorStale;
    }
    const mrhiFramePass* planned = &device->framePasses[pass.index1 - 1];
    *planOut = (mrhiPassPlan){
        .kept = planned->kept,
        .depthStore = planned->depthStore,
        .stencilStore = planned->stencilStore,
    };
    for (uint32_t i = 0; i < planned->colorTargetCount; ++i)
    {
        planOut->colorStores[i] = planned->colorStores[i];
    }
    return mrhi_success;
}

mrhiResult mrhiGetFrameMemory(mrhiDevice* device, uint64_t* bytesOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (bytesOut == nullptr)
    {
        return mrhiDeviceMisuse(device);
    }
    if (!device->frameOpen || !device->frameCompiled)
    {
        return mrhi_errorState;
    }
    *bytesOut = device->frameMemory;
    return mrhi_success;
}
