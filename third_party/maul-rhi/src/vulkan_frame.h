// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Vulkan device's frames: a slot per frame in flight with its command
// buffers and the batches it submits them in, its transients' memory and
// objects, its mapped staging and the readbacks it fills; the timeline
// semaphore whose value counts the frames finished, and one per lane for
// the batches; and the queue of destroyed objects, retired once the next
// frame submitted after their destruction finishes.

#ifndef MAUL_RHI_SRC_VULKAN_FRAME_H
#define MAUL_RHI_SRC_VULKAN_FRAME_H

#include "vulkan_heap.h"
#include "vulkan_pipeline.h"
#include "vulkan_swapchain.h"

#include "maul-rhi/device.h"

// The bytes of the device's zeros at least, which a texture's clear
// copies where no clear takes its format, a band of blocks at a time.
#define MRHI_VULKAN_ZERO_BYTES 65536

// The descriptor pools a frame slot has at most, and each pool's sets
// and descriptors of every type.
#define MRHI_VULKAN_POOLS            64
#define MRHI_VULKAN_POOL_SETS        256
#define MRHI_VULKAN_POOL_DESCRIPTORS 1024

// The lanes a frame's passes are recorded in. Every pass class has one
// with MAUL_RHI_VULKAN_SPLIT_LANES, each lane's passes running on the one
// queue as submissions of their own that wait on the passes they follow
// through the lanes' timeline semaphores; otherwise every class is
// recorded in one lane, a frame's one submission.
#ifdef MAUL_RHI_VULKAN_SPLIT_LANES
#define MRHI_VULKAN_LANES 3
#define MRHI_VULKAN_SPLIT true
#else
#define MRHI_VULKAN_LANES 1
#define MRHI_VULKAN_SPLIT false
#endif

// The lane a pass class records in: its own with a lane per class, else
// the one lane.
static inline uint32_t mrhiVulkanLaneOf(mrhiPassClass passClass)
{
    return (uint32_t)passClass % MRHI_VULKAN_LANES;
}

// The command buffers one batch submits at most: its parts with the
// native passes' between them.
#define MRHI_VULKAN_BATCH_SUBMITS (1 + 2 * MRHI_NATIVE_PASSES)

// One submission of a frame: a lane's command buffers, a range of its
// slot's submits; by lane, the timeline value it waits for (0 for none,
// always for its own); and the value of its lane's timeline it signals,
// 0 for none.
typedef struct mrhiVulkanBatch
{
    uint32_t lane;
    uint32_t firstSubmit;
    uint32_t submitCount;
    uint64_t waits[MRHI_VULKAN_LANES];
    uint64_t signal;
} mrhiVulkanBatch;

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
    // The command buffer the frame records into now, one of its parts: a
    // batch's first, then after each native pass the next.
    VkCommandBuffer commands;
    VkCommandBuffer* parts;
    uint32_t partsUsed;
    // The command buffers the frame submits, each batch's together: its
    // parts with the native passes' between them.
    VkCommandBufferSubmitInfo* submits;
    uint32_t submitCount;
    // The frame's batches, in the order they are submitted, and room for
    // their submissions and semaphores.
    mrhiVulkanBatch* batches;
    uint32_t batchCount;
    VkSubmitInfo2* infos;
    VkSemaphoreSubmitInfo* semaphores;
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
    // Each lane's timeline semaphore, made only with more than one lane,
    // and the last value given each.
    VkSemaphore lanes[MRHI_VULKAN_LANES];
    uint64_t laneValues[MRHI_VULKAN_LANES];
    VkFormat depthStencil;
    const VkPhysicalDeviceMemoryProperties* memory;
    mrhiVulkanObjects* objects;
    mrhiVulkanPipelines* pipelines;
    mrhiVulkanHeaps* heaps;
    mrhiVulkanSwapchains* swapchains;
    mrhiVulkanSlot* slots;
    uint32_t slotCount;
    // The command buffers and batches each slot has.
    uint32_t partLimit;
    uint32_t batchLimit;
    // While a frame records, by frame pass index: whether a pass of
    // another lane follows it, and the batch it is in (an index plus one).
    bool* followed;
    uint32_t* passBatches;
    uint32_t readbackLimit;
    uint32_t viewLimit;
    // Frames submitted, and frames reported finished.
    uint64_t submitted;
    // Frames recorded, failed ones included: a query set is reset once in
    // each recording, and a failed one is not submitted, so the next frame,
    // under the same serial, resets it again.
    uint64_t recorded;
    uint64_t finished;
    VkDeviceMemory zerosMemory;
    VkBuffer zeros;
    // Their bytes: MRHI_VULKAN_ZERO_BYTES, or a row of blocks of the
    // widest texture the device takes where that is more.
    uint64_t zeroBytes;
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
// staging, the readback buffer, and the zeros, for a device with these
// limits whose textures are at most `widest` texels across: mrhi_success,
// or the error.
mrhiResult mrhiVulkanFramesInit(mrhiVulkanFrames* frames, uint32_t family,
                                const mrhiDeviceLimits* limits, uint32_t widest);

// Destroys everything the frames hold, the retiring objects included,
// on an idle device.
void mrhiVulkanFramesDestroy(mrhiVulkanFrames* frames);

// Records and submits a frame, then presents its surface images:
// mrhi_success, mrhi_errorCapacity with the budget that ran out in
// budgetOut, or mrhi_errorDeviceLost.
mrhiResult mrhiVulkanSubmit(mrhiVulkanFrames* frames, const mrhiDriverFrame* frame, uint64_t tag,
                            mrhiDiagnosticCode* budgetOut);

// Moves up to capacity finished frames into events, in order, their
// readbacks filled and their objects and the objects they retire gone;
// or reports the device's loss with tag 0.
size_t mrhiVulkanPollFrames(mrhiVulkanFrames* frames, mrhiDriverEvent* events, size_t capacity);

// Waits up to timeoutNs for a running frame: whether it finished.
bool mrhiVulkanWaitFrame(mrhiVulkanFrames* frames, uint64_t tag, uint64_t timeoutNs);

// Destroys an object once the next frame submitted finishes.
void mrhiVulkanRetireLater(mrhiVulkanFrames* frames, mrhiVulkanRetired kind, uint64_t handle);

// Records a frame's work into its slot's batches (vulkan_record.c):
// mrhi_success, or mrhi_errorCapacity when views or descriptor sets run
// out, with the budget in budgetOut; resultOut is the device's failure
// to begin or end a command buffer, VK_SUCCESS without one.
mrhiResult mrhiVulkanRecord(mrhiVulkanFrames* frames, mrhiVulkanSlot* slot,
                            const mrhiDriverFrame* frame, VkResult* resultOut,
                            mrhiDiagnosticCode* budgetOut);

#endif // MAUL_RHI_SRC_VULKAN_FRAME_H
