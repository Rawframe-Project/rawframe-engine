// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Vulkan device's frames (vulkan_frame.h). Frames run in submission
// order on one queue, each signalling the timeline semaphore with its
// serial, so a poll reads one counter to know every frame finished, and
// a slot is reused only once the core has been told its frame finished.

#include "vulkan_frame.h"

#include "invariant.h"
#include "vulkan_resource.h"

#include <string.h>

// Makes a buffer in mapped host-visible memory with the required
// flags, preferring the preferred ones: the result, and the flags it
// got.
static VkResult MakeMapped(mrhiVulkanFrames* frames, VkDeviceSize size, VkBufferUsageFlags usage,
                           VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred,
                           VkBuffer* bufferOut, VkDeviceMemory* memoryOut, uint8_t** bytesOut,
                           VkMemoryPropertyFlags* flagsOut)
{
    const VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkResult result = frames->api->vkCreateBuffer(frames->device, &info, nullptr, bufferOut);
    if (result != VK_SUCCESS)
    {
        return result;
    }
    VkMemoryRequirements2 needs = {.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    const VkBufferMemoryRequirementsInfo2 query = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2, .buffer = *bufferOut};
    frames->api->vkGetBufferMemoryRequirements2(frames->device, &query, &needs);
    uint32_t type = mrhiVulkanMemoryType(frames->memory, needs.memoryRequirements.memoryTypeBits,
                                         required, preferred, 0);
    if (type == UINT32_MAX)
    {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    *flagsOut = frames->memory->memoryTypes[type].propertyFlags;
    const VkMemoryAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = needs.memoryRequirements.size,
        .memoryTypeIndex = type,
    };
    result = frames->api->vkAllocateMemory(frames->device, &allocate, nullptr, memoryOut);
    if (result == VK_SUCCESS)
    {
        result = frames->api->vkBindBufferMemory(frames->device, *bufferOut, *memoryOut, 0);
    }
    void* mapped = nullptr;
    if (result == VK_SUCCESS)
    {
        result = frames->api->vkMapMemory(frames->device, *memoryOut, 0, VK_WHOLE_SIZE, 0, &mapped);
    }
    *bytesOut = mapped;
    return result;
}

static VkResult MakeSlot(mrhiVulkanFrames* frames, mrhiVulkanSlot* slot, uint32_t family,
                         const mrhiDeviceLimits* limits)
{
    const VkCommandPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = family,
    };
    VkResult result = frames->api->vkCreateCommandPool(frames->device, &pool, nullptr, &slot->pool);
    if (result != VK_SUCCESS)
    {
        return result;
    }
    const VkCommandBufferAllocateInfo buffer = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = slot->pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    result = frames->api->vkAllocateCommandBuffers(frames->device, &buffer, &slot->commands);
    if (result != VK_SUCCESS || limits->frameUploadBytes == 0)
    {
        return result;
    }
    VkMemoryPropertyFlags flags = 0;
    return MakeMapped(frames, limits->frameUploadBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0,
                      &slot->staging, &slot->stagingMemory, &slot->stagingBytes, &flags);
}

