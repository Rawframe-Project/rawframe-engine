// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Surfaces on Vulkan (mrhi-0003, mrhi-0007): the instance extensions
// they need, a surface made from the source a def chains, and what a
// surface can do on a physical device, as the contract's rows map it.

#ifndef MAUL_RHI_SRC_VULKAN_SURFACE_H
#define MAUL_RHI_SRC_VULKAN_SURFACE_H

#include "vulkan_api.h"

#include "maul-rhi/surface.h"

// The instance extensions surfaces use on this platform: VK_KHR_surface
// first, then VK_EXT_swapchain_colorspace and the platform's surface
// extensions. Returns how many there are, at most 32.
size_t mrhiVulkanSurfaceExtensions(const char* const** namesOut);

// Makes a surface from a source, given the extensions the instance
// enabled, one bit each in the order above: mrhi_success,
// mrhi_errorUnsupported for a source the instance cannot use, or
// mrhi_errorPlatform.
mrhiResult mrhiVulkanCreateSurface(const mrhiVulkan* vulkan, VkInstance instance, uint32_t enabled,
                                   const mrhiChain* source, VkSurfaceKHR* surfaceOut);

// What a surface lists on a physical device: its formats, its present
// modes, its composite alpha and image usage flags, and whether the
// device offers twin views.
typedef struct mrhiVulkanSurfaceFacts
{
    const VkSurfaceFormatKHR* formats;
    uint32_t formatCount;
    const VkPresentModeKHR* modes;
    uint32_t modeCount;
    VkCompositeAlphaFlagsKHR alpha;
    VkImageUsageFlags usages;
    bool mutableFormat;
} mrhiVulkanSurfaceFacts;

// Fills what a surface can do from what it lists: nothing but
// presentable, false, when a floor is missing (a color, FIFO, opaque
// alpha, render targets, a way to sRGB).
void mrhiVulkanCapsOf(const mrhiVulkanSurfaceFacts* facts, mrhiSurfaceCaps* capsOut);

// Fills what a surface can do on a physical device, given whether it
// offers VK_KHR_swapchain and VK_KHR_swapchain_mutable_format (twin
// views); nothing but presentable, false, when it cannot present there.
void mrhiVulkanSurfaceCaps(const mrhiVulkan* vulkan, VkPhysicalDevice device, bool swapchain,
                           bool mutableFormat, VkSurfaceKHR surface, mrhiSurfaceCaps* capsOut);

// Whether a surface's formats list the sRGB format of every 8-bit unorm
// color the caps report, in the same color space, and there is one: then
// any of them may be configured as sRGB images (twinImages).
bool mrhiVulkanTwinImages(const VkSurfaceFormatKHR* formats, uint32_t count,
                          const mrhiSurfaceCaps* caps);

// The Vulkan format and color space a surface lists for a color: for a
// unorm color the unorm format, or its sRGB twin when the surface lists
// only that; for an sRGB color (twin images) the sRGB format itself;
// false when it lists none.
bool mrhiVulkanSurfaceFormat(const mrhiVulkan* vulkan, VkPhysicalDevice device,
                             VkSurfaceKHR surface, mrhiSurfaceColor color,
                             VkSurfaceFormatKHR* formatOut);

// The same, from the formats a surface lists.
bool mrhiVulkanPickFormat(const VkSurfaceFormatKHR* formats, uint32_t count, mrhiSurfaceColor color,
                          VkSurfaceFormatKHR* formatOut);

#endif // MAUL_RHI_SRC_VULKAN_SURFACE_H
