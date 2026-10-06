// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The VkDeviceCreateInfo a device def makes (mrhi-0003, mrhi-0018): one queue
// with graphics and compute, the floor's features and the granted ones,
// the swapchain extensions offered, mutable descriptors for
// heterogeneous heaps, and the def's own extensions
// (mrhiDeviceVulkanExtensions). The driver opens devices from it, and
// mrhiDescribeVulkanDevice gives it to a program whose OpenXR runtime
// makes the device.

#ifndef MAUL_RHI_SRC_VULKAN_RECIPE_H
#define MAUL_RHI_SRC_VULKAN_RECIPE_H

#include "vulkan_api.h"

#include "maul-rhi/vulkan.h"

// The features a device enables, chained.
typedef struct mrhiVulkanEnabled
{
    VkPhysicalDeviceFeatures2 features;
    VkPhysicalDeviceVulkan11Features features11;
    VkPhysicalDeviceVulkan12Features features12;
    VkPhysicalDeviceVulkan13Features features13;
    VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT mutableType;
} mrhiVulkanEnabled;

// A device's create info and what it holds. It points into itself, so it
// stays where it was composed; its extension names are allocated.
typedef struct mrhiVulkanRecipe
{
    VkDeviceCreateInfo info;
    VkDeviceQueueCreateInfo queue;
    float priority;
    mrhiVulkanEnabled enabled;
    // The extensions' names and their count, and the copy of the def's
    // own names they point into.
    const char** names;
    uint32_t nameCount;
    char* ownNames;
    size_t ownBytes;
    // Whether the device presents, and takes the twin views of its
    // swapchains' images.
    bool swapchain;
    bool mutableFormat;
} mrhiVulkanRecipe;

// The def's extra extensions, each ended by a NUL, from its
// mrhiDeviceVulkanExtensions: false for a malformed list (a name empty
// or not ended within the bytes); none when the chain has no such
// struct.
bool mrhiVulkanDefExtensions(const mrhiDeviceDef* def, const char** namesOut, size_t* bytesOut);

// Whether the def's features chain holds the core feature structs only.
bool mrhiVulkanDefFeaturesKnown(const mrhiDeviceDef* def);

// Composes the recipe for a def on a physical device: mrhi_success,
// mrhi_errorCapacity when memory runs out, mrhi_errorInvalid for a
// malformed extension list, or mrhi_errorUnsupported for a features
// chain with a struct other than the core feature structs.
mrhiResult mrhiVulkanComposeDevice(const mrhiVulkan* vulkan, const mrhiAllocator* allocator,
                                   VkPhysicalDevice physical, const mrhiDeviceDef* def,
                                   mrhiVulkanRecipe* recipeOut);

// Lets go of a recipe's allocations; it may be ended twice.
void mrhiVulkanEndRecipe(const mrhiAllocator* allocator, mrhiVulkanRecipe* recipe);

#endif // MAUL_RHI_SRC_VULKAN_RECIPE_H