mrhiResult mrhiVulkanFramesInit(mrhiVulkanFrames* frames, uint32_t family,
                                const mrhiDeviceLimits* limits)
{
    VkResult result = VK_SUCCESS;
    for (uint32_t i = 0; i < frames->slotCount && result == VK_SUCCESS; ++i)
    {
        result = MakeSlot(frames, &frames->slots[i], family, limits);
    }
    if (result == VK_SUCCESS && limits->readbackBytes > 0)
    {
        VkMemoryPropertyFlags flags = 0;
        result =
            MakeMapped(frames, limits->readbackBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                       &frames->readback, &frames->readbackMemory, &frames->readbackBytes, &flags);
        frames->readbackSize = limits->readbackBytes;
        frames->readbackCoherent = (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    }
    return result == VK_SUCCESS ? mrhi_success : mrhiVulkanStatus(result);
}

// Destroys a slot's transients and views, and resets its descriptor
// pools.
static void DropTransients(mrhiVulkanFrames* frames, mrhiVulkanSlot* slot)
{
    for (uint32_t i = 0; i < slot->transients; ++i)
    {
        frames->api->vkDestroyImage(frames->device, slot->images[i], nullptr);
        frames->api->vkDestroyBuffer(frames->device, slot->buffers[i], nullptr);
        frames->api->vkFreeMemory(frames->device, slot->own[i], nullptr);
        slot->images[i] = VK_NULL_HANDLE;
        slot->buffers[i] = VK_NULL_HANDLE;
        slot->own[i] = VK_NULL_HANDLE;
    }
    slot->transients = 0;
    for (uint32_t i = 0; i < slot->viewCount; ++i)
    {
        frames->api->vkDestroyImageView(frames->device, slot->views[i], nullptr);
    }
    slot->viewCount = 0;
    for (uint32_t i = 0; i < slot->poolCount; ++i)
    {
        (void)frames->api->vkResetDescriptorPool(frames->device, slot->pools[i], 0);
    }
    slot->poolInUse = 0;
}

// Destroys a retired object for good.
static void Retire(mrhiVulkanFrames* frames, const mrhiVulkanRetire* retired)
{
    switch (retired->kind)
    {
    case mrhiVulkanRetiredBuffer:
        mrhiVulkanDestroyBuffer(frames->objects, retired->handle);
        break;
    case mrhiVulkanRetiredTexture:
        mrhiVulkanDestroyTexture(frames->objects, retired->handle);
        break;
    case mrhiVulkanRetiredView:
        mrhiVulkanDestroyView(frames->objects, retired->handle);
        break;
    case mrhiVulkanRetiredSampler:
        mrhiVulkanDestroySampler(frames->objects, retired->handle);
        break;
    case mrhiVulkanRetiredQuerySet:
        mrhiVulkanDestroyQuerySet(frames->objects, retired->handle);
        break;
    case mrhiVulkanRetiredHeap:
        mrhiVulkanDestroyHeap(frames->heaps, retired->handle);
        break;
    default:
        mrhiVulkanDestroyPipeline(frames->pipelines, retired->handle);
        break;
    }
}

// Retires the objects whose frame has finished: all of them with no
// frame left to wait for.
static void RetireUpTo(mrhiVulkanFrames* frames, uint64_t serial)
{
    while (frames->retireCount > 0 && frames->retire[frames->retireFirst].serial <= serial)
    {
        Retire(frames, &frames->retire[frames->retireFirst]);
        frames->retireFirst = (frames->retireFirst + 1) % frames->retireCapacity;
        --frames->retireCount;
    }
}

void mrhiVulkanRetireLater(mrhiVulkanFrames* frames, mrhiVulkanRetired kind, uint64_t handle)
{
    // Every handle waits at most once, so the queue never fills.
    MRHI_ASSERT(frames->retireCount < frames->retireCapacity);
    uint32_t at = (frames->retireFirst + frames->retireCount) % frames->retireCapacity;
    frames->retire[at] =
        (mrhiVulkanRetire){.serial = frames->submitted + 1, .handle = handle, .kind = kind};
    ++frames->retireCount;
}

void mrhiVulkanFramesDestroy(mrhiVulkanFrames* frames)
{
    RetireUpTo(frames, UINT64_MAX);
    for (uint32_t i = 0; i < frames->slotCount; ++i)
    {
        mrhiVulkanSlot* slot = &frames->slots[i];
        DropTransients(frames, slot);
        frames->api->vkFreeMemory(frames->device, slot->memory, nullptr);
        frames->api->vkDestroyBuffer(frames->device, slot->staging, nullptr);
        frames->api->vkFreeMemory(frames->device, slot->stagingMemory, nullptr);
        frames->api->vkDestroyCommandPool(frames->device, slot->pool, nullptr);
        for (uint32_t p = 0; p < slot->poolCount; ++p)
        {
            frames->api->vkDestroyDescriptorPool(frames->device, slot->pools[p], nullptr);
        }
    }
    frames->api->vkDestroyBuffer(frames->device, frames->readback, nullptr);
    frames->api->vkFreeMemory(frames->device, frames->readbackMemory, nullptr);
}

// Makes a transient's object and reports its memory requirements.
static VkResult MakeTransient(mrhiVulkanFrames* frames, mrhiVulkanSlot* slot, uint32_t index,
                              const mrhiDriverResource* resource, VkMemoryRequirements* needsOut)
{
    VkMemoryRequirements2 needs = {.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    VkResult result = VK_SUCCESS;
    if (resource->kind == mrhiDriverTransientBuffer)
    {
        const mrhiBufferDef def = {.size = resource->size, .usage = resource->usage};
        const VkBufferCreateInfo info = mrhiVulkanBufferOf(&def);
        result = frames->api->vkCreateBuffer(frames->device, &info, nullptr, &slot->buffers[index]);
        const VkBufferMemoryRequirementsInfo2 query = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
            .buffer = slot->buffers[index]};
        if (result == VK_SUCCESS)
        {
            frames->api->vkGetBufferMemoryRequirements2(frames->device, &query, &needs);
        }
    }
    else
    {
        mrhiTextureDef def = *resource->texture;
        def.usage = resource->usage;
        mrhiVulkanImage image;
        mrhiVulkanImageOf(&def, frames->depthStencil, &image);
        result =
            frames->api->vkCreateImage(frames->device, &image.info, nullptr, &slot->images[index]);
        const VkImageMemoryRequirementsInfo2 query = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
            .image = slot->images[index]};
        if (result == VK_SUCCESS)
        {
            frames->api->vkGetImageMemoryRequirements2(frames->device, &query, &needs);
        }
    }
    *needsOut = needs.memoryRequirements;
    return result;
}

