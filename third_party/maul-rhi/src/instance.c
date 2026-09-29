// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The instance: the contract version check, the def's extension chain,
// its driver, and one block from the def's allocator for the queue, the
// adapter table and the surface table.

#include "allocator.h"
#include "chain.h"
#include "instance_core.h"
#include "invariant.h"

#ifdef MAUL_RHI_TEST_DRIVER
#include "driver_test.h"
#endif
#ifdef MAUL_RHI_VULKAN_DRIVER
#include "driver_vulkan.h"
#endif
#ifdef MAUL_RHI_WEBGPU_DRIVER
#include "driver_webgpu.h"
#endif

#include <stdalign.h>

#define INSTANCE_DEF_COOKIE 0x6D72696Eu

// The chained structs an instance def accepts.
static const mrhiStructType s_instanceStructs[] = {
#ifdef MAUL_RHI_TEST_DRIVER
    mrhi_structTestDriver,
#endif
    mrhi_structNone,
};

mrhiInstanceDef mrhiDefaultInstanceDef(void)
{
    mrhiInstanceDef def = {0};
    def.cookie = INSTANCE_DEF_COOKIE;
    def.contractVersion = MRHI_CONTRACT_VERSION;
    def.limits.chainDepth = 8;
    def.limits.notifications = 64;
    def.limits.adapters = 16;
    def.limits.surfaces = 16;
    return def;
}

static mrhiResult CheckDef(const mrhiInstanceDef* def)
{
    const mrhiInstanceLimits* limits = &def->limits;
    if (def->cookie != INSTANCE_DEF_COOKIE || limits->chainDepth == 0 ||
        limits->notifications == 0 || limits->adapters == 0 || limits->surfaces == 0 ||
        !mrhiIsAllocatorValid(&def->allocator))
    {
        return mrhi_errorInvalid;
    }
    if (def->contractVersion != MRHI_CONTRACT_VERSION)
    {
        return mrhi_errorVersion;
    }
    size_t accepted = sizeof(s_instanceStructs) / sizeof(s_instanceStructs[0]) - 1;
    return mrhiCheckChain(def->next, s_instanceStructs, accepted, limits->chainDepth);
}

// The instance's block: the struct, then its arrays.
static mrhiInstance* Allocate(const mrhiInstanceDef* def)
{
    const mrhiInstanceLimits* limits = &def->limits;
    mrhiLayout layout = {.size = sizeof(mrhiInstance)};
    size_t queueAt = mrhiLayoutAdd(&layout, limits->notifications, sizeof(mrhiInstanceNotification),
                                   alignof(mrhiInstanceNotification));
    size_t pendingAt =
        mrhiLayoutAdd(&layout, limits->notifications, sizeof(mrhiPending), alignof(mrhiPending));
    size_t slotsAt =
        mrhiLayoutAdd(&layout, limits->adapters, sizeof(mrhiAdapterSlot), alignof(mrhiAdapterSlot));
    size_t listingAt =
        mrhiLayoutAdd(&layout, limits->adapters, sizeof(uint32_t), alignof(uint32_t));
    size_t foundAt = mrhiLayoutAdd(&layout, limits->adapters, sizeof(mrhiDriverAdapter),
                                   alignof(mrhiDriverAdapter));
    size_t generationsAt =
        mrhiLayoutAdd(&layout, limits->surfaces, sizeof(uint32_t), alignof(uint32_t));
    size_t nextFreeAt =
        mrhiLayoutAdd(&layout, limits->surfaces, sizeof(uint32_t), alignof(uint32_t));
    size_t surfaceSlotsAt =
        mrhiLayoutAdd(&layout, limits->surfaces, sizeof(mrhiSurfaceSlot), alignof(mrhiSurfaceSlot));
    unsigned char* block = layout.overflow
                               ? nullptr
                               : mrhiAllocate(&def->allocator, layout.size, alignof(mrhiInstance));
    if (block == nullptr)
    {
        return nullptr;
    }
    mrhiInstance* instance = (mrhiInstance*)block;
    *instance = (mrhiInstance){
        .allocator = def->allocator,
        .limits = def->limits,
        .bytes = layout.size,
        .queue = (mrhiInstanceNotification*)(block + queueAt),
        .pending = (mrhiPending*)(block + pendingAt),
        .slots = (mrhiAdapterSlot*)(block + slotsAt),
        .listing = (uint32_t*)(block + listingAt),
        .found = (mrhiDriverAdapter*)(block + foundAt),
        .surfaceSlots = (mrhiSurfaceSlot*)(block + surfaceSlotsAt),
    };
    mrhiPoolInit(&instance->surfaces, limits->surfaces, (uint32_t*)(block + generationsAt),
                 (uint32_t*)(block + nextFreeAt));
    for (uint32_t i = 0; i < limits->adapters; ++i)
    {
        instance->slots[i] = (mrhiAdapterSlot){.generation = 1};
    }
    return instance;
}

