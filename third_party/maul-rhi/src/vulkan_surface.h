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

// Fills what a surface can do on a physical device, given whether it
// offers VK_KHR_swapchain; nothing but presentable, false, when it
// cannot present there.
void mrhiVulkanSurfaceCaps(const mrhiVulkan* vulkan, VkPhysicalDevice device, bool swapchain,
                           VkSurfaceKHR surface, mrhiSurfaceCaps* capsOut);

// The Vulkan format and color space a surface lists for a color: the
// unorm format, or its sRGB twin when the surface lists only that; false
// when it lists neither.
bool mrhiVulkanSurfaceFormat(const mrhiVulkan* vulkan, VkPhysicalDevice device,
                             VkSurfaceKHR surface, mrhiSurfaceColor color,
                             VkSurfaceFormatKHR* formatOut);

#endif // MAUL_RHI_SRC_VULKAN_SURFACE_H