static VkResult Bind(mrhiVulkanFrames* frames, mrhiVulkanSlot* slot, uint32_t index,
                     VkDeviceMemory memory, VkDeviceSize offset)
{
    return slot->buffers[index] != VK_NULL_HANDLE
               ? frames->api->vkBindBufferMemory(frames->device, slot->buffers[index], memory,
                                                 offset)
               : frames->api->vkBindImageMemory(frames->device, slot->images[index], memory,
                                                offset);
}

// Gives a target the core placed nowhere memory of its own, lazily
// allocated as a tile GPU keeps it on chip.
static VkResult OnChip(mrhiVulkanFrames* frames, mrhiVulkanSlot* slot, uint32_t index,
                       const VkMemoryRequirements* needs)
{
    uint32_t type = mrhiVulkanMemoryType(frames->memory, needs->memoryTypeBits,
                                         VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, 0, 0);
    const VkMemoryAllocateInfo info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = needs->size,
        .memoryTypeIndex = type,
    };
    VkResult result =
        type == UINT32_MAX
            ? VK_ERROR_FEATURE_NOT_PRESENT
            : frames->api->vkAllocateMemory(frames->device, &info, nullptr, &slot->own[index]);
    return result == VK_SUCCESS ? Bind(frames, slot, index, slot->own[index], 0) : result;
}