// Starts the driver the def's chain asks for, otherwise the build's
// native driver (mrhi-0003); no driver where there is neither.
static mrhiResult StartDriver(mrhiInstance* instance, const mrhiInstanceDef* def)
{
    for (const mrhiChain* node = def->next; node != nullptr; node = node->next)
    {
#ifdef MAUL_RHI_TEST_DRIVER
        if (node->type == mrhi_structTestDriver)
        {
            mrhiResult status =
                mrhiCreateTestDriver(&instance->allocator, (const mrhiTestDriverDef*)node,
                                     def->limits.notifications, &instance->driver);
            MRHI_ASSERT(status != mrhi_success || mrhiIsDriverVtableValid(instance->driver.vtable));
            return status;
        }
#endif
    }
#ifdef MAUL_RHI_VULKAN_DRIVER
    mrhiResult status = mrhiCreateVulkanDriver(&instance->allocator, def->limits.notifications,
                                               def->limits.adapters, &instance->driver);
    MRHI_ASSERT(instance->driver.vtable == nullptr ||
                mrhiIsDriverVtableValid(instance->driver.vtable));
    return status;
#elif defined(MAUL_RHI_WEBGPU_DRIVER)
    return mrhiCreateWebGpuDriver(&instance->allocator, def->limits.notifications,
                                  &instance->driver);
#else
    (void)instance;
    return mrhi_success;
#endif
}

mrhiResult mrhiCreateInstance(const mrhiInstanceDef* def, mrhiInstance** instanceOut)
{
    if (instanceOut == nullptr)
    {
        return mrhi_errorInvalid;
    }
    *instanceOut = nullptr;
    if (def == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = CheckDef(def);
    if (status != mrhi_success)
    {
        return status;
    }
    mrhiInstance* instance = Allocate(def);
    if (instance == nullptr)
    {
        return mrhi_errorCapacity;
    }
    status = StartDriver(instance, def);
    if (status != mrhi_success)
    {
        mrhiDestroyInstance(instance);
        return status;
    }
    *instanceOut = instance;
    return mrhi_success;
}

void mrhiDestroyInstance(mrhiInstance* instance)
{
    if (instance == nullptr)
    {
        return;
    }
    if (instance->deviceCount > 0)
    {
        mrhiMisuse(instance);
        return;
    }
    if (instance->driver.vtable != nullptr)
    {
        mrhiDestroySurfaces(instance);
        instance->driver.vtable->destroy(instance->driver.self);
    }
    mrhiAllocator allocator = instance->allocator;
    mrhiRelease(&allocator, instance, instance->bytes, alignof(mrhiInstance));
}

mrhiResult mrhiMisuse(mrhiInstance* instance)
{
    ++instance->misuse;
    return mrhi_errorInvalid;
}

uint64_t mrhiGetInstanceMisuse(mrhiInstance* instance)
{
    return instance == nullptr ? 0 : instance->misuse;
}
