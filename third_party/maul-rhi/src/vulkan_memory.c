// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Vulkan device's memory for its objects (vulkan_memory.h,
// mrhi-0003): device-local pools of 64 MiB blocks, or an eighth of the
// heap if smaller.

#include "vulkan_memory.h"

#include "invariant.h"

// The most bytes a block takes.
#define BLOCK_BYTES (UINT64_C(64) << 20)

void mrhiVulkanMemoryCounts(uint32_t objects, uint32_t* blocksOut, uint32_t* nodesOut)
{
    // A block but a pool's last holds an allocation; a block's free range
    // and each allocation's used and split ranges take a node each.
    *blocksOut = objects + 2 * VK_MAX_MEMORY_TYPES;
    *nodesOut = 3 * objects + *blocksOut;
}

void mrhiVulkanMemoryInit(mrhiVulkanMemory* memory, const mrhiVulkanDevice* api, VkDevice device,
                          const VkPhysicalDeviceMemoryProperties* properties, mrhiVulkanPool* pools,
                          mrhiVulkanBlock* blocks, uint32_t blockCapacity, mrhiTlsfNode* nodes,
                          uint32_t nodeCapacity)
{
    *memory = (mrhiVulkanMemory){
        .api = api,
        .device = device,
        .properties = *properties,
        .pools = pools,
        .blocks = blocks,
        .blockCapacity = blockCapacity,
    };
    for (uint32_t i = 0; i < 2 * VK_MAX_MEMORY_TYPES; ++i)
    {
        pools[i].blocks = 0;
        mrhiTlsfInit(&pools[i].ranges);
    }
    for (uint32_t i = 0; i < blockCapacity; ++i)
    {
        blocks[i] = (mrhiVulkanBlock){0};
    }
    mrhiTlsfNodesInit(&memory->nodes, nodes, nodeCapacity);
}

uint32_t mrhiVulkanMemoryType(const VkPhysicalDeviceMemoryProperties* properties, uint32_t allowed,
                              VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred,
                              VkMemoryPropertyFlags avoided)
{
    uint32_t fallback = UINT32_MAX;
    for (uint32_t i = 0; i < properties->memoryTypeCount; ++i)
    {
        VkMemoryPropertyFlags flags = properties->memoryTypes[i].propertyFlags;
        if ((allowed & (1u << i)) == 0 || (flags & required) != required)
        {
            continue;
        }
        if ((flags & preferred) == preferred && (flags & avoided) == 0)
        {
            return i;
        }
        fallback = fallback == UINT32_MAX ? i : fallback;
    }
    return fallback;
}

static mrhiResult StatusOf(VkResult result)
{
    return result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY
               ? mrhi_errorCapacity
               : mrhi_errorPlatform;
}

static VkDeviceSize BlockBytes(const mrhiVulkanMemory* memory, uint32_t type)
{
    const VkPhysicalDeviceMemoryProperties* properties = &memory->properties;
    VkDeviceSize eighth = properties->memoryHeaps[properties->memoryTypes[type].heapIndex].size / 8;
    return eighth < BLOCK_BYTES ? eighth : BLOCK_BYTES;
}

// Adds a block to a pool: mrhi_errorCapacity when memory runs out.
static mrhiResult AddBlock(mrhiVulkanMemory* memory, uint32_t pool, uint32_t type,
                           VkDeviceSize bytes)
{
    uint32_t slot = 0;
    while (slot < memory->blockCapacity && memory->blocks[slot].memory != VK_NULL_HANDLE)
    {
        ++slot;
    }
    MRHI_ASSERT(slot < memory->blockCapacity);
    const VkMemoryAllocateInfo info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = bytes,
        .memoryTypeIndex = type,
    };
    VkDeviceMemory block = VK_NULL_HANDLE;
    VkResult result = memory->api->vkAllocateMemory(memory->device, &info, nullptr, &block);
    if (result != VK_SUCCESS)
    {
        return StatusOf(result);
    }
    // The node store is sized for every block the table holds.
    if (mrhiTlsfAddBlock(&memory->pools[pool].ranges, &memory->nodes, slot, bytes) == 0)
    {
        memory->api->vkFreeMemory(memory->device, block, nullptr);
        return mrhi_errorCapacity;
    }
    memory->blocks[slot] = (mrhiVulkanBlock){.memory = block, .pool = pool};
    ++memory->pools[pool].blocks;
    return mrhi_success;
}

