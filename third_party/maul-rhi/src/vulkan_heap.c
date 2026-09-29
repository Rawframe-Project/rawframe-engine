// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Heaps on Vulkan (mrhi-0015): the device's set layout, a pool and a set
// per heap, and entries written as the core accepts them. Only a set's
// last binding may have a variable count, so every heap takes the
// device's counts; the core bounds the indices each heap takes. Clearing
// an entry writes nothing: a partially bound entry no shader reads may
// be stale.

#include "vulkan_heap.h"

#include "invariant.h"
#include "vulkan_pipeline.h"

// The stages that read heaps.
#define HEAP_STAGES                                                                                \
    (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)

// Entries written while other sets using the layout are bound or pending,
// and left empty.
#define HEAP_BINDING_FLAGS                                                                         \
    (VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |     \
     VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT)

// The types a mutable resource entry may be.
static const VkDescriptorType s_mutableTypes[] = {
    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
};

// The resource binding's type.
static VkDescriptorType ResourceType(const mrhiVulkanHeaps* heaps)
{
    return heaps->heterogeneous ? VK_DESCRIPTOR_TYPE_MUTABLE_EXT : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
}

mrhiResult mrhiVulkanHeapsInit(mrhiVulkanHeaps* heaps)
{
    heaps->layout = VK_NULL_HANDLE;
    if (heaps->entries == 0 && heaps->samplers == 0)
    {
        return mrhi_success;
    }
    // The resource binding's list of types; the sampler binding's is
    // empty.
    const VkMutableDescriptorTypeListEXT lists[2] = {{3, s_mutableTypes}, {0, nullptr}};
    const VkMutableDescriptorTypeCreateInfoEXT mutableInfo = {
        .sType = VK_STRUCTURE_TYPE_MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT,
        .mutableDescriptorTypeListCount = 2,
        .pMutableDescriptorTypeLists = lists,
    };
    const VkDescriptorBindingFlags flags[2] = {HEAP_BINDING_FLAGS, HEAP_BINDING_FLAGS};
    const VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlags = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .pNext = heaps->heterogeneous ? &mutableInfo : nullptr,
        .bindingCount = 2,
        .pBindingFlags = flags,
    };
    const VkDescriptorSetLayoutBinding bindings[2] = {
        {0, ResourceType(heaps), heaps->entries, HEAP_STAGES, nullptr},
        {1, VK_DESCRIPTOR_TYPE_SAMPLER, heaps->samplers, HEAP_STAGES, nullptr},
    };
    const VkDescriptorSetLayoutCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &bindingFlags,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = 2,
        .pBindings = bindings,
    };
    VkResult result =
        heaps->api->vkCreateDescriptorSetLayout(heaps->device, &info, nullptr, &heaps->layout);
    return result == VK_SUCCESS ? mrhi_success : mrhiVulkanStatus(result);
}

void mrhiVulkanHeapsEnd(mrhiVulkanHeaps* heaps)
{
    for (uint32_t i = 0; i < heaps->heapSlots.capacity; ++i)
    {
        heaps->api->vkDestroyDescriptorPool(heaps->device, heaps->heaps[i].pool, nullptr);
    }
    heaps->api->vkDestroyDescriptorSetLayout(heaps->device, heaps->layout, nullptr);
    heaps->layout = VK_NULL_HANDLE;
}

// Makes a heap's pool, room for one set of the layout, and the set.
static VkResult MakeSet(const mrhiVulkanHeaps* heaps, mrhiVulkanHeap* heap)
{
    // A pool takes at least one size, so the samplers always have one.
    VkDescriptorPoolSize sizes[2];
    uint32_t count = 0;
    if (heaps->entries > 0)
    {
        sizes[count++] = (VkDescriptorPoolSize){ResourceType(heaps), heaps->entries};
    }
    sizes[count++] = (VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_SAMPLER,
                                            heaps->samplers > 0 ? heaps->samplers : 1};
    const VkMutableDescriptorTypeListEXT lists[2] = {{3, s_mutableTypes}, {0, nullptr}};
    const VkMutableDescriptorTypeCreateInfoEXT mutableInfo = {
        .sType = VK_STRUCTURE_TYPE_MUTABLE_DESCRIPTOR_TYPE_CREATE_INFO_EXT,
        .mutableDescriptorTypeListCount = count,
        .pMutableDescriptorTypeLists = heaps->entries > 0 ? lists : lists + 1,
    };
    const VkDescriptorPoolCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = heaps->heterogeneous ? &mutableInfo : nullptr,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = count,
        .pPoolSizes = sizes,
    };
    VkResult result =
        heaps->api->vkCreateDescriptorPool(heaps->device, &info, nullptr, &heap->pool);
    if (result != VK_SUCCESS)
    {
        heap->pool = VK_NULL_HANDLE;
        return result;
    }
    const VkDescriptorSetAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = heap->pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &heaps->layout,
    };
    result = heaps->api->vkAllocateDescriptorSets(heaps->device, &allocate, &heap->set);
    if (result != VK_SUCCESS)
    {
        heaps->api->vkDestroyDescriptorPool(heaps->device, heap->pool, nullptr);
        heap->pool = VK_NULL_HANDLE;
    }
    return result;
}