// Makes the slot's block hold a frame's placed transients: kept when
// its type suits them all and it is large enough.
static VkResult Reserve(mrhiVulkanFrames* frames, mrhiVulkanSlot* slot, uint32_t types,
                        VkDeviceSize bytes)
{
    uint32_t type = mrhiVulkanMemoryType(frames->memory, types, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                         0, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (type == UINT32_MAX)
    {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (slot->memory != VK_NULL_HANDLE && slot->memoryType == type && slot->memoryBytes >= bytes)
    {
        return VK_SUCCESS;
    }
    frames->api->vkFreeMemory(frames->device, slot->memory, nullptr);
    slot->memory = VK_NULL_HANDLE;
    slot->memoryBytes = 0;
    const VkMemoryAllocateInfo info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = bytes,
        .memoryTypeIndex = type,
    };
    VkResult result = frames->api->vkAllocateMemory(frames->device, &info, nullptr, &slot->memory);
    if (result == VK_SUCCESS)
    {
        slot->memoryBytes = bytes;
        slot->memoryType = type;
    }
    return result;
}

// Makes a frame's transients in the slot and binds them where the core
// placed them.
static VkResult MakeTransients(mrhiVulkanFrames* frames, mrhiVulkanSlot* slot,
                               const mrhiDriverFrame* frame)
{
    slot->transients = frame->resourceCount;
    uint32_t types = UINT32_MAX;
    VkResult result = VK_SUCCESS;
    for (uint32_t i = 0; i < frame->resourceCount && result == VK_SUCCESS; ++i)
    {
        const mrhiDriverResource* resource = &frame->resources[i];
        bool transient = resource->kind == mrhiDriverTransientTexture ||
                         resource->kind == mrhiDriverTransientBuffer;
        if (!transient || !resource->needed)
        {
            continue;
        }
        VkMemoryRequirements needs;
        result = MakeTransient(frames, slot, i, resource, &needs);
        if (result == VK_SUCCESS && resource->memoryBytes == 0)
        {
            result = OnChip(frames, slot, i, &needs);
        }
        types &= resource->memoryBytes > 0 ? needs.memoryTypeBits : UINT32_MAX;
    }
    if (result != VK_SUCCESS || frame->memoryBytes == 0)
    {
        return result;
    }
    result = Reserve(frames, slot, types, frame->memoryBytes);
    for (uint32_t i = 0; i < frame->resourceCount && result == VK_SUCCESS; ++i)
    {
        const mrhiDriverResource* resource = &frame->resources[i];
        bool placed = (slot->buffers[i] != VK_NULL_HANDLE || slot->images[i] != VK_NULL_HANDLE) &&
                      resource->memoryBytes > 0;
        result = placed ? Bind(frames, slot, i, slot->memory, resource->memoryOffset) : result;
    }
    return result;
}

// Records a frame into its slot and submits it, signalling its serial.
static VkResult Run(mrhiVulkanFrames* frames, mrhiVulkanSlot* slot, const mrhiDriverFrame* frame,
                    mrhiResult* statusOut)
{
    VkResult result = frames->api->vkResetCommandPool(frames->device, slot->pool, 0);
    const VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    if (result == VK_SUCCESS)
    {
        result = frames->api->vkBeginCommandBuffer(slot->commands, &begin);
    }
    *statusOut = result == VK_SUCCESS ? mrhiVulkanRecord(frames, slot, frame) : mrhi_success;
    if (result == VK_SUCCESS)
    {
        result = frames->api->vkEndCommandBuffer(slot->commands);
    }
    if (result != VK_SUCCESS || *statusOut != mrhi_success)
    {
        return result;
    }
    const VkCommandBufferSubmitInfo commands = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = slot->commands,
    };
    // The frame waits for its surface images' acquires and signals their
    // presents beside its serial. It also waits for the frame that used
    // its slot before it: the host saw that frame finish before reusing
    // the slot's memory, so the wait costs the GPU nothing, but it puts
    // the reuse in the queue's own order, where synchronization
    // validation sees it.
    mrhiVulkanSwapchains* swapchains = frames->swapchains;
    uint32_t images = mrhiVulkanPresentSemaphores(swapchains, frame, frames->submitted + 1);
    bool reused = frames->submitted >= frames->slotCount;
    swapchains->waits[0] = (VkSemaphoreSubmitInfo){
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = frames->timeline,
        .value = reused ? frames->submitted + 1 - frames->slotCount : 0,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
    swapchains->signals[0] = (VkSemaphoreSubmitInfo){
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = frames->timeline,
        .value = frames->submitted + 1,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
    const VkSubmitInfo2 submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = images + (reused ? 1 : 0),
        .pWaitSemaphoreInfos = reused ? swapchains->waits : swapchains->waits + 1,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commands,
        .signalSemaphoreInfoCount = images + 1,
        .pSignalSemaphoreInfos = swapchains->signals,
    };
    return frames->api->vkQueueSubmit2(frames->queue, 1, &submit, VK_NULL_HANDLE);
}

mrhiResult mrhiVulkanSubmit(mrhiVulkanFrames* frames, const mrhiDriverFrame* frame, uint64_t tag)
{
    if (frames->lost)
    {
        return mrhi_errorDeviceLost;
    }
    mrhiVulkanSlot* slot = &frames->slots[frames->submitted % frames->slotCount];
    MRHI_ASSERT(slot->tag == 0 && tag != 0);
    VkResult result = MakeTransients(frames, slot, frame);
    if (result == VK_SUCCESS && frame->stagingBytes > 0)
    {
        memcpy(slot->stagingBytes, frame->staging, frame->stagingBytes);
    }
    mrhiResult status = mrhi_success;
    if (result == VK_SUCCESS)
    {
        result = Run(frames, slot, frame, &status);
    }
    if (result != VK_SUCCESS || status != mrhi_success)
    {
        DropTransients(frames, slot);
        slot->readbackCount = 0;
        frames->lost = result == VK_ERROR_DEVICE_LOST;
        return status != mrhi_success ? status : mrhiVulkanStatus(result);
    }
    ++frames->submitted;
    slot->serial = frames->submitted;
    slot->tag = tag;
    slot->ring = frame->readbackRing;
    // A present's own results reach the program at the next acquire;
    // only a lost device ends the frame here.
    frames->lost = !mrhiVulkanPresent(frames->swapchains);
    return mrhi_success;
}

// Copies a finished frame's readbacks into the core's ring.
static void FillReadbacks(mrhiVulkanFrames* frames, mrhiVulkanSlot* slot)
{
    for (uint32_t i = 0; i < slot->readbackCount; ++i)
    {
        const mrhiVulkanRange* range = &slot->readbacks[i];
        if (!frames->readbackCoherent)
        {
            VkDeviceSize first = range->offset & ~(frames->atom - 1);
            VkDeviceSize end =
                (range->offset + range->size + frames->atom - 1) & ~(frames->atom - 1);
            const VkMappedMemoryRange mapped = {
                .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                .memory = frames->readbackMemory,
                .offset = first,
                .size = end < frames->readbackSize ? end - first : VK_WHOLE_SIZE,
            };
            (void)frames->api->vkInvalidateMappedMemoryRanges(frames->device, 1, &mapped);
        }
        memcpy(slot->ring + range->offset, frames->readbackBytes + range->offset, range->size);
    }
    slot->readbackCount = 0;
}

size_t mrhiVulkanPollFrames(mrhiVulkanFrames* frames, mrhiDriverEvent* events, size_t capacity)
{
    if (frames->lost || capacity == 0)
    {
        return 0;
    }
    uint64_t done = 0;
    VkResult result =
        frames->api->vkGetSemaphoreCounterValue(frames->device, frames->timeline, &done);
    if (result != VK_SUCCESS)
    {
        frames->lost = true;
        events[0] = (mrhiDriverEvent){.tag = 0, .outcome = mrhi_errorDeviceLost};
        return 1;
    }
    size_t moved = 0;
    while (moved < capacity && frames->finished < frames->submitted && frames->finished < done)
    {
        mrhiVulkanSlot* slot = &frames->slots[frames->finished % frames->slotCount];
        FillReadbacks(frames, slot);
        DropTransients(frames, slot);
        RetireUpTo(frames, slot->serial);
        events[moved++] = (mrhiDriverEvent){.tag = slot->tag, .outcome = mrhi_success};
        slot->tag = 0;
        ++frames->finished;
    }
    return moved;
}

bool mrhiVulkanWaitFrame(mrhiVulkanFrames* frames, uint64_t tag, uint64_t timeoutNs)
{
    for (uint32_t i = 0; i < frames->slotCount; ++i)
    {
        if (frames->slots[i].tag == tag)
        {
            const VkSemaphoreWaitInfo wait = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
                .semaphoreCount = 1,
                .pSemaphores = &frames->timeline,
                .pValues = &frames->slots[i].serial,
            };
            return frames->api->vkWaitSemaphores(frames->device, &wait, timeoutNs) == VK_SUCCESS;
        }
    }
    MRHI_ASSERT(false);
    return false;
}
