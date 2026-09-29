// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The frame's compile (mrhi-0008): passes are culled from the frame's
// outputs backwards, at the granularity of whole resources, the kept
// ones are planned, and the usages they make of each declared texture
// are checked against its format. Declaration order stays execution
// order.

#include "device_core.h"

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
    if (status != mrhi_success)
    {
        return status;
    }
    mrhiDeriveStores(device);
    device->frameCompiled = true;
    return mrhi_success;
}

mrhiResult mrhiCompileFrame(mrhiDevice* device)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
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
    *keptOut = device->framePasses[pass.index1 - 1].kept;
    return mrhi_success;
}
