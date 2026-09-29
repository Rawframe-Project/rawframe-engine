// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Vulkan device's heaps (mrhi-0015): one descriptor set layout for the
// device at set 4, whose binding 0 holds the resource heap's entries
// (sampled images, or mutable descriptors that may also be storage
// images and buffers) and binding 1 the sampler heap's, both updated
// after bind and partially bound; each heap one set from a pool of its
// own, written as the core accepts entries.

#ifndef MAUL_RHI_SRC_VULKAN_HEAP_H
#define MAUL_RHI_SRC_VULKAN_HEAP_H

#include "driver.h"
#include "vulkan_object.h"

typedef struct mrhiVulkanHeap
{
    VkDescriptorPool pool;
    VkDescriptorSet set;
} mrhiVulkanHeap;

typedef struct mrhiVulkanHeaps
{
    const mrhiVulkanDevice* api;
    VkDevice device;
    const mrhiVulkanObjects* objects;
    // Every heap's layout, VK_NULL_HANDLE on a device without bindless
    // sampling; its bindings' counts, the device's heap limits; whether
    // binding 0 is mutable.
    VkDescriptorSetLayout layout;
    uint32_t entries;
    uint32_t samplers;
    bool heterogeneous;
    mrhiVulkanHeap* heaps;
    mrhiVulkanSlots heapSlots;
} mrhiVulkanHeaps;

// Makes the device's heap layout when it has bindless sampling:
// mrhi_success, or the error.
mrhiResult mrhiVulkanHeapsInit(mrhiVulkanHeaps* heaps);

// Destroys every heap left and the layout.
void mrhiVulkanHeapsEnd(mrhiVulkanHeaps* heaps);

// Makes a heap, all entries empty: its handle, never zero;
// mrhi_errorCapacity when the table or the pool's memory runs out, or
// mrhi_errorPlatform.
mrhiResult mrhiVulkanCreateHeap(mrhiVulkanHeaps* heaps, uint64_t* handleOut);

// Destroys a heap at once.
void mrhiVulkanDestroyHeap(mrhiVulkanHeaps* heaps, uint64_t handle);

// Writes a resource entry or a sampler entry the core has checked.
void mrhiVulkanWriteHeapEntry(const mrhiVulkanHeaps* heaps, uint64_t heap, uint32_t index,
                              const mrhiDriverHeapEntry* entry);
void mrhiVulkanWriteHeapSampler(const mrhiVulkanHeaps* heaps, uint64_t heap, uint32_t index,
                                uint64_t sampler);

// A heap's set.
VkDescriptorSet mrhiVulkanHeapSet(const mrhiVulkanHeaps* heaps, uint64_t handle);

#endif // MAUL_RHI_SRC_VULKAN_HEAP_H
