// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Surfaces (mrhi-0007): instance ids made from exactly one chained
// native source, which the instance's driver turns into its surface,
// and what each adapter can do with them.

#include "chain.h"
#include "device_core.h"
#include "instance_core.h"
#include "invariant.h"
#include "label.h"

#define SURFACE_DEF_COOKIE 0x6D727366u

// The chained structs a surface def accepts: the sources.
static const mrhiStructType s_sources[] = {
    mrhi_structSurfaceSourceWin32,      mrhi_structSurfaceSourceWayland,
    mrhi_structSurfaceSourceXcb,        mrhi_structSurfaceSourceAndroid,
    mrhi_structSurfaceSourceMetalLayer, mrhi_structSurfaceSourceCanvas,
#ifdef MAUL_RHI_TEST_DRIVER
    mrhi_structSurfaceSourceTest,
#endif
};

#define SOURCE_COUNT (sizeof(s_sources) / sizeof(s_sources[0]))

mrhiSurfaceDef mrhiDefaultSurfaceDef(void)
{
    mrhiSurfaceDef def = {0};
    def.cookie = SURFACE_DEF_COOKIE;
    return def;
}

static bool IsSource(mrhiStructType type)
{
    for (size_t i = 0; i < SOURCE_COUNT; ++i)
    {
        if (s_sources[i] == type)
        {
            return true;
        }
    }
    return false;
}

// The one source on a checked chain, or NULL when there is none or
// more than one.
static const mrhiChain* FindSource(const mrhiChain* head)
{
    const mrhiChain* source = nullptr;
    for (const mrhiChain* node = head; node != nullptr; node = node->next)
    {
        if (IsSource(node->type))
        {
            if (source != nullptr)
            {
                return nullptr;
            }
            source = node;
        }
    }
    return source;
}

// Checks a def and returns its source; NULL with the refusal in
// statusOut.
static const mrhiChain* CheckDef(mrhiInstance* instance, const mrhiSurfaceDef* def,
                                 mrhiResult* statusOut)
{
    mrhiResult chain =
        mrhiCheckChain(def->next, s_sources, SOURCE_COUNT, instance->limits.chainDepth);
    if (def->cookie != SURFACE_DEF_COOKIE || chain == mrhi_errorInvalid ||
        !mrhiIsLabelValid(def->label, def->labelLength))
    {
        *statusOut = mrhiMisuse(instance);
        return nullptr;
    }
    if (chain != mrhi_success)
    {
        *statusOut = chain;
        return nullptr;
    }
    const mrhiChain* source = FindSource(def->next);
    if (source == nullptr)
    {
        *statusOut = mrhiMisuse(instance);
        return nullptr;
    }
    if (instance->driver.vtable == nullptr)
    {
        *statusOut = mrhi_errorUnsupported;
        return nullptr;
    }
    return source;
}

mrhiResult mrhiCreateSurface(mrhiInstance* instance, const mrhiSurfaceDef* def,
                             mrhiSurfaceId* surfaceOut)
{
    if (instance == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (def == nullptr || surfaceOut == nullptr)
    {
        return mrhiMisuse(instance);
    }
    mrhiResult status = mrhi_success;
    const mrhiChain* source = CheckDef(instance, def, &status);
    if (source == nullptr)
    {
        return status;
    }
    uint32_t index1 = 0;
    uint32_t generation = 0;
    if (!mrhiPoolAcquire(&instance->surfaces, &index1, &generation))
    {
        return mrhi_errorCapacity;
    }
    uint64_t handle = 0;
    status = instance->driver.vtable->createSurface(instance->driver.self, source, def, &handle);
    if (status != mrhi_success)
    {
        mrhiPoolRelease(&instance->surfaces, index1);
        return status;
    }
    instance->surfaceSlots[index1 - 1] = (mrhiSurfaceSlot){.handle = handle};
    *surfaceOut = (mrhiSurfaceId){index1, generation};
    return mrhi_success;
}

uint64_t mrhiFindSurface(const mrhiInstance* instance, mrhiSurfaceId surface)
{
    if (!mrhiPoolIsLive(&instance->surfaces, surface.index1, surface.generation))
    {
        return 0;
    }
    return instance->surfaceSlots[surface.index1 - 1].handle;
}

// Ends a live surface: its configuration, the driver's surface and the
// id.
static void EndSurface(mrhiInstance* instance, uint32_t index1)
{
    const mrhiSurfaceSlot* slot = &instance->surfaceSlots[index1 - 1];
    if (slot->device != nullptr)
    {
        mrhiEndConfiguration(slot->device, slot->swapchain);
    }
    instance->driver.vtable->destroySurface(instance->driver.self, slot->handle);
    mrhiPoolRelease(&instance->surfaces, index1);
}

mrhiResult mrhiDestroySurface(mrhiInstance* instance, mrhiSurfaceId surface)
{
    if (instance == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (mrhiFindSurface(instance, surface) == 0)
    {
        return mrhi_errorStale;
    }
    EndSurface(instance, surface.index1);
    return mrhi_success;
}

void mrhiDestroySurfaces(mrhiInstance* instance)
{
    for (uint32_t index1 = 1; index1 <= instance->surfaces.capacity; ++index1)
    {
        if (mrhiPoolIsLive(&instance->surfaces, index1, instance->surfaces.generations[index1 - 1]))
        {
            EndSurface(instance, index1);
        }
    }
}

mrhiResult mrhiGetSurfaceCaps(mrhiInstance* instance, mrhiSurfaceId surface, mrhiAdapterId adapter,
                              mrhiSurfaceCaps* capsOut)
{
    if (instance == nullptr || capsOut == nullptr)
    {
        return instance == nullptr ? mrhi_errorInvalid : mrhiMisuse(instance);
    }
    uint64_t handle = mrhiFindSurface(instance, surface);
    const mrhiDriverAdapter* found = mrhiFindAdapter(instance, adapter);
    if (handle == 0 || found == nullptr)
    {
        return mrhi_errorStale;
    }
    mrhiSurfaceCaps caps = {0};
    instance->driver.vtable->getSurfaceCaps(instance->driver.self, handle, found->handle, &caps);
    MRHI_ASSERT(!caps.presentable ||
                (caps.colorCount >= 1 && caps.colorCount <= MRHI_SURFACE_COLORS &&
                 (caps.presentModes & mrhi_presentFifo) != 0 &&
                 (caps.alphaModes & mrhi_alphaOpaque) != 0 &&
                 (caps.usages & mrhi_textureRenderTarget) != 0));
    // Nothing a driver fills for an adapter that cannot present is passed on.
    *capsOut = caps.presentable ? caps : (mrhiSurfaceCaps){0};
    return mrhi_success;
}
