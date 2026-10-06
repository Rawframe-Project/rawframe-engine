// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Vulkan device's objects (mrhi-0003): buffers, textures, views,
// samplers and query sets in tables sized from the device limits, each
// handle its slot's index plus one.

#ifndef MAUL_RHI_SRC_VULKAN_OBJECT_H
#define MAUL_RHI_SRC_VULKAN_OBJECT_H

#include "vulkan_memory.h"

#include "maul-rhi/resources.h"

// Free slots of a table, linked by index plus one.
typedef struct mrhiVulkanSlots
{
    uint32_t* next;
    uint32_t head;
    uint32_t capacity;
} mrhiVulkanSlots;

typedef struct mrhiVulkanBuffer
{
    VkBuffer buffer;
    mrhiVulkanAllocation allocation;
} mrhiVulkanBuffer;

typedef struct mrhiVulkanTexture
{
    VkImage image;
    mrhiVulkanAllocation allocation;
    // Made elsewhere and adopted (mrhi-0018): neither it nor its memory is
    // the library's to destroy.
    bool adopted;
} mrhiVulkanTexture;

// The most queries a set has (mrhi-0012), and the words of a set's
// bits.
#define MRHI_VULKAN_SET_QUERIES 4096
#define MRHI_VULKAN_SET_WORDS   (MRHI_VULKAN_SET_QUERIES / 64)

// A query set: its pool, its queries and their type, the frame that
// reset it last, and the queries that frame has written so far as it
// records.
typedef struct mrhiVulkanQuerySet
{
    VkQueryPool pool;
    uint32_t count;
    mrhiQueryType type;
    uint64_t resetSerial;
    uint64_t written[MRHI_VULKAN_SET_WORDS];
} mrhiVulkanQuerySet;

typedef struct mrhiVulkanObjects
{
    const mrhiVulkanDevice* api;
    VkDevice device;
    mrhiVulkanMemory* memory;
    VkFormat depthStencil;
    float maxAnisotropy;
    mrhiVulkanBuffer* buffers;
    mrhiVulkanSlots bufferSlots;
    mrhiVulkanTexture* textures;
    mrhiVulkanSlots textureSlots;
    VkImageView* views;
    mrhiVulkanSlots viewSlots;
    VkSampler* samplers;
    mrhiVulkanSlots samplerSlots;
    mrhiVulkanQuerySet* querySets;
    mrhiVulkanSlots querySetSlots;
} mrhiVulkanObjects;

// Sets up a table's free slots over an array of capacity entries.
void mrhiVulkanSlotsInit(mrhiVulkanSlots* slots, uint32_t* next, uint32_t capacity);

// A free slot's handle, or 0 for none; and a slot given back.
uint32_t mrhiVulkanTakeSlot(mrhiVulkanSlots* slots);
void mrhiVulkanGiveSlot(mrhiVulkanSlots* slots, uint64_t handle);

// Makes an object from a def the core has checked: its handle, never
// zero; mrhi_errorCapacity when memory or the table runs out, or
// mrhi_errorPlatform.
mrhiResult mrhiVulkanCreateBuffer(mrhiVulkanObjects* objects, const mrhiBufferDef* def,
                                  uint64_t* handleOut);
mrhiResult mrhiVulkanCreateTexture(mrhiVulkanObjects* objects, const mrhiTextureDef* def,
                                   uint64_t* handleOut);
mrhiResult mrhiVulkanCreateView(mrhiVulkanObjects* objects, uint64_t texture,
                                const mrhiViewDef* def, uint64_t* handleOut);
mrhiResult mrhiVulkanCreateSampler(mrhiVulkanObjects* objects, const mrhiSamplerDef* def,
                                   uint64_t* handleOut);
mrhiResult mrhiVulkanCreateQuerySet(mrhiVulkanObjects* objects, const mrhiQuerySetDef* def,
                                    uint64_t* handleOut);

// Destroys an object at once, with its memory.
void mrhiVulkanDestroyBuffer(mrhiVulkanObjects* objects, uint64_t handle);
void mrhiVulkanDestroyTexture(mrhiVulkanObjects* objects, uint64_t handle);
void mrhiVulkanDestroyView(mrhiVulkanObjects* objects, uint64_t handle);
void mrhiVulkanDestroySampler(mrhiVulkanObjects* objects, uint64_t handle);
void mrhiVulkanDestroyQuerySet(mrhiVulkanObjects* objects, uint64_t handle);

#endif // MAUL_RHI_SRC_VULKAN_OBJECT_H
