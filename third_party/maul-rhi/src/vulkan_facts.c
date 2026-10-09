// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Vulkan device's floor, features and limits from its facts.

#include "vulkan_facts.h"

#include "capabilities_core.h"
#include "stage_limits.h"

// The frames the driver lets run at once: each is a value of the
// device's timeline semaphore, so the bound is the driver's own.
#define VULKAN_FRAMES_IN_FLIGHT 4

// The subgroup operations WGSL's subgroups need, in compute and
// fragment shaders.
#define SUBGROUP_OPERATIONS                                                                        \
    (VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_VOTE_BIT |                                \
     VK_SUBGROUP_FEATURE_ARITHMETIC_BIT | VK_SUBGROUP_FEATURE_BALLOT_BIT |                         \
     VK_SUBGROUP_FEATURE_SHUFFLE_BIT | VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT |                  \
     VK_SUBGROUP_FEATURE_QUAD_BIT)
#define SUBGROUP_STAGES (VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)

// What four full binding tables may take of a stage's samplers, and of
// its resources with the color targets: the heaps have the rest, since
// per-stage limits count every set of a pipeline.
#define TABLES_RESERVE    (4u * MRHI_TABLE_BINDINGS)
#define RESOURCES_RESERVE (TABLES_RESERVE + MRHI_COLOR_TARGETS)

// Whether a device has the Vulkan 1.0 features the contract's floor
// uses, which every device enables.
static bool HasFloorFeatures(const VkPhysicalDeviceFeatures* core)
{
    return core->fullDrawIndexUint32 && core->imageCubeArray && core->independentBlend &&
           core->sampleRateShading && core->depthBiasClamp && core->fragmentStoresAndAtomics &&
           core->samplerAnisotropy && core->shaderStorageImageExtendedFormats;
}

// Whether a device meets the driver's floor beyond its version.
bool mrhiVulkanMeetsFloor(const mrhiVulkanFacts* facts)
{
    return facts->family != UINT32_MAX && HasFloorFeatures(&facts->features.features) &&
           facts->features13.dynamicRendering && facts->features13.synchronization2 &&
           facts->features12.timelineSemaphore && facts->features12.bufferDeviceAddress &&
           facts->features12.descriptorIndexing;
}

// Whether a device has what a heap of sampled images and samplers
// needs: runtime arrays, partially bound and update after bind bindings
// updatable while pending, non-uniform indexing, and a set after the
// four tables.
static bool HasBindlessSampling(const mrhiVulkanFacts* facts)
{
    const VkPhysicalDeviceVulkan12Features* indexing = &facts->features12;
    return indexing->runtimeDescriptorArray && indexing->descriptorBindingPartiallyBound &&
           indexing->descriptorBindingUpdateUnusedWhilePending &&
           indexing->descriptorBindingSampledImageUpdateAfterBind &&
           indexing->shaderSampledImageArrayNonUniformIndexing &&
           facts->properties.properties.limits.maxBoundDescriptorSets >= 5;
}

// Whether a device can also hold storage images and buffers in the
// resource heap: their update after bind and non-uniform indexing,
// storage images without a format, and mutable descriptors.
static bool HasBindlessHeterogeneous(const mrhiVulkanFacts* facts)
{
    const VkPhysicalDeviceVulkan12Features* indexing = &facts->features12;
    const VkPhysicalDeviceFeatures* core = &facts->features.features;
    return HasBindlessSampling(facts) && indexing->descriptorBindingStorageImageUpdateAfterBind &&
           indexing->descriptorBindingStorageBufferUpdateAfterBind &&
           indexing->shaderStorageImageArrayNonUniformIndexing &&
           indexing->shaderStorageBufferArrayNonUniformIndexing &&
           core->shaderStorageImageReadWithoutFormat &&
           core->shaderStorageImageWriteWithoutFormat && facts->mutableType.mutableDescriptorType;
}

