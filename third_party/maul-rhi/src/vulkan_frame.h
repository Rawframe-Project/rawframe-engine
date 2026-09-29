// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Vulkan device's frames (mrhi-0003, mrhi-0013): a slot per frame in
// flight with its command buffer, its transients' memory and objects,
// its mapped staging and the readbacks it fills; the timeline semaphore
// whose value counts the frames finished; and the queue of destroyed
// objects, retired once the next frame submitted after their
// destruction finishes.

#ifndef MAUL_RHI_SRC_VULKAN_FRAME_H
#define MAUL_RHI_SRC_VULKAN_FRAME_H

#include "vulkan_heap.h"
#include "vulkan_pipeline.h"
#include "vulkan_swapchain.h"

#include "maul-rhi/device.h"

// The descriptor pools a frame slot has at most, and each pool's sets
// and descriptors of every type.
#define MRHI_VULKAN_POOLS            64
#define MRHI_VULKAN_POOL_SETS        256
#define MRHI_VULKAN_POOL_DESCRIPTORS 1024

// What a destroyed object waiting to retire is.
typedef enum mrhiVulkanRetired
{
    mrhiVulkanRetiredBuffer,
    mrhiVulkanRetiredTexture,
    mrhiVulkanRetiredView,
    mrhiVulkanRetiredSampler,
    mrhiVulkanRetiredQuerySet,
    mrhiVulkanRetiredPipeline,
    mrhiVulkanRetiredHeap,
} mrhiVulkanRetired;

typedef struct mrhiVulkanRetire
{
    // The frame whose end retires it: the frames submitted when it was
    // destroyed, plus one.
    uint64_t serial;
    uint64_t handle;
    mrhiVulkanRetired kind;
} mrhiVulkanRetire;

// A range of the readback ring a frame fills.
typedef struct mrhiVulkanRange
{
    uint64_t offset;
    uint64_t size;
} mrhiVulkanRange;

typedef struct mrhiVulkanSlot
{
    VkCommandPool pool;
    VkCommandBuffer commands;
    // The core's tag while its frame runs; 0 when idle.
    uint64_t tag;
    // The frame's serial, the timeline value its end signals.
    uint64_t serial;
    // The transients' block and its size.
    VkDeviceMemory memory;
    VkDeviceSize memoryBytes;
    uint32_t memoryType;
    // The frame's transients by frame slot, and memory of their own for
    // a target a tile GPU keeps on chip.
    VkImage* images;
    VkBuffer* buffers;
    VkDeviceMemory* own;
    uint32_t transients;
    VkDeviceMemory stagingMemory;
    VkBuffer staging;
    uint8_t* stagingBytes;
    // The core's ring and the ranges of it the frame fills.
    uint8_t* ring;
    mrhiVulkanRange* readbacks;
    uint32_t readbackCount;
    // The views the frame's targets and bindings take.
    VkImageView* views;
    uint32_t viewCount;
    // Descriptor pools, made as the frames need them and reset with the
    // slot, and the one sets are taken from.
    VkDescriptorPool pools[MRHI_VULKAN_POOLS];
    uint32_t poolCount;
    uint32_t poolInUse;
} mrhiVulkanSlot;

typedef struct mrhiVulkanFrames
{
    const mrhiVulkanDevice* api;
    VkDevice device;
    VkQueue queue;
    VkSemaphore timeline;
    VkFormat depthStencil;
    const VkPhysicalDeviceMemoryProperties* memory;
    mrhiVulkanObjects* objects;
    mrhiVulkanPipelines* pipelines;
    mrhiVulkanHeaps* heaps;
    mrhiVulkanSwapchains* swapchains;
    mrhiVulkanSlot* slots;
    uint32_t slotCount;
    uint32_t readbackLimit;
    uint32_t viewLimit;
    // Frames submitted, and frames reported finished.
    uint64_t submitted;
    uint64_t finished;
    VkDeviceMemory readbackMemory;
    VkBuffer readback;
    uint8_t* readbackBytes;
    VkDeviceSize readbackSize;
    bool readbackCoherent;
    VkDeviceSize atom;
    mrhiVulkanRetire* retire;
    uint32_t retireFirst;
    uint32_t retireCount;
    uint32_t retireCapacity;
    bool lost;
} mrhiVulkanFrames;

// Makes each slot's command pool on the queue family, its buffer and
// staging, and the readback buffer, for a device with these limits:
// mrhi_success, or the error.
mrhiResult mrhiVulkanFramesInit(mrhiVulkanFrames* frames, uint32_t family,
                                const mrhiDeviceLimits* limits);

// Destroys everything the frames hold, the retiring objects included,
// on an idle device.
void mrhiVulkanFramesDestroy(mrhiVulkanFrames* frames);

// Records and submits a frame, then presents its surface images:
// mrhi_success, mrhi_errorCapacity, or mrhi_errorDeviceLost.
mrhiResult mrhiVulkanSubmit(mrhiVulkanFrames* frames, const mrhiDriverFrame* frame, uint64_t tag);

// Moves up to capacity finished frames into events, in order, their
// readbacks filled and their objects and the objects they retire gone;
// or reports the device's loss with tag 0.
size_t mrhiVulkanPollFrames(mrhiVulkanFrames* frames, mrhiDriverEvent* events, size_t capacity);

// Waits up to timeoutNs for a running frame: whether it finished.
bool mrhiVulkanWaitFrame(mrhiVulkanFrames* frames, uint64_t tag, uint64_t timeoutNs);

// Destroys an object once the next frame submitted finishes.
void mrhiVulkanRetireLater(mrhiVulkanFrames* frames, mrhiVulkanRetired kind, uint64_t handle);

// Records a frame's work into its slot's command buffer
// (vulkan_record.c): mrhi_success, or mrhi_errorCapacity when views or
// descriptor sets run out.
mrhiResult mrhiVulkanRecord(mrhiVulkanFrames* frames, mrhiVulkanSlot* slot,
                            const mrhiDriverFrame* frame);

#endif // MAUL_RHI_SRC_VULKAN_FRAME_H
