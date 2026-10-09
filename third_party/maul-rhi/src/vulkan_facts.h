// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a Vulkan physical device reports, and what the contract makes of
// it (mrhi-0003): whether the device meets the driver's floor, and its
// features and limits as the contract's Vulkan rows map them. Nothing
// here calls Vulkan, so a test can give any device's facts.

#ifndef MAUL_RHI_SRC_VULKAN_FACTS_H
#define MAUL_RHI_SRC_VULKAN_FACTS_H

#include "driver.h"
#include "vulkan_api.h"

// The vertex buffers and attributes a pipeline has at most on this
// driver: the adapter's limits are reported no higher.
#define MRHI_VULKAN_VERTEX_BUFFERS    64
#define MRHI_VULKAN_VERTEX_ATTRIBUTES 64

// What a device reports, read once for a description.
typedef struct mrhiVulkanFacts
{
    VkPhysicalDeviceProperties2 properties;
    VkPhysicalDeviceVulkan11Properties properties11;
    VkPhysicalDeviceVulkan12Properties properties12;
    VkPhysicalDeviceVulkan13Properties properties13;
    VkPhysicalDeviceFeatures2 features;
    VkPhysicalDeviceVulkan11Features features11;
    VkPhysicalDeviceVulkan12Features features12;
    VkPhysicalDeviceVulkan13Features features13;
    // Read only when the device offers VK_EXT_mutable_descriptor_type.
    VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT mutableType;
    // The queue family with graphics and compute, or UINT32_MAX.
    uint32_t family;
    uint32_t familyTimestampBits;
} mrhiVulkanFacts;

// Whether a device meets the driver's floor beyond its version.
bool mrhiVulkanMeetsFloor(const mrhiVulkanFacts* facts);

// A device's features, with the two its formats tell: every float32
// format filtering, and B10G11R11 rendering and blending.
mrhiFeatures mrhiVulkanFeaturesOf(const mrhiVulkanFacts* facts, bool float32Filterable,
                                  bool rg11b10Renderable);

// A device's limits.
mrhiLimits mrhiVulkanLimitsOf(const mrhiVulkanFacts* facts);

// The limits of binding a device's Vulkan limits give, into limits whose
// tables and vertex buffers are already set: the contract's slots per
// table, the per-stage kinds fitted under maxPerStageResources less the
// color attachments (stage_limits.h), and tables plus vertex buffers at
// least the contract's value.
void mrhiVulkanBindingLimits(const VkPhysicalDeviceLimits* limits, mrhiLimits* granted);

#endif // MAUL_RHI_SRC_VULKAN_FACTS_H