mrhiFeatures mrhiVulkanFeaturesOf(const mrhiVulkanFacts* facts, bool float32Filterable,
                                  bool rg11b10Renderable)
{
    const VkPhysicalDeviceFeatures* core = &facts->features.features;
    const VkPhysicalDeviceVulkan11Properties* subgroup = &facts->properties11;
    return (mrhiFeatures){
        .timestampQuery = facts->properties.properties.limits.timestampComputeAndGraphics &&
                          facts->familyTimestampBits > 0,
        .pipelineStatisticsQuery = core->pipelineStatisticsQuery,
        .textureCompressionBc = core->textureCompressionBC,
        .textureCompressionEtc2 = core->textureCompressionETC2,
        .textureCompressionAstc = core->textureCompressionASTC_LDR,
        .float32Filterable = float32Filterable,
        .rg11b10Renderable = rg11b10Renderable,
        .dualSourceBlending = core->dualSrcBlend,
        .unclippedDepth = core->depthClamp,
        .shaderF16 = facts->features12.shaderFloat16 &&
                     facts->features11.storageBuffer16BitAccess &&
                     facts->features11.uniformAndStorageBuffer16BitAccess,
        .subgroups =
            (subgroup->subgroupSupportedStages & SUBGROUP_STAGES) == SUBGROUP_STAGES &&
            (subgroup->subgroupSupportedOperations & SUBGROUP_OPERATIONS) == SUBGROUP_OPERATIONS,
        .shaderInt64 = core->shaderInt64,
        .indirectFirstInstance = core->drawIndirectFirstInstance,
        .multiDrawIndirectCount = core->multiDrawIndirect && facts->features12.drawIndirectCount,
        .multiview = facts->features11.multiview,
        .bindlessSampling = HasBindlessSampling(facts),
        .bindlessHeterogeneous = HasBindlessHeterogeneous(facts),
    };
}

