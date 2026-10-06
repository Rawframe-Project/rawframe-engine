// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Vulkan objects made outside the library (maul-rhi/vulkan.h, mrhi-0018): the
// functions an OpenXR program calls, answered by the Vulkan driver and
// unsupported on every other.

#include "device_core.h"
#include "encoder_core.h"
#include "instance_core.h"
#include "validation.h"

#include "maul-rhi/vulkan.h"

#include <string.h>

#ifdef MAUL_RHI_VULKAN_DRIVER
#include "driver_vulkan.h"
#include "vulkan_device.h"
#endif

mrhiResult mrhiDescribeVulkanDevice(mrhiInstance* instance, const mrhiDeviceDef* def,
                                    void** createInfoOut, void** physicalDeviceOut)
{
    if (instance == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (def == nullptr || createInfoOut == nullptr || physicalDeviceOut == nullptr)
    {
        return mrhiMisuse(instance, mrhi_diagnosticNullArgument);
    }
    *createInfoOut = nullptr;
    *physicalDeviceOut = nullptr;
#ifdef MAUL_RHI_VULKAN_DRIVER
    if (!mrhiIsVulkanDriver(mrhiInnerDriver(&instance->driver)))
    {
        return mrhi_errorUnsupported;
    }
    // Each description ends the one before, even one refused.
    mrhiForgetVulkanDevice(mrhiInnerDriver(&instance->driver));
    mrhiResult status = mrhi_success;
    const mrhiDriverAdapter* adapter = mrhiCheckDeviceDef(instance, def, &status);
    if (adapter == nullptr)
    {
        return status;
    }
    void* info = nullptr;
    status = mrhiDescribeVulkanDriverDevice(mrhiInnerDriver(&instance->driver), adapter->handle,
                                            def, &info);
    if (status == mrhi_success)
    {
        *createInfoOut = info;
        *physicalDeviceOut = mrhiVulkanPhysicalDevice(adapter->handle);
    }
    return status;
#else
    return mrhi_errorUnsupported;
#endif
}

mrhiResult mrhiGetVulkanPhysicalDevice(mrhiInstance* instance, mrhiAdapterId adapter,
                                       void** physicalDeviceOut)
{
    if (instance == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (physicalDeviceOut == nullptr)
    {
        return mrhiMisuse(instance, mrhi_diagnosticNullArgument);
    }
    *physicalDeviceOut = nullptr;
#ifdef MAUL_RHI_VULKAN_DRIVER
    if (!mrhiIsVulkanDriver(mrhiInnerDriver(&instance->driver)))
    {
        return mrhi_errorUnsupported;
    }
    const mrhiDriverAdapter* found = mrhiFindAdapter(instance, adapter);
    if (found == nullptr)
    {
        return mrhi_errorStale;
    }
    *physicalDeviceOut = mrhiVulkanPhysicalDevice(found->handle);
    return mrhi_success;
#else
    (void)adapter;
    return mrhi_errorUnsupported;
#endif
}

mrhiResult mrhiGetVulkanQueue(mrhiDevice* device, uint32_t* familyOut, uint32_t* indexOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (familyOut == nullptr || indexOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
#ifdef MAUL_RHI_VULKAN_DRIVER
    return mrhiVulkanDeviceQueue(mrhiInnerDevice(&device->driver), familyOut, indexOut)
               ? mrhi_success
               : mrhi_errorUnsupported;
#else
    return mrhi_errorUnsupported;
#endif
}

mrhiResult mrhiGetVulkanDevice(mrhiDevice* device, void** instanceOut, void** physicalDeviceOut,
                               void** deviceOut, void** getInstanceProcAddrOut,
                               void** getDeviceProcAddrOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (instanceOut == nullptr || physicalDeviceOut == nullptr || deviceOut == nullptr ||
        getInstanceProcAddrOut == nullptr || getDeviceProcAddrOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
#ifdef MAUL_RHI_VULKAN_DRIVER
    mrhiVulkanNative native;
    if (!mrhiVulkanDeviceNative(mrhiInnerDevice(&device->driver), &native))
    {
        return mrhi_errorUnsupported;
    }
    *instanceOut = (void*)native.instance;
    *physicalDeviceOut = (void*)native.physical;
    *deviceOut = (void*)native.device;
    // Function pointers read out as the program's untyped ones.
    static_assert(sizeof(void*) == sizeof(native.getInstanceProcAddr), "function pointers");
    memcpy((void*)getInstanceProcAddrOut, (const void*)&native.getInstanceProcAddr, sizeof(void*));
    memcpy((void*)getDeviceProcAddrOut, (const void*)&native.getDeviceProcAddr, sizeof(void*));
    return mrhi_success;
#else
    return mrhi_errorUnsupported;
#endif
}

mrhiResult mrhiGetVulkanTexture(mrhiDevice* device, mrhiTextureId texture,
                                mrhiVulkanTextureInfo* textureOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (textureOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
#ifdef MAUL_RHI_VULKAN_DRIVER
    if (device->adapterInfo.driver != mrhi_driverVulkan)
    {
        return mrhi_errorUnsupported;
    }
    if (!mrhiPoolIsLive(&device->textures, texture.index1, texture.generation))
    {
        return mrhi_errorStale;
    }
    const mrhiTextureSlot* slot = &device->textureSlots[texture.index1 - 1];
    return mrhiVulkanDeviceTexture(mrhiInnerDevice(&device->driver), slot->handle, &slot->def,
                                   textureOut)
               ? mrhi_success
               : mrhi_errorUnsupported;
#else
    (void)texture;
    return mrhi_errorUnsupported;
#endif
}

mrhiResult mrhiSetVulkanPassCommands(mrhiDevice* device, mrhiPassId pass, void* commandBuffer)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (device->adapterInfo.driver != mrhi_driverVulkan)
    {
        return mrhi_errorUnsupported;
    }
    return mrhiSetNativeCommands(device, pass, commandBuffer);
}
