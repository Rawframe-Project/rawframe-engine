// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Vulkan device's shaders and pipelines (mrhi-0003): shader modules
// from the container's SPIR-V, pipeline layouts from the reflection
// (a descriptor set per binding table, the device's heap layout after
// them for containers reading heaps, the root block as push constants),
// pipelines made at the call and answered at the next poll, and the
// device's pipeline cache.

#ifndef MAUL_RHI_SRC_VULKAN_PIPELINE_H
#define MAUL_RHI_SRC_VULKAN_PIPELINE_H

#include "driver.h"
#include "vulkan_object.h"

// The binding tables a pipeline layout has at most; a heap's set follows
// them, at set MRHI_VULKAN_TABLES.
#define MRHI_VULKAN_TABLES 4

// The constants one pipeline sets at most, a container's record bound.
#define MRHI_VULKAN_CONSTANTS 4096

typedef struct mrhiVulkanPipeline
{
    VkPipeline pipeline;
    VkPipelineLayout layout;
    // The tables' set layouts, which it owns, and the device's heap
    // layout after them when it reads heaps.
    VkDescriptorSetLayout sets[MRHI_VULKAN_TABLES + 1];
    uint32_t setCount;
    bool heap;
    // The stages its push constants reach; 0 without a root block.
    VkShaderStageFlags rootStages;
    VkPipelineBindPoint bindPoint;
} mrhiVulkanPipeline;

// Room to translate one pipeline's constants.
typedef struct mrhiVulkanConstants
{
    VkSpecializationMapEntry entries[MRHI_VULKAN_CONSTANTS];
    uint32_t data[MRHI_VULKAN_CONSTANTS];
} mrhiVulkanConstants;

typedef struct mrhiVulkanPipelines
{
    const mrhiVulkanDevice* api;
    VkDevice device;
    VkFormat depthStencil;
    // The device's heap layout, VK_NULL_HANDLE without bindless sampling.
    VkDescriptorSetLayout heapLayout;
    VkPipelineCache cache;
    // What a cache blob must name to be this device's.
    VkPhysicalDeviceProperties properties;
    VkShaderModule* shaders;
    mrhiVulkanSlots shaderSlots;
    mrhiVulkanPipeline* pipelines;
    mrhiVulkanSlots pipelineSlots;
    // Answers for the next poll, with the pipelines they answer.
    mrhiDriverEvent* pending;
    uint64_t* pendingHandles;
    uint32_t pendingCount;
    mrhiVulkanConstants* constants;
} mrhiVulkanPipelines;

// Makes the device's empty pipeline cache: mrhi_success, or the error.
mrhiResult mrhiVulkanPipelinesInit(mrhiVulkanPipelines* pipelines);

// Destroys every pipeline and shader left, and the cache.
void mrhiVulkanPipelinesDestroy(mrhiVulkanPipelines* pipelines);

mrhiResult mrhiVulkanCreateShader(mrhiVulkanPipelines* pipelines, const mrhiShaderDef* def,
                                  const mrhiContainer* container, uint64_t* handleOut);
void mrhiVulkanDestroyShader(mrhiVulkanPipelines* pipelines, uint64_t handle);

// Makes a compute pipeline at once, answered at the next poll.
mrhiResult mrhiVulkanCreateCompute(mrhiVulkanPipelines* pipelines,
                                   const mrhiDriverComputePipeline* pipeline, uint64_t tag,
                                   uint64_t* handleOut);

// Makes a graphics pipeline at once, answered at the next poll
// (vulkan_graphics.c).
mrhiResult mrhiVulkanCreateGraphics(mrhiVulkanPipelines* pipelines,
                                    const mrhiDriverGraphicsPipeline* pipeline, uint64_t tag,
                                    uint64_t* handleOut);

// Drops a pipeline's answer if it is still pending.
void mrhiVulkanForgetPipeline(mrhiVulkanPipelines* pipelines, uint64_t handle);

// Destroys a pipeline, dropping its answer if it is still pending.
void mrhiVulkanDestroyPipeline(mrhiVulkanPipelines* pipelines, uint64_t handle);

// Moves up to capacity answers into events.
size_t mrhiVulkanPollPipelines(mrhiVulkanPipelines* pipelines, mrhiDriverEvent* events,
                               size_t capacity);

// Takes a cache blob whose header names this device: false otherwise.
bool mrhiVulkanImportCache(mrhiVulkanPipelines* pipelines, const void* bytes, size_t size);

// Writes the cache's blob when capacity allows; its size either way.
size_t mrhiVulkanExportCache(mrhiVulkanPipelines* pipelines, void* bytes, size_t capacity);

// Shared with vulkan_graphics.c: makes a pipeline's set layouts and
// layout from the reflection, its push constants reaching rootStages.
mrhiResult mrhiVulkanMakeLayout(mrhiVulkanPipelines* pipelines, const mrhiReflection* reflection,
                                VkShaderStageFlags rootStages, mrhiVulkanPipeline* pipelineOut);

// Destroys what mrhiVulkanMakeLayout made.
void mrhiVulkanDropLayout(mrhiVulkanPipelines* pipelines, mrhiVulkanPipeline* pipeline);

// Fills a specialization from the program's constants, typed by the
// reflection.
void mrhiVulkanSpecialize(mrhiVulkanPipelines* pipelines, const mrhiReflection* reflection,
                          const mrhiConstantValue* values, uint32_t count,
                          VkSpecializationInfo* infoOut);

// Copies an entry point's name, NUL-terminated, into room of
// MRHI_VULKAN_NAME bytes.
#define MRHI_VULKAN_NAME 257
void mrhiVulkanEntryName(const mrhiReflection* reflection, uint32_t entry, char* nameOut);

// Takes a pipeline slot and records its answer: the handle, or 0 for
// none free.
uint32_t mrhiVulkanTakePipeline(mrhiVulkanPipelines* pipelines);
void mrhiVulkanAnswer(mrhiVulkanPipelines* pipelines, uint32_t handle, uint64_t tag);

// Gives back a slot whose pipeline failed.
void mrhiVulkanGivePipeline(mrhiVulkanPipelines* pipelines, uint32_t handle);

// The status a failed Vulkan call stands for.
mrhiResult mrhiVulkanStatus(VkResult result);

#endif // MAUL_RHI_SRC_VULKAN_PIPELINE_H