static uint32_t Smaller(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static uint32_t Clamp32(uint64_t value)
{
    return value < UINT32_MAX ? (uint32_t)value : UINT32_MAX;
}

// A limit less a reserve, not below 0.
static uint32_t Net(uint32_t limit, uint32_t reserve)
{
    return limit > reserve ? limit - reserve : 0;
}

// The entries of the resource heap: the smaller of the update after bind
// limits of each type it may hold, per stage and per set, less what the
// tables and targets may take; 0 without bindless sampling.
static uint32_t HeapSize(const mrhiVulkanFacts* facts)
{
    if (!HasBindlessSampling(facts))
    {
        return 0;
    }
    const VkPhysicalDeviceVulkan12Properties* bind = &facts->properties12;
    uint32_t size = Smaller(bind->maxPerStageDescriptorUpdateAfterBindSampledImages,
                            bind->maxDescriptorSetUpdateAfterBindSampledImages);
    size = Smaller(size, bind->maxPerStageUpdateAfterBindResources);
    size = Smaller(size, bind->maxUpdateAfterBindDescriptorsInAllPools);
    if (HasBindlessHeterogeneous(facts))
    {
        size = Smaller(size, Smaller(bind->maxPerStageDescriptorUpdateAfterBindStorageImages,
                                     bind->maxDescriptorSetUpdateAfterBindStorageImages));
        size = Smaller(size, Smaller(bind->maxPerStageDescriptorUpdateAfterBindStorageBuffers,
                                     bind->maxDescriptorSetUpdateAfterBindStorageBuffers));
    }
    return Net(size, RESOURCES_RESERVE);
}

// The entries of the sampler heap, the same way.
static uint32_t SamplerHeapSize(const mrhiVulkanFacts* facts)
{
    if (!HasBindlessSampling(facts))
    {
        return 0;
    }
    const VkPhysicalDeviceVulkan12Properties* bind = &facts->properties12;
    uint32_t size = Smaller(bind->maxPerStageDescriptorUpdateAfterBindSamplers,
                            bind->maxDescriptorSetUpdateAfterBindSamplers);
    size = Smaller(size, bind->maxUpdateAfterBindDescriptorsInAllPools);
    return Net(size, TABLES_RESERVE);
}

void mrhiVulkanBindingLimits(const VkPhysicalDeviceLimits* limits, mrhiLimits* granted)
{
    const mrhiLimits floor = mrhiDefaultLimits();
    // A slot's number: Vulkan bounds no binding number, and the
    // contract's value is every driver's, as WebGPU's is.
    granted->bindingsPerTable = floor.bindingsPerTable;
    granted->sampledTexturesPerStage = limits->maxPerStageDescriptorSampledImages;
    granted->samplersPerStage = limits->maxPerStageDescriptorSamplers;
    granted->storageBuffersPerStage = limits->maxPerStageDescriptorStorageBuffers;
    granted->storageTexturesPerStage = limits->maxPerStageDescriptorStorageImages;
    granted->uniformBuffersPerStage = limits->maxPerStageDescriptorUniformBuffers;
    // A fragment stage's color attachments count toward the stage's
    // resources too.
    mrhiFitStageLimits(granted, Net(limits->maxPerStageResources, limits->maxColorAttachments));
    // Vulkan bounds tables and vertex buffers apart, never together: their
    // sum is the most a pipeline reaches, and any larger value bounds
    // nothing more, so a sum below the contract's value (four tables, as
    // many devices have) reads as that value.
    uint32_t sum = Clamp32((uint64_t)granted->bindingTables + granted->vertexBuffers);
    granted->tablesPlusVertexBuffers =
        sum > floor.tablesPlusVertexBuffers ? sum : floor.tablesPlusVertexBuffers;
}

mrhiLimits mrhiVulkanLimitsOf(const mrhiVulkanFacts* facts)
{
    const VkPhysicalDeviceLimits* limits = &facts->properties.properties.limits;
    uint32_t tables = limits->maxBoundDescriptorSets;
    uint32_t vertexBuffers = Smaller(limits->maxVertexInputBindings, MRHI_VULKAN_VERTEX_BUFFERS);
    mrhiLimits granted = {
        .textureDimension2d = limits->maxImageDimension2D,
        .textureDimension3d = limits->maxImageDimension3D,
        .textureArrayLayers = limits->maxImageArrayLayers,
        .bindingTables = tables,
        .uniformBindingBytes = limits->maxUniformBufferRange,
        .storageBindingBytes = limits->maxStorageBufferRange,
        .uniformOffsetAlignment = Clamp32(limits->minUniformBufferOffsetAlignment),
        .storageOffsetAlignment = Clamp32(limits->minStorageBufferOffsetAlignment),
        .vertexBuffers = vertexBuffers,
        .bufferBytes = facts->properties13.maxBufferSize,
        .vertexAttributes =
            Smaller(limits->maxVertexInputAttributes, MRHI_VULKAN_VERTEX_ATTRIBUTES),
        .vertexStride = limits->maxVertexInputBindingStride,
        .interStageVariables =
            Smaller(limits->maxVertexOutputComponents, limits->maxFragmentInputComponents) / 4,
        .colorAttachments = limits->maxColorAttachments,
        // Vulkan has no such bound: every target may hold 16 bytes.
        .colorBytesPerSample = Clamp32((uint64_t)limits->maxColorAttachments * 16),
        .workgroupStorageBytes = limits->maxComputeSharedMemorySize,
        .workgroupInvocations = limits->maxComputeWorkGroupInvocations,
        .workgroupSizeX = limits->maxComputeWorkGroupSize[0],
        .workgroupSizeY = limits->maxComputeWorkGroupSize[1],
        .workgroupSizeZ = limits->maxComputeWorkGroupSize[2],
        .workgroupsPerDimension = Smaller(
            Smaller(limits->maxComputeWorkGroupCount[0], limits->maxComputeWorkGroupCount[1]),
            limits->maxComputeWorkGroupCount[2]),
        .rootBlockBytes = limits->maxPushConstantsSize,
        .framesInFlight = VULKAN_FRAMES_IN_FLIGHT,
        .heapSize = HeapSize(facts),
        .samplerHeapSize = SamplerHeapSize(facts),
        // A view mask holds 32 views.
        .multiviewViews = facts->features11.multiview
                              ? Smaller(facts->properties11.maxMultiviewViewCount, 32)
                              : 1,
    };
    mrhiVulkanBindingLimits(limits, &granted);
    return granted;
}