// Takes a range of a pool, adding a block when none fits.
static mrhiResult Suballocate(mrhiVulkanMemory* memory, uint32_t pool, uint32_t type,
                              const VkMemoryRequirements* needs,
                              mrhiVulkanAllocation* allocationOut)
{
    mrhiTlsf* ranges = &memory->pools[pool].ranges;
    uint32_t block = 0;
    VkDeviceSize offset = 0;
    uint32_t node =
        mrhiTlsfAllocate(ranges, &memory->nodes, needs->size, needs->alignment, &block, &offset);
    if (node == 0)
    {
        mrhiResult status = AddBlock(memory, pool, type, BlockBytes(memory, type));
        if (status != mrhi_success)
        {
            return status;
        }
        node = mrhiTlsfAllocate(ranges, &memory->nodes, needs->size, needs->alignment, &block,
                                &offset);
        MRHI_ASSERT(node != 0);
    }
    *allocationOut = (mrhiVulkanAllocation){
        .memory = memory->blocks[block].memory,
        .offset = offset,
        .node = node,
        .pool = pool,
    };
    return mrhi_success;
}

// Gives a buffer or image memory of its own.
static mrhiResult Dedicate(mrhiVulkanMemory* memory, VkBuffer buffer, VkImage image, uint32_t type,
                           VkDeviceSize bytes, mrhiVulkanAllocation* allocationOut)
{
    const VkMemoryDedicatedAllocateInfo dedicated = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .image = image,
        .buffer = buffer,
    };
    const VkMemoryAllocateInfo info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &dedicated,
        .allocationSize = bytes,
        .memoryTypeIndex = type,
    };
    *allocationOut = (mrhiVulkanAllocation){0};
    VkResult result =
        memory->api->vkAllocateMemory(memory->device, &info, nullptr, &allocationOut->memory);
    return result == VK_SUCCESS ? mrhi_success : StatusOf(result);
}

mrhiResult mrhiVulkanPlace(mrhiVulkanMemory* memory, VkBuffer buffer, VkImage image,
                           mrhiVulkanAllocation* allocationOut)
{
    VkMemoryDedicatedRequirements alone = {.sType =
                                               VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
    VkMemoryRequirements2 needs = {.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
                                   .pNext = &alone};
    if (buffer != VK_NULL_HANDLE)
    {
        const VkBufferMemoryRequirementsInfo2 info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2, .buffer = buffer};
        memory->api->vkGetBufferMemoryRequirements2(memory->device, &info, &needs);
    }
    else
    {
        const VkImageMemoryRequirementsInfo2 info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2, .image = image};
        memory->api->vkGetImageMemoryRequirements2(memory->device, &info, &needs);
    }
    const VkMemoryRequirements* size = &needs.memoryRequirements;
    uint32_t type = mrhiVulkanMemoryType(&memory->properties, size->memoryTypeBits,
                                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (type == UINT32_MAX)
    {
        return mrhi_errorPlatform;
    }
    uint32_t pool = 2 * type + (buffer != VK_NULL_HANDLE ? mrhiVulkanBuffers : mrhiVulkanTextures);
    bool apart = alone.prefersDedicatedAllocation || alone.requiresDedicatedAllocation ||
                 size->size > BlockBytes(memory, type) / 2;
    mrhiResult status = apart ? Dedicate(memory, buffer, image, type, size->size, allocationOut)
                              : Suballocate(memory, pool, type, size, allocationOut);
    if (status != mrhi_success)
    {
        return status;
    }
    allocationOut->pool = pool;
    VkResult result =
        buffer != VK_NULL_HANDLE
            ? memory->api->vkBindBufferMemory(memory->device, buffer, allocationOut->memory,
                                              allocationOut->offset)
            : memory->api->vkBindImageMemory(memory->device, image, allocationOut->memory,
                                             allocationOut->offset);
    if (result != VK_SUCCESS)
    {
        mrhiVulkanRelease(memory, allocationOut);
        return StatusOf(result);
    }
    return mrhi_success;
}

void mrhiVulkanRelease(mrhiVulkanMemory* memory, const mrhiVulkanAllocation* allocation)
{
    if (allocation->node == 0)
    {
        memory->api->vkFreeMemory(memory->device, allocation->memory, nullptr);
        return;
    }
    mrhiVulkanPool* pool = &memory->pools[allocation->pool];
    uint32_t node = mrhiTlsfFree(&pool->ranges, &memory->nodes, allocation->node);
    if (pool->blocks > 1 && mrhiTlsfIsBlockEmpty(&memory->nodes, node))
    {
        uint32_t block = memory->nodes.nodes[node - 1].block;
        mrhiTlsfRemoveBlock(&pool->ranges, &memory->nodes, node);
        memory->api->vkFreeMemory(memory->device, memory->blocks[block].memory, nullptr);
        memory->blocks[block] = (mrhiVulkanBlock){0};
        --pool->blocks;
    }
}

void mrhiVulkanMemoryDestroy(mrhiVulkanMemory* memory)
{
    for (uint32_t i = 0; i < memory->blockCapacity; ++i)
    {
        if (memory->blocks[i].memory != VK_NULL_HANDLE)
        {
            memory->api->vkFreeMemory(memory->device, memory->blocks[i].memory, nullptr);
            memory->blocks[i] = (mrhiVulkanBlock){0};
        }
    }
}
