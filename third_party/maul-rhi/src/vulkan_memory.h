// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Vulkan device's memory for its objects (mrhi-0003): pools per
// memory type and kind (buffers apart from textures, so that
// bufferImageGranularity never applies) suballocated with TLSF from
// blocks, and dedicated allocations for what the driver wants alone or
// is over half a block. Its bookkeeping lies in arrays the device sizes
// from its limits when it is made.

#ifndef MAUL_RHI_SRC_VULKAN_MEMORY_H
#define MAUL_RHI_SRC_VULKAN_MEMORY_H

#include "tlsf.h"
#include "vulkan_api.h"

#include "maul-rhi/base.h"

// What a pool holds.
typedef enum mrhiVulkanKind
{
    mrhiVulkanBuffers = 0,
    mrhiVulkanTextures = 1,
} mrhiVulkanKind;

// A pool: its free ranges and how many blocks it has.
typedef struct mrhiVulkanPool
{
    mrhiTlsf ranges;
    uint32_t blocks;
} mrhiVulkanPool;

// A block of device memory, or an unused entry (memory VK_NULL_HANDLE).
typedef struct mrhiVulkanBlock
{
    VkDeviceMemory memory;
    uint32_t pool;
} mrhiVulkanBlock;

// Where an object's memory lies; node 0 marks a dedicated allocation.
typedef struct mrhiVulkanAllocation
{
    VkDeviceMemory memory;
    VkDeviceSize offset;
    uint32_t node;
    uint32_t pool;
} mrhiVulkanAllocation;

typedef struct mrhiVulkanMemory
{
    const mrhiVulkanDevice* api;
    VkDevice device;
    VkPhysicalDeviceMemoryProperties properties;
    // Two per memory type, by mrhiVulkanKind.
    mrhiVulkanPool* pools;
    mrhiVulkanBlock* blocks;
    uint32_t blockCapacity;
    mrhiTlsfNodes nodes;
} mrhiVulkanMemory;

// The bookkeeping a device with room for objects buffers and textures
// needs: blocks, and TLSF nodes.
void mrhiVulkanMemoryCounts(uint32_t objects, uint32_t* blocksOut, uint32_t* nodesOut);

// Sets up a device's memory over its arrays: pools for 2 *
// VK_MAX_MEMORY_TYPES, blocks and nodes as mrhiVulkanMemoryCounts says.
void mrhiVulkanMemoryInit(mrhiVulkanMemory* memory, const mrhiVulkanDevice* api, VkDevice device,
                          const VkPhysicalDeviceMemoryProperties* properties, mrhiVulkanPool* pools,
                          mrhiVulkanBlock* blocks, uint32_t blockCapacity, mrhiTlsfNode* nodes,
                          uint32_t nodeCapacity);

// The memory type an allocation takes: the first of the allowed types
// with the required flags that has the preferred ones and lacks the
// avoided ones, else the first with the required flags; UINT32_MAX for
// none.
uint32_t mrhiVulkanMemoryType(const VkPhysicalDeviceMemoryProperties* properties, uint32_t allowed,
                              VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred,
                              VkMemoryPropertyFlags avoided);

// Places a buffer or an image (the other VK_NULL_HANDLE) in
// device-local memory and binds it: mrhi_success, mrhi_errorCapacity
// when memory runs out, or mrhi_errorPlatform.
mrhiResult mrhiVulkanPlace(mrhiVulkanMemory* memory, VkBuffer buffer, VkImage image,
                           mrhiVulkanAllocation* allocationOut);

// Gives an allocation back, freeing a block it leaves empty unless it is
// its pool's last.
void mrhiVulkanRelease(mrhiVulkanMemory* memory, const mrhiVulkanAllocation* allocation);

// Frees every block; the objects in them are gone.
void mrhiVulkanMemoryDestroy(mrhiVulkanMemory* memory);

#endif // MAUL_RHI_SRC_VULKAN_MEMORY_H
