// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Vulkan physical devices as adapters (mrhi-0003): the floor a device must
// meet to be listed, and its facts, features, limits and format caps,
// as the contract's Vulkan rows map them.

#ifndef MAUL_RHI_SRC_VULKAN_ADAPTER_H
#define MAUL_RHI_SRC_VULKAN_ADAPTER_H

#include "allocator.h"
#include "driver.h"
#include "vulkan_api.h"

// The vertex buffers and attributes a pipeline has at most on this
// driver: the adapter's limits are reported no higher.
#define MRHI_VULKAN_VERTEX_BUFFERS    64
#define MRHI_VULKAN_VERTEX_ATTRIBUTES 64

// Describes a physical device as an adapter whose handle is the device:
// false, with nothing written, for a device below the floor. The
// allocator lends room to read its extensions.
bool mrhiDescribeVulkanAdapter(const mrhiVulkan* vulkan, const mrhiAllocator* allocator,
                               VkPhysicalDevice device, mrhiDriverAdapter* adapterOut);

// The depth and stencil format a device uses: the first of D24S8 and
// D32S8 it renders to.
VkFormat mrhiVulkanDepthStencil(const mrhiVulkan* vulkan, VkPhysicalDevice device);

// The Vulkan format of a format, given the device's depth and stencil
// format; VK_FORMAT_UNDEFINED for one the contract does not list.
VkFormat mrhiVulkanFormat(mrhiFormat format, VkFormat depthStencil);

// The queue family with graphics and compute a listed device has.
uint32_t mrhiVulkanQueueFamily(const mrhiVulkan* vulkan, VkPhysicalDevice device);

// Which of the named extensions the instance (device VK_NULL_HANDLE) or
// a physical device offers, one bit each in the names' order, at most
// 32; 0 when the list cannot be read.
uint32_t mrhiVulkanExtensions(const mrhiVulkan* vulkan, const mrhiAllocator* allocator,
                              VkPhysicalDevice device, const char* const* names, uint32_t count);

// Fills what a format can do on a device.
void mrhiGetVulkanFormatCaps(const mrhiVulkan* vulkan, VkPhysicalDevice device, mrhiFormat format,
                             mrhiFormatCaps* capsOut);

#endif // MAUL_RHI_SRC_VULKAN_ADAPTER_H
