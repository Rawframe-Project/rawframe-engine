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
#include "validation.h"

#include "maul-rhi/vulkan.h"

#ifdef MAUL_RHI_TEST_DRIVER
#include "driver_test.h"
#endif
#ifdef MAUL_RHI_D3D12_DRIVER
#include "driver_d3d12.h"
#endif
#ifdef MAUL_RHI_METAL_DRIVER
#include "driver_metal.h"
#endif
#ifdef MAUL_RHI_VULKAN_DRIVER
#include "driver_vulkan.h"
#endif
#ifdef MAUL_RHI_WEBGPU_DRIVER
#include "driver_webgpu.h"
#endif

#include <stdalign.h>

// The chained structs an instance def accepts.
static const mrhiStructType s_instanceStructs[] = {
    mrhi_structExternalDriver,
#ifdef MAUL_RHI_TEST_DRIVER
    mrhi_structTestDriver,
#endif
#ifdef MAUL_RHI_VULKAN_DRIVER
    mrhi_structInstanceVulkanAdopt,
    mrhi_structInstanceVulkanExtensions,
#endif
    mrhi_structNone,
};

// Whether the def's Vulkan structs are well formed (mrhi-0018): name lists,
// an adopted instance, and no extra extensions for an instance the
// program made.
static bool AreVulkanStructsValid(const mrhiInstanceDef* def)
{
    const mrhiInstanceVulkanAdopt* adopt =
        (const mrhiInstanceVulkanAdopt*)mrhiFindStruct(def->next, mrhi_structInstanceVulkanAdopt);
    const mrhiInstanceVulkanExtensions* extra = (const mrhiInstanceVulkanExtensions*)mrhiFindStruct(
        def->next, mrhi_structInstanceVulkanExtensions);
    bool adopted =
        adopt == nullptr || (adopt->instance != nullptr && adopt->getInstanceProcAddr != nullptr &&
                             mrhiIsNameList(adopt->extensions, adopt->extensionsLength));
    bool extended = extra == nullptr || mrhiIsNameList(extra->extensions, extra->extensionsLength);
    return adopted && extended && (adopt == nullptr || extra == nullptr);
}

// Whether the def asks for more than one driver: the test driver, an
// external one (mrhi-0024), or the Vulkan driver through its structs.
static bool MixesDrivers(const mrhiInstanceDef* def)
{
    bool vulkan = mrhiFindStruct(def->next, mrhi_structInstanceVulkanAdopt) != nullptr ||
                  mrhiFindStruct(def->next, mrhi_structInstanceVulkanExtensions) != nullptr;
    bool test = mrhiFindStruct(def->next, mrhi_structTestDriver) != nullptr;
    bool external = mrhiFindStruct(def->next, mrhi_structExternalDriver) != nullptr;
    return (vulkan && test) || (external && (vulkan || test));
}

static mrhiResult CheckDef(const mrhiInstanceDef* def)
{
    const mrhiInstanceLimits* limits = &def->limits;
    if (def->cookie != MRHI_INSTANCE_DEF_COOKIE || limits->chainDepth == 0 ||
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
    mrhiResult chain = mrhiCheckChain(def->next, s_instanceStructs, accepted, limits->chainDepth);
    if (chain != mrhi_success)
    {
        return chain;
    }
    if (!AreVulkanStructsValid(def))
    {
        return mrhi_errorInvalid;
    }
    return MixesDrivers(def) ? mrhi_errorUnsupported : mrhi_success;
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
    size_t diagnosticsAt = mrhiLayoutAdd(&layout, limits->diagnostics, sizeof(mrhiDiagnostic),
                                         alignof(mrhiDiagnostic));
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
    mrhiInitDiagnostics(&instance->diagnostics, (mrhiDiagnostic*)(block + diagnosticsAt),
                        limits->diagnostics);
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
        if (node->type == mrhi_structExternalDriver)
        {
            // Owned from here only when it passes the handshake.
            const mrhiExternalDriverDef* external = (const mrhiExternalDriverDef*)node;
            const mrhiInstanceDriverVtable* vtable = external->vtable;
            mrhiResult status = mrhiCheckInstanceVtable(vtable);
            if (status == mrhi_success)
            {
                instance->driver = (mrhiInstanceDriver){vtable, external->driver};
                instance->external = true;
            }
            return status;
        }
#ifdef MAUL_RHI_TEST_DRIVER
        if (node->type == mrhi_structTestDriver)
        {
            mrhiResult status =
                mrhiCreateTestDriver(&instance->allocator, (const mrhiTestDriverDef*)node,
                                     def->limits.notifications, &instance->driver);
            MRHI_ASSERT(status != mrhi_success ||
                        mrhiCheckInstanceVtable(instance->driver.vtable) == mrhi_success);
            return status;
        }
#endif
    }
#ifdef MAUL_RHI_D3D12_DRIVER
    return mrhiCreateD3d12Driver(&instance->allocator, def->limits.notifications,
                                 &instance->driver);
#elif defined(MAUL_RHI_METAL_DRIVER)
    return mrhiCreateMetalDriver(&instance->allocator, def->limits.notifications,
                                 &instance->driver);
#elif defined(MAUL_RHI_VULKAN_DRIVER)
    mrhiResult status =
        mrhiCreateVulkanDriver(&instance->allocator, def->next, def->limits.notifications,
                               def->limits.adapters, &instance->driver);
    MRHI_ASSERT(instance->driver.vtable == nullptr ||
                mrhiCheckInstanceVtable(instance->driver.vtable) == mrhi_success);
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
#ifdef MAUL_RHI_VALIDATION
    if (status == mrhi_success && instance->driver.vtable != nullptr)
    {
        status = mrhiWrapDriver(&instance->allocator, def->limits.notifications,
                                &instance->driverFaults, &instance->diagnostics, &instance->driver);
        // An external driver stays the program's when the instance fails.
        if (status != mrhi_success && instance->external)
        {
            instance->driver = (mrhiInstanceDriver){0};
        }
    }
#endif
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
        mrhiMisuse(instance, mrhi_diagnosticInstanceHasDevices);
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

mrhiResult mrhiMisuse(mrhiInstance* instance, mrhiDiagnosticCode code)
{
    ++instance->misuse;
    mrhiRecordDiagnostic(&instance->diagnostics, code);
    return mrhi_errorInvalid;
}

mrhiResult mrhiNextInstanceDiagnostic(mrhiInstance* instance, mrhiDiagnostic* diagnosticOut)
{
    if (instance == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (diagnosticOut == nullptr)
    {
        return mrhiMisuse(instance, mrhi_diagnosticNullArgument);
    }
    return mrhiTakeDiagnostic(&instance->diagnostics, diagnosticOut);
}

uint64_t mrhiGetInstanceMisuse(mrhiInstance* instance)
{
    return instance == nullptr ? 0 : instance->misuse;
}

uint64_t mrhiGetDriverFaults(const mrhiInstance* instance)
{
    return instance == nullptr
               ? 0
               : atomic_load_explicit(&instance->driverFaults, memory_order_relaxed);
}