mrhiResult mrhiVulkanCreateHeap(mrhiVulkanHeaps* heaps, uint64_t* handleOut)
{
    MRHI_ASSERT(heaps->layout != VK_NULL_HANDLE);
    uint32_t handle = mrhiVulkanTakeSlot(&heaps->heapSlots);
    if (handle == 0)
    {
        return mrhi_errorCapacity;
    }
    VkResult result = MakeSet(heaps, &heaps->heaps[handle - 1]);
    if (result != VK_SUCCESS)
    {
        mrhiVulkanGiveSlot(&heaps->heapSlots, handle);
        // A pool the device has no room for.
        bool full = result == VK_ERROR_FRAGMENTATION || result == VK_ERROR_OUT_OF_POOL_MEMORY;
        return full ? mrhi_errorCapacity : mrhiVulkanStatus(result);
    }
    *handleOut = handle;
    return mrhi_success;
}

void mrhiVulkanDestroyHeap(mrhiVulkanHeaps* heaps, uint64_t handle)
{
    mrhiVulkanHeap* heap = &heaps->heaps[handle - 1];
    heaps->api->vkDestroyDescriptorPool(heaps->device, heap->pool, nullptr);
    *heap = (mrhiVulkanHeap){0};
    mrhiVulkanGiveSlot(&heaps->heapSlots, handle);
}

void mrhiVulkanWriteHeapEntry(const mrhiVulkanHeaps* heaps, uint64_t heap, uint32_t index,
                              const mrhiDriverHeapEntry* entry)
{
    MRHI_ASSERT(index < heaps->entries);
    VkDescriptorImageInfo image = {0};
    VkDescriptorBufferInfo buffer = {0};
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = heaps->heaps[heap - 1].set,
        .dstBinding = 0,
        .dstArrayElement = index,
        .descriptorCount = 1,
    };
    switch (entry->kind)
    {
    case mrhi_heapSampledTexture:
        image = (VkDescriptorImageInfo){VK_NULL_HANDLE, heaps->objects->views[entry->handle - 1],
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.pImageInfo = &image;
        break;
    case mrhi_heapStorageTexture:
        image = (VkDescriptorImageInfo){VK_NULL_HANDLE, heaps->objects->views[entry->handle - 1],
                                        VK_IMAGE_LAYOUT_GENERAL};
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.pImageInfo = &image;
        break;
    default:
        buffer = (VkDescriptorBufferInfo){heaps->objects->buffers[entry->handle - 1].buffer,
                                          entry->offset, entry->size};
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &buffer;
        break;
    }
    heaps->api->vkUpdateDescriptorSets(heaps->device, 1, &write, 0, nullptr);
}

void mrhiVulkanWriteHeapSampler(const mrhiVulkanHeaps* heaps, uint64_t heap, uint32_t index,
                                uint64_t sampler)
{
    MRHI_ASSERT(index < heaps->samplers);
    const VkDescriptorImageInfo image = {heaps->objects->samplers[sampler - 1], VK_NULL_HANDLE,
                                         VK_IMAGE_LAYOUT_UNDEFINED};
    const VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = heaps->heaps[heap - 1].set,
        .dstBinding = 1,
        .dstArrayElement = index,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
        .pImageInfo = &image,
    };
    heaps->api->vkUpdateDescriptorSets(heaps->device, 1, &write, 0, nullptr);
}

VkDescriptorSet mrhiVulkanHeapSet(const mrhiVulkanHeaps* heaps, uint64_t handle)
{
    return heaps->heaps[handle - 1].set;
}
