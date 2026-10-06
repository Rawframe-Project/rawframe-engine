// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Vulkan device (mrhi-0003): one queue with graphics and compute, a
// timeline semaphore whose values are the frames, the floor's features
// and the granted ones enabled.

#ifndef MAUL_RHI_SRC_VULKAN_DEVICE_H
#define MAUL_RHI_SRC_VULKAN_DEVICE_H

#include "driver.h"
#include "vulkan_api.h"

#include "maul-rhi/vulkan.h"

// Opens a device on a listed physical device with the def's features:
// mrhi_success with the device driver, mrhi_errorCapacity when memory
// runs out, or mrhi_errorPlatform when Vulkan refuses. The instance's
// functions must outlive the device.
mrhiResult mrhiCreateVulkanDevice(const mrhiAllocator* allocator, const mrhiVulkan* vulkan,
                                  VkPhysicalDevice physical, const mrhiDeviceDef* def,
                                  mrhiDeviceDriver* deviceOut);

// A device's native objects, borrowed (mrhi-0018).
typedef struct mrhiVulkanNative
{
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    PFN_vkGetInstanceProcAddr getInstanceProcAddr;
    PFN_vkGetDeviceProcAddr getDeviceProcAddr;
} mrhiVulkanNative;

// Reads a device's native objects: false for a device driver that is
// not Vulkan's.
bool mrhiVulkanDeviceNative(const mrhiDeviceDriver* driver, mrhiVulkanNative* nativeOut);

// Reads a device texture's image, its memory and the VkFormat, usage and
// create flags its def makes: false for a device driver that is not
// Vulkan's.
bool mrhiVulkanDeviceTexture(const mrhiDeviceDriver* driver, uint64_t handle,
                             const mrhiTextureDef* def, mrhiVulkanTextureInfo* textureOut);

// The queue family and index a device submits on (mrhi-0018): false for a
// device driver that is not Vulkan's.
bool mrhiVulkanDeviceQueue(const mrhiDeviceDriver* driver, uint32_t* familyOut, uint32_t* indexOut);

#endif // MAUL_RHI_SRC_VULKAN_DEVICE_H
