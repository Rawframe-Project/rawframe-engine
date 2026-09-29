// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Vulkan device (mrhi-0003): opened with the floor's features and the
// granted ones, one queue, and a timeline semaphore for the frames. The
// memory a declared resource takes is read from the create info alone
// (Vulkan 1.3's device memory requirements). Buffers, textures, views
// and samplers are made in device-local memory, query sets as query
// pools; shader modules and pipelines are made at the call; configured
// surfaces get swapchains (vulkan_swapchain.c). Frames run.

#include "vulkan_device.h"

#include "allocator.h"
#include "command.h"
#include "invariant.h"
#include "vulkan_adapter.h"
#include "vulkan_frame.h"
#include "vulkan_heap.h"
#include "vulkan_label.h"
#include "vulkan_object.h"
#include "vulkan_pipeline.h"
#include "vulkan_resource.h"
#include "vulkan_swapchain.h"

#include <stdalign.h>
#include <stdckdint.h>
#include <string.h>

typedef struct VulkanDevice
{
    mrhiAllocator allocator;
    const mrhiVulkan* vulkan;
    mrhiVulkanDevice api;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    uint32_t family;
    // Its value is the last frame finished.
    VkSemaphore timeline;
    VkFormat depthStencil;
    // The alignment every placement in the frame's memory keeps, so that
    // buffers and images may lie side by side.
    VkDeviceSize granularity;
    // The unit mapped memory is invalidated in.
    VkDeviceSize atom;
    // The memory types allocated lazily, where a tile GPU keeps
    // transient targets on chip.
    uint32_t lazyTypes;
    double timestampPeriod;
    size_t bytes;
    mrhiVulkanMemory memory;
    mrhiVulkanObjects objects;
    mrhiVulkanPipelines pipelines;
    mrhiVulkanHeaps heaps;
    mrhiVulkanFrames frames;
    mrhiVulkanSwapchains swapchains;
} VulkanDevice;

// The features a device enables, chained.
typedef struct Enabled
{
    VkPhysicalDeviceFeatures2 features;
    VkPhysicalDeviceVulkan11Features features11;
    VkPhysicalDeviceVulkan12Features features12;
    VkPhysicalDeviceVulkan13Features features13;
    VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT mutableType;
} Enabled;

// The floor's features, and the granted ones as the contract's Vulkan
// rows name them.
static void Enable(const mrhiFeatures* granted, Enabled* enabled)
{
    *enabled = (Enabled){
        .features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2},
        .features11 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES},
        .features12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES},
        .features13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES},
        .mutableType = {.sType =
                            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT},
    };
    enabled->features.pNext = &enabled->features11;
    enabled->features11.pNext = &enabled->features12;
    enabled->features12.pNext = &enabled->features13;
    VkPhysicalDeviceFeatures* core = &enabled->features.features;
    core->fullDrawIndexUint32 = VK_TRUE;
    core->imageCubeArray = VK_TRUE;
    core->independentBlend = VK_TRUE;
    core->sampleRateShading = VK_TRUE;
    core->depthBiasClamp = VK_TRUE;
    core->fragmentStoresAndAtomics = VK_TRUE;
    core->samplerAnisotropy = VK_TRUE;
    core->shaderStorageImageExtendedFormats = VK_TRUE;
    core->pipelineStatisticsQuery = granted->pipelineStatisticsQuery;
    core->textureCompressionBC = granted->textureCompressionBc;
    core->textureCompressionETC2 = granted->textureCompressionEtc2;
    core->textureCompressionASTC_LDR = granted->textureCompressionAstc;
    core->dualSrcBlend = granted->dualSourceBlending;
    core->depthClamp = granted->unclippedDepth;
    core->shaderInt64 = granted->shaderInt64;
    core->drawIndirectFirstInstance = granted->indirectFirstInstance;
    core->multiDrawIndirect = granted->multiDrawIndirectCount;
    enabled->features11.multiview = granted->multiview;
    enabled->features11.storageBuffer16BitAccess = granted->shaderF16;
    enabled->features11.uniformAndStorageBuffer16BitAccess = granted->shaderF16;
    enabled->features12.shaderFloat16 = granted->shaderF16;
    enabled->features12.drawIndirectCount = granted->multiDrawIndirectCount;
    enabled->features12.timelineSemaphore = VK_TRUE;
    enabled->features12.bufferDeviceAddress = VK_TRUE;
    enabled->features12.descriptorIndexing = VK_TRUE;
    enabled->features13.dynamicRendering = VK_TRUE;
    enabled->features13.synchronization2 = VK_TRUE;
    // Heaps (mrhi-0015): descriptor indexing over sampled images and samplers,
    // and over storage images and buffers through mutable descriptors.
    VkPhysicalDeviceVulkan12Features* indexing = &enabled->features12;
    bool sampling = granted->bindlessSampling;
    bool heterogeneous = granted->bindlessHeterogeneous;
    indexing->runtimeDescriptorArray = sampling;
    indexing->descriptorBindingPartiallyBound = sampling;
    indexing->descriptorBindingUpdateUnusedWhilePending = sampling;
    indexing->descriptorBindingSampledImageUpdateAfterBind = sampling;
    indexing->shaderSampledImageArrayNonUniformIndexing = sampling;
    indexing->descriptorBindingStorageImageUpdateAfterBind = heterogeneous;
    indexing->descriptorBindingStorageBufferUpdateAfterBind = heterogeneous;
    indexing->shaderStorageImageArrayNonUniformIndexing = heterogeneous;
    indexing->shaderStorageBufferArrayNonUniformIndexing = heterogeneous;
    core->shaderStorageImageReadWithoutFormat = heterogeneous;
    core->shaderStorageImageWriteWithoutFormat = heterogeneous;
    enabled->mutableType.mutableDescriptorType = heterogeneous;
    enabled->features13.pNext = heterogeneous ? &enabled->mutableType : nullptr;
}

static mrhiResult StatusOf(VkResult result)
{
    switch (result)
    {
    case VK_SUCCESS:
        return mrhi_success;
    case VK_ERROR_OUT_OF_HOST_MEMORY:
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return mrhi_errorCapacity;
    case VK_ERROR_DEVICE_LOST:
        return mrhi_errorDeviceLost;
    default:
        return mrhi_errorPlatform;
    }
}

// Destroys the objects the core leaves to the device's end.
static void DestroyObjects(mrhiVulkanObjects* objects)
{
    for (uint32_t i = 0; i < objects->viewSlots.capacity; ++i)
    {
        objects->api->vkDestroyImageView(objects->device, objects->views[i], nullptr);
    }
    for (uint32_t i = 0; i < objects->samplerSlots.capacity; ++i)
    {
        objects->api->vkDestroySampler(objects->device, objects->samplers[i], nullptr);
    }
    for (uint32_t i = 0; i < objects->querySetSlots.capacity; ++i)
    {
        objects->api->vkDestroyQueryPool(objects->device, objects->querySets[i].pool, nullptr);
    }
    for (uint32_t i = 0; i < objects->bufferSlots.capacity; ++i)
    {
        objects->api->vkDestroyBuffer(objects->device, objects->buffers[i].buffer, nullptr);
    }
    for (uint32_t i = 0; i < objects->textureSlots.capacity; ++i)
    {
        objects->api->vkDestroyImage(objects->device, objects->textures[i].image, nullptr);
    }
}

static void Destroy(void* self)
{
    VulkanDevice* device = self;
    // Nothing runs once a device is destroyed; a lost device answers at
    // once.
    (void)device->api.vkDeviceWaitIdle(device->device);
    mrhiVulkanSwapchainsDestroy(&device->swapchains);
    mrhiVulkanFramesDestroy(&device->frames);
    mrhiVulkanPipelinesDestroy(&device->pipelines);
    mrhiVulkanHeapsEnd(&device->heaps);
    DestroyObjects(&device->objects);
    mrhiVulkanMemoryDestroy(&device->memory);
    device->api.vkDestroySemaphore(device->device, device->timeline, nullptr);
    device->api.vkDestroyDevice(device->device, nullptr);
    mrhiAllocator allocator = device->allocator;
    mrhiRelease(&allocator, device, device->bytes, alignof(VulkanDevice));
}

static mrhiResult CreateSampler(void* self, const mrhiSamplerDef* def, uint64_t* handleOut)
{
    VulkanDevice* device = self;
    return mrhiVulkanCreateSampler(&device->objects, def, handleOut);
}

static void DestroySampler(void* self, uint64_t handle)
{
    VulkanDevice* device = self;
    mrhiVulkanRetireLater(&device->frames, mrhiVulkanRetiredSampler, handle);
}

static mrhiResult CreateHeap(void* self, const mrhiHeapDef* def, uint64_t* handleOut)
{
    VulkanDevice* device = self;
    mrhiResult status = mrhiVulkanCreateHeap(&device->heaps, handleOut);
    if (status == mrhi_success)
    {
        mrhiVulkanName(&device->api, device->device, VK_OBJECT_TYPE_DESCRIPTOR_SET,
                       MRHI_VULKAN_HANDLE(mrhiVulkanHeapSet(&device->heaps, *handleOut)),
                       def->label, def->labelLength);
    }
    return status;
}

static void DestroyHeap(void* self, uint64_t handle)
{
    VulkanDevice* device = self;
    mrhiVulkanRetireLater(&device->frames, mrhiVulkanRetiredHeap, handle);
}

static void WriteHeapEntry(void* self, uint64_t heap, uint32_t index,
                           const mrhiDriverHeapEntry* entry)
{
    const VulkanDevice* device = self;
    mrhiVulkanWriteHeapEntry(&device->heaps, heap, index, entry);
}

static void WriteHeapSampler(void* self, uint64_t heap, uint32_t index, uint64_t sampler)
{
    const VulkanDevice* device = self;
    mrhiVulkanWriteHeapSampler(&device->heaps, heap, index, sampler);
}

static mrhiResult CreateQuerySet(void* self, const mrhiQuerySetDef* def, uint64_t* handleOut)
{
    VulkanDevice* device = self;
    return mrhiVulkanCreateQuerySet(&device->objects, def, handleOut);
}

static void DestroyQuerySet(void* self, uint64_t handle)
{
    VulkanDevice* device = self;
    mrhiVulkanRetireLater(&device->frames, mrhiVulkanRetiredQuerySet, handle);
}

static mrhiResult CreateBuffer(void* self, const mrhiBufferDef* def, uint64_t* handleOut)
{
    VulkanDevice* device = self;
    return mrhiVulkanCreateBuffer(&device->objects, def, handleOut);
}

static void DestroyBuffer(void* self, uint64_t handle)
{
    VulkanDevice* device = self;
    mrhiVulkanRetireLater(&device->frames, mrhiVulkanRetiredBuffer, handle);
}

static mrhiResult CreateTexture(void* self, const mrhiTextureDef* def, uint64_t* handleOut)
{
    VulkanDevice* device = self;
    return mrhiVulkanCreateTexture(&device->objects, def, handleOut);
}

static void DestroyTexture(void* self, uint64_t handle)
{
    VulkanDevice* device = self;
    mrhiVulkanRetireLater(&device->frames, mrhiVulkanRetiredTexture, handle);
}

static mrhiResult CreateView(void* self, uint64_t texture, const mrhiViewDef* def,
                             uint64_t* handleOut)
{
    VulkanDevice* device = self;
    return mrhiVulkanCreateView(&device->objects, texture, def, handleOut);
}

static void DestroyView(void* self, uint64_t handle)
{
    VulkanDevice* device = self;
    mrhiVulkanRetireLater(&device->frames, mrhiVulkanRetiredView, handle);
}

static void TextureMemory(const void* self, const mrhiTextureDef* def, uint64_t* bytesOut,
                          uint64_t* alignmentOut)
{
    const VulkanDevice* device = self;
    mrhiVulkanImage image;
    mrhiVulkanImageOf(def, device->depthStencil, &image);
    const VkDeviceImageMemoryRequirements query = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_IMAGE_MEMORY_REQUIREMENTS,
        .pCreateInfo = &image.info,
    };
    VkMemoryRequirements2 needs = {.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    device->api.vkGetDeviceImageMemoryRequirements(device->device, &query, &needs);
    bool onChip = (def->usage & mrhi_textureTransient) != 0 &&
                  (needs.memoryRequirements.memoryTypeBits & device->lazyTypes) != 0;
    VkDeviceSize alignment = needs.memoryRequirements.alignment;
    *bytesOut = onChip ? 0 : needs.memoryRequirements.size;
    *alignmentOut = alignment > device->granularity ? alignment : device->granularity;
}

static void BufferMemory(const void* self, const mrhiBufferDef* def, uint64_t* bytesOut,
                         uint64_t* alignmentOut)
{
    const VulkanDevice* device = self;
    const VkBufferCreateInfo info = mrhiVulkanBufferOf(def);
    const VkDeviceBufferMemoryRequirements query = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS,
        .pCreateInfo = &info,
    };
    VkMemoryRequirements2 needs = {.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    device->api.vkGetDeviceBufferMemoryRequirements(device->device, &query, &needs);
    VkDeviceSize alignment = needs.memoryRequirements.alignment;
    *bytesOut = needs.memoryRequirements.size;
    *alignmentOut = alignment > device->granularity ? alignment : device->granularity;
}

static double TimestampPeriod(void* self)
{
    const VulkanDevice* device = self;
    return device->timestampPeriod;
}

static void LossReport(void* self, mrhiDeviceLossReport* reportOut)
{
    (void)self;
    static const char message[] = "Vulkan reported VK_ERROR_DEVICE_LOST";
    *reportOut = (mrhiDeviceLossReport){.messageLength = sizeof(message) - 1};
    memcpy(reportOut->message, message, sizeof(message) - 1);
}

static mrhiResult ConfigureSurface(void* self, uint64_t surface, const mrhiSurfaceConfig* config,
                                   uint64_t oldSwapchain, uint64_t* swapchainOut)
{
    VulkanDevice* device = self;
    // The core configures only surfaces the adapter presents to, which
    // needs VK_KHR_swapchain, enabled wherever it is offered.
    MRHI_ASSERT(device->api.vkCreateSwapchainKHR != nullptr);
    static_assert(sizeof(VkSurfaceKHR) == sizeof(uint64_t), "a handle holds a surface");
    VkSurfaceKHR handle;
    memcpy((void*)&handle, &surface, sizeof(surface));
    return mrhiVulkanConfigure(&device->swapchains, handle, config, oldSwapchain, swapchainOut);
}

static void UnconfigureSurface(void* self, uint64_t swapchain)
{
    VulkanDevice* device = self;
    mrhiVulkanUnconfigure(&device->swapchains, swapchain);
}

static mrhiResult AcquireImage(void* self, uint64_t swapchain, uint64_t* imageOut)
{
    VulkanDevice* device = self;
    return mrhiVulkanAcquire(&device->swapchains, swapchain, imageOut);
}

static void ReleaseImage(void* self, uint64_t swapchain, uint64_t image)
{
    VulkanDevice* device = self;
    mrhiVulkanGiveBack(&device->swapchains, swapchain, image);
}

static mrhiResult CreateShader(void* self, const mrhiShaderDef* def, const mrhiContainer* container,
                               uint64_t* handleOut)
{
    VulkanDevice* device = self;
    return mrhiVulkanCreateShader(&device->pipelines, def, container, handleOut);
}

static void DestroyShader(void* self, uint64_t handle)
{
    VulkanDevice* device = self;
    mrhiVulkanDestroyShader(&device->pipelines, handle);
}

static mrhiResult CreateCompute(void* self, const mrhiDriverComputePipeline* pipeline, uint64_t tag,
                                uint64_t* handleOut)
{
    VulkanDevice* device = self;
    return mrhiVulkanCreateCompute(&device->pipelines, pipeline, tag, handleOut);
}

static mrhiResult CreateGraphics(void* self, const mrhiDriverGraphicsPipeline* pipeline,
                                 uint64_t tag, uint64_t* handleOut)
{
    VulkanDevice* device = self;
    return mrhiVulkanCreateGraphics(&device->pipelines, pipeline, tag, handleOut);
}

static void DestroyPipeline(void* self, uint64_t handle)
{
    VulkanDevice* device = self;
    mrhiVulkanForgetPipeline(&device->pipelines, handle);
    mrhiVulkanRetireLater(&device->frames, mrhiVulkanRetiredPipeline, handle);
}

static bool ImportPipelineCache(void* self, const void* bytes, size_t size)
{
    VulkanDevice* device = self;
    return mrhiVulkanImportCache(&device->pipelines, bytes, size);
}

static size_t ExportPipelineCache(void* self, void* bytes, size_t capacity)
{
    VulkanDevice* device = self;
    return mrhiVulkanExportCache(&device->pipelines, bytes, capacity);
}

// Finished pipelines, then finished frames.
static size_t Poll(void* self, mrhiDriverEvent* events, size_t capacity)
{
    VulkanDevice* device = self;
    size_t moved = mrhiVulkanPollPipelines(&device->pipelines, events, capacity);
    return moved + mrhiVulkanPollFrames(&device->frames, events + moved, capacity - moved);
}

static mrhiResult SubmitFrame(void* self, const mrhiDriverFrame* frame, uint64_t tag)
{
    VulkanDevice* device = self;
    return mrhiVulkanSubmit(&device->frames, frame, tag);
}

static bool WaitFrame(void* self, uint64_t tag, uint64_t timeoutNs)
{
    VulkanDevice* device = self;
    return mrhiVulkanWaitFrame(&device->frames, tag, timeoutNs);
}

static const mrhiDeviceDriverVtable s_vtable = {
    .spiVersion = MRHI_SPI_VERSION,
    .size = sizeof(mrhiDeviceDriverVtable),
    .destroy = Destroy,
    .createSampler = CreateSampler,
    .destroySampler = DestroySampler,
    .createBuffer = CreateBuffer,
    .destroyBuffer = DestroyBuffer,
    .createTexture = CreateTexture,
    .destroyTexture = DestroyTexture,
    .createView = CreateView,
    .destroyView = DestroyView,
    .configureSurface = ConfigureSurface,
    .unconfigureSurface = UnconfigureSurface,
    .createShader = CreateShader,
    .destroyShader = DestroyShader,
    .createComputePipeline = CreateCompute,
    .createGraphicsPipeline = CreateGraphics,
    .destroyPipeline = DestroyPipeline,
    .lossReport = LossReport,
    .acquireImage = AcquireImage,
    .releaseImage = ReleaseImage,
    .createQuerySet = CreateQuerySet,
    .destroyQuerySet = DestroyQuerySet,
    .createHeap = CreateHeap,
    .destroyHeap = DestroyHeap,
    .writeHeapEntry = WriteHeapEntry,
    .writeHeapSampler = WriteHeapSampler,
    .timestampPeriod = TimestampPeriod,
    .importPipelineCache = ImportPipelineCache,
    .exportPipelineCache = ExportPipelineCache,
    .textureMemory = TextureMemory,
    .bufferMemory = BufferMemory,
    .submitFrame = SubmitFrame,
    .poll = Poll,
    .waitFrame = WaitFrame,
};

// Reads what the device keeps of its physical device: the timestamp
// period, the granularity, the lazily allocated types, the depth and
// stencil format.
static void ReadPhysical(VulkanDevice* device, VkPhysicalDeviceMemoryProperties* memoryOut)
{
    const mrhiVulkan* vulkan = device->vulkan;
    VkPhysicalDeviceProperties2 properties = {.sType =
                                                  VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    vulkan->vkGetPhysicalDeviceProperties2(device->physical, &properties);
    device->timestampPeriod = (double)properties.properties.limits.timestampPeriod;
    device->granularity = properties.properties.limits.bufferImageGranularity;
    device->objects.maxAnisotropy = properties.properties.limits.maxSamplerAnisotropy;
    device->pipelines.properties = properties.properties;
    device->atom = properties.properties.limits.nonCoherentAtomSize;
    *memoryOut = (VkPhysicalDeviceMemoryProperties){0};
    vulkan->vkGetPhysicalDeviceMemoryProperties(device->physical, memoryOut);
    for (uint32_t i = 0; i < memoryOut->memoryTypeCount; ++i)
    {
        bool lazy = (memoryOut->memoryTypes[i].propertyFlags &
                     VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) != 0;
        device->lazyTypes |= lazy ? 1u << i : 0u;
    }
    device->depthStencil = mrhiVulkanDepthStencil(vulkan, device->physical);
}

// Makes the device, its queue and its timeline.
static VkResult Open(VulkanDevice* device, const mrhiDeviceDef* def)
{
    uint32_t family = mrhiVulkanQueueFamily(device->vulkan, device->physical);
    device->family = family;
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    Enabled enabled;
    Enable(&def->features, &enabled);
    // Presenting needs VK_KHR_swapchain, and a swapchain whose images
    // take their sRGB twin's views VK_KHR_swapchain_mutable_format.
    static const char* const s_wanted[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                                           VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME};
    uint32_t offered =
        mrhiVulkanExtensions(device->vulkan, &device->allocator, device->physical, s_wanted, 2);
    // The mutable format extension needs the swapchain.
    offered = (offered & 1u) != 0 ? offered : 0;
    const char* names[3];
    uint32_t count = 0;
    for (uint32_t i = 0; i < 2; ++i)
    {
        if ((offered >> i & 1u) != 0)
        {
            names[count++] = s_wanted[i];
        }
    }
    // Heterogeneous heaps are granted only where mutable descriptors are
    // offered.
    if (def->features.bindlessHeterogeneous)
    {
        names[count++] = VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME;
    }
    device->swapchains.mutableFormat = (offered & 2u) != 0;
    const VkDeviceCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &enabled.features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue,
        .enabledExtensionCount = count,
        .ppEnabledExtensionNames = names,
    };
    VkResult result =
        device->vulkan->vkCreateDevice(device->physical, &info, nullptr, &device->device);
    if (result != VK_SUCCESS)
    {
        return result;
    }
    if (!mrhiLoadVulkanDevice(device->vulkan, device->device, (offered & 1u) != 0, &device->api))
    {
        PFN_vkDestroyDevice destroy = device->api.vkDestroyDevice;
        if (destroy != nullptr)
        {
            destroy(device->device, nullptr);
        }
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    device->api.vkGetDeviceQueue(device->device, family, 0, &device->queue);
    mrhiVulkanName(&device->api, device->device, VK_OBJECT_TYPE_DEVICE,
                   MRHI_VULKAN_DISPATCHABLE(device->device), def->label, def->labelLength);
    const VkSemaphoreTypeCreateInfo timeline = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
    };
    const VkSemaphoreCreateInfo semaphore = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &timeline,
    };
    result = device->api.vkCreateSemaphore(device->device, &semaphore, nullptr, &device->timeline);
    if (result != VK_SUCCESS)
    {
        device->api.vkDestroyDevice(device->device, nullptr);
    }
    return result;
}

// Where the device's tables lie in its one block.
typedef struct Layout
{
    mrhiLayout layout;
    size_t pools;
    size_t blocks;
    size_t nodes;
    size_t buffers;
    size_t textures;
    size_t views;
    size_t samplers;
    size_t querySets;
    size_t shaders;
    size_t pipelines;
    size_t heaps;
    size_t pending;
    size_t pendingHandles;
    size_t constants;
    // The tables' free slot links, one after another.
    size_t slots;
    // The frame slots, their transients and readback ranges, and the
    // retiring objects.
    size_t frames;
    size_t images;
    size_t buffers2;
    size_t own;
    size_t readbacks;
    size_t frameViews;
    uint32_t viewLimit;
    size_t retire;
    uint32_t retireCount;
    // The swapchains, and room for one frame's surface images.
    size_t swapchains;
    size_t waits;
    size_t signals;
    size_t presentChains;
    size_t presentImages;
    size_t presentWaits;
    size_t presentResults;
    size_t presentSlots;
    uint32_t blockCount;
    uint32_t nodeCount;
} Layout;

static Layout LayoutOf(const mrhiDeviceDef* def)
{
    const mrhiDeviceLimits* limits = &def->deviceLimits;
    uint32_t slots = def->limits.framesInFlight;
    Layout at = {.layout = {.size = sizeof(VulkanDevice)}};
    mrhiVulkanMemoryCounts(limits->buffers + limits->textures, &at.blockCount, &at.nodeCount);
    mrhiLayout* layout = &at.layout;
    at.pools = mrhiLayoutAdd(layout, 2 * VK_MAX_MEMORY_TYPES, sizeof(mrhiVulkanPool),
                             alignof(mrhiVulkanPool));
    at.blocks =
        mrhiLayoutAdd(layout, at.blockCount, sizeof(mrhiVulkanBlock), alignof(mrhiVulkanBlock));
    at.nodes = mrhiLayoutAdd(layout, at.nodeCount, sizeof(mrhiTlsfNode), alignof(mrhiTlsfNode));
    at.buffers =
        mrhiLayoutAdd(layout, limits->buffers, sizeof(mrhiVulkanBuffer), alignof(mrhiVulkanBuffer));
    at.textures = mrhiLayoutAdd(layout, limits->textures, sizeof(mrhiVulkanTexture),
                                alignof(mrhiVulkanTexture));
    at.views = mrhiLayoutAdd(layout, limits->views, sizeof(VkImageView), alignof(VkImageView));
    at.samplers = mrhiLayoutAdd(layout, limits->samplers, sizeof(VkSampler), alignof(VkSampler));
    at.querySets = mrhiLayoutAdd(layout, limits->querySets, sizeof(mrhiVulkanQuerySet),
                                 alignof(mrhiVulkanQuerySet));
    at.shaders =
        mrhiLayoutAdd(layout, limits->shaders, sizeof(VkShaderModule), alignof(VkShaderModule));
    at.pipelines = mrhiLayoutAdd(layout, limits->pipelines, sizeof(mrhiVulkanPipeline),
                                 alignof(mrhiVulkanPipeline));
    at.heaps =
        mrhiLayoutAdd(layout, limits->heaps, sizeof(mrhiVulkanHeap), alignof(mrhiVulkanHeap));
    at.pending =
        mrhiLayoutAdd(layout, limits->pipelines, sizeof(mrhiDriverEvent), alignof(mrhiDriverEvent));
    at.pendingHandles =
        mrhiLayoutAdd(layout, limits->pipelines, sizeof(uint64_t), alignof(uint64_t));
    at.constants =
        mrhiLayoutAdd(layout, 1, sizeof(mrhiVulkanConstants), alignof(mrhiVulkanConstants));
    size_t links = (size_t)limits->buffers + limits->textures + limits->views + limits->samplers +
                   limits->querySets + limits->shaders + limits->pipelines + limits->heaps;
    at.slots = mrhiLayoutAdd(layout, links, sizeof(uint32_t), alignof(uint32_t));
    at.frames = mrhiLayoutAdd(layout, slots, sizeof(mrhiVulkanSlot), alignof(mrhiVulkanSlot));
    size_t transients = (size_t)slots * limits->frameResources;
    at.images = mrhiLayoutAdd(layout, transients, sizeof(VkImage), alignof(VkImage));
    at.buffers2 = mrhiLayoutAdd(layout, transients, sizeof(VkBuffer), alignof(VkBuffer));
    at.own = mrhiLayoutAdd(layout, transients, sizeof(VkDeviceMemory), alignof(VkDeviceMemory));
    // A texture binding takes a record at least, and a pass's targets
    // and resolves a view each.
    at.viewLimit = limits->frameCommandBytes / (uint32_t)sizeof(mrhiCommand) +
                   limits->framePasses * (2u * MRHI_COLOR_TARGETS + 1u);
    at.frameViews = mrhiLayoutAdd(layout, (size_t)slots * at.viewLimit, sizeof(VkImageView),
                                  alignof(VkImageView));
    at.readbacks = mrhiLayoutAdd(layout, (size_t)slots * limits->readbacks, sizeof(mrhiVulkanRange),
                                 alignof(mrhiVulkanRange));
    at.retireCount = limits->buffers + limits->textures + limits->views + limits->samplers +
                     limits->querySets + limits->pipelines + limits->heaps;
    at.retire =
        mrhiLayoutAdd(layout, at.retireCount, sizeof(mrhiVulkanRetire), alignof(mrhiVulkanRetire));
    uint32_t surfaces = limits->surfaces;
    at.swapchains =
        mrhiLayoutAdd(layout, surfaces, sizeof(mrhiVulkanSwapchain), alignof(mrhiVulkanSwapchain));
    at.waits = mrhiLayoutAdd(layout, surfaces, sizeof(VkSemaphoreSubmitInfo),
                             alignof(VkSemaphoreSubmitInfo));
    at.signals = mrhiLayoutAdd(layout, (size_t)surfaces + 1, sizeof(VkSemaphoreSubmitInfo),
                               alignof(VkSemaphoreSubmitInfo));
    at.presentChains =
        mrhiLayoutAdd(layout, surfaces, sizeof(VkSwapchainKHR), alignof(VkSwapchainKHR));
    at.presentImages = mrhiLayoutAdd(layout, surfaces, sizeof(uint32_t), alignof(uint32_t));
    at.presentWaits = mrhiLayoutAdd(layout, surfaces, sizeof(VkSemaphore), alignof(VkSemaphore));
    at.presentResults = mrhiLayoutAdd(layout, surfaces, sizeof(VkResult), alignof(VkResult));
    at.presentSlots = mrhiLayoutAdd(layout, surfaces, sizeof(uint32_t), alignof(uint32_t));
    return at;
}

// Sets up the memory and the object tables in the device's block, all
// entries empty.
static void PlaceTables(VulkanDevice* device, const Layout* at, const mrhiDeviceLimits* limits,
                        const VkPhysicalDeviceMemoryProperties* properties)
{
    unsigned char* block = (unsigned char*)device;
    memset(block + sizeof(VulkanDevice), 0, at->layout.size - sizeof(VulkanDevice));
    mrhiVulkanMemoryInit(&device->memory, &device->api, device->device, properties,
                         (mrhiVulkanPool*)(block + at->pools),
                         (mrhiVulkanBlock*)(block + at->blocks), at->blockCount,
                         (mrhiTlsfNode*)(block + at->nodes), at->nodeCount);
    mrhiVulkanObjects* objects = &device->objects;
    objects->api = &device->api;
    objects->device = device->device;
    objects->memory = &device->memory;
    objects->depthStencil = device->depthStencil;
    objects->buffers = (mrhiVulkanBuffer*)(block + at->buffers);
    objects->textures = (mrhiVulkanTexture*)(block + at->textures);
    objects->views = (VkImageView*)(block + at->views);
    objects->samplers = (VkSampler*)(block + at->samplers);
    objects->querySets = (mrhiVulkanQuerySet*)(block + at->querySets);
    uint32_t* slots = (uint32_t*)(block + at->slots);
    mrhiVulkanSlotsInit(&objects->bufferSlots, slots, limits->buffers);
    slots += limits->buffers;
    mrhiVulkanSlotsInit(&objects->textureSlots, slots, limits->textures);
    slots += limits->textures;
    mrhiVulkanSlotsInit(&objects->viewSlots, slots, limits->views);
    slots += limits->views;
    mrhiVulkanSlotsInit(&objects->samplerSlots, slots, limits->samplers);
    slots += limits->samplers;
    mrhiVulkanSlotsInit(&objects->querySetSlots, slots, limits->querySets);
    slots += limits->querySets;
    mrhiVulkanPipelines* pipelines = &device->pipelines;
    pipelines->api = &device->api;
    pipelines->device = device->device;
    pipelines->depthStencil = device->depthStencil;
    pipelines->shaders = (VkShaderModule*)(block + at->shaders);
    pipelines->pipelines = (mrhiVulkanPipeline*)(block + at->pipelines);
    pipelines->pending = (mrhiDriverEvent*)(block + at->pending);
    pipelines->pendingHandles = (uint64_t*)(block + at->pendingHandles);
    pipelines->constants = (mrhiVulkanConstants*)(block + at->constants);
    mrhiVulkanSlotsInit(&pipelines->shaderSlots, slots, limits->shaders);
    slots += limits->shaders;
    mrhiVulkanSlotsInit(&pipelines->pipelineSlots, slots, limits->pipelines);
    slots += limits->pipelines;
    mrhiVulkanHeaps* heaps = &device->heaps;
    heaps->api = &device->api;
    heaps->device = device->device;
    heaps->objects = objects;
    heaps->heaps = (mrhiVulkanHeap*)(block + at->heaps);
    mrhiVulkanSlotsInit(&heaps->heapSlots, slots, limits->heaps);
}

// Sets up the swapchains over their tables in the device's block.
static void PlaceSwapchains(VulkanDevice* device, const Layout* at, uint32_t surfaces)
{
    unsigned char* block = (unsigned char*)device;
    bool mutableFormat = device->swapchains.mutableFormat;
    device->swapchains = (mrhiVulkanSwapchains){
        .vulkan = device->vulkan,
        .api = &device->api,
        .physical = device->physical,
        .device = device->device,
        .queue = device->queue,
        .timeline = device->timeline,
        .mutableFormat = mutableFormat,
        .depthStencil = device->depthStencil,
        .slots = (mrhiVulkanSwapchain*)(block + at->swapchains),
        .capacity = surfaces,
        .waits = (VkSemaphoreSubmitInfo*)(block + at->waits),
        .signals = (VkSemaphoreSubmitInfo*)(block + at->signals),
        .presentChains = (VkSwapchainKHR*)(block + at->presentChains),
        .presentImages = (uint32_t*)(block + at->presentImages),
        .presentWaits = (VkSemaphore*)(block + at->presentWaits),
        .presentResults = (VkResult*)(block + at->presentResults),
        .presentSlots = (uint32_t*)(block + at->presentSlots),
    };
}

// Sets up the frames over their tables in the device's block.
static void PlaceFrames(VulkanDevice* device, const Layout* at, const mrhiDeviceDef* def)
{
    unsigned char* block = (unsigned char*)device;
    const mrhiDeviceLimits* limits = &def->deviceLimits;
    mrhiVulkanFrames* frames = &device->frames;
    *frames = (mrhiVulkanFrames){
        .api = &device->api,
        .device = device->device,
        .queue = device->queue,
        .timeline = device->timeline,
        .depthStencil = device->depthStencil,
        .memory = &device->memory.properties,
        .objects = &device->objects,
        .pipelines = &device->pipelines,
        .heaps = &device->heaps,
        .swapchains = &device->swapchains,
        .slots = (mrhiVulkanSlot*)(block + at->frames),
        .slotCount = def->limits.framesInFlight,
        .readbackLimit = limits->readbacks,
        .viewLimit = at->viewLimit,
        .atom = device->atom,
        .retire = (mrhiVulkanRetire*)(block + at->retire),
        .retireCapacity = at->retireCount,
    };
    for (uint32_t i = 0; i < frames->slotCount; ++i)
    {
        size_t first = (size_t)i * limits->frameResources;
        frames->slots[i] = (mrhiVulkanSlot){
            .images = (VkImage*)(block + at->images) + first,
            .buffers = (VkBuffer*)(block + at->buffers2) + first,
            .own = (VkDeviceMemory*)(block + at->own) + first,
            .readbacks = (mrhiVulkanRange*)(block + at->readbacks) + (size_t)i * limits->readbacks,
            .views = (VkImageView*)(block + at->frameViews) + (size_t)i * at->viewLimit,
        };
    }
}

// The def with room for twice the objects the core allows: a destroyed
// object keeps its slot until the next frame submitted finishes, so a
// program may replace every object once between submissions. False
// when the counts overflow.
static bool WithRetiring(const mrhiDeviceDef* def, mrhiDeviceDef* heldOut)
{
    *heldOut = *def;
    mrhiDeviceLimits* limits = &heldOut->deviceLimits;
    return !ckd_mul(&limits->buffers, limits->buffers, 2u) &&
           !ckd_mul(&limits->textures, limits->textures, 2u) &&
           !ckd_mul(&limits->views, limits->views, 2u) &&
           !ckd_mul(&limits->samplers, limits->samplers, 2u) &&
           !ckd_mul(&limits->querySets, limits->querySets, 2u) &&
           !ckd_mul(&limits->pipelines, limits->pipelines, 2u);
}

mrhiResult mrhiCreateVulkanDevice(const mrhiAllocator* allocator, const mrhiVulkan* vulkan,
                                  VkPhysicalDevice physical, const mrhiDeviceDef* asked,
                                  mrhiDeviceDriver* deviceOut)
{
    mrhiDeviceDef held;
    if (!WithRetiring(asked, &held))
    {
        return mrhi_errorCapacity;
    }
    const mrhiDeviceDef* def = &held;
    Layout at = LayoutOf(def);
    VulkanDevice* device = at.layout.overflow
                               ? nullptr
                               : mrhiAllocate(allocator, at.layout.size, alignof(VulkanDevice));
    if (device == nullptr)
    {
        return mrhi_errorCapacity;
    }
    *device = (VulkanDevice){
        .allocator = *allocator,
        .vulkan = vulkan,
        .physical = physical,
        .bytes = at.layout.size,
    };
    VkPhysicalDeviceMemoryProperties properties;
    ReadPhysical(device, &properties);
    VkResult result = Open(device, def);
    if (result != VK_SUCCESS)
    {
        mrhiRelease(allocator, device, at.layout.size, alignof(VulkanDevice));
        mrhiResult status = StatusOf(result);
        return status == mrhi_errorDeviceLost ? mrhi_errorPlatform : status;
    }
    PlaceTables(device, &at, &def->deviceLimits, &properties);
    PlaceSwapchains(device, &at, def->deviceLimits.surfaces);
    PlaceFrames(device, &at, def);
    // A device without bindless sampling has no heap layout.
    bool sampling = def->features.bindlessSampling;
    device->heaps.entries = sampling ? def->limits.heapSize : 0;
    device->heaps.samplers = sampling ? def->limits.samplerHeapSize : 0;
    device->heaps.heterogeneous = def->features.bindlessHeterogeneous;
    mrhiResult status = mrhiVulkanHeapsInit(&device->heaps);
    device->pipelines.heapLayout = device->heaps.layout;
    if (status == mrhi_success)
    {
        status = mrhiVulkanPipelinesInit(&device->pipelines);
    }
    if (status == mrhi_success)
    {
        status = mrhiVulkanFramesInit(&device->frames, device->family, &def->deviceLimits);
    }
    if (status != mrhi_success)
    {
        Destroy(device);
        return status;
    }
    *deviceOut = (mrhiDeviceDriver){.vtable = &s_vtable, .self = device};
    return mrhi_success;
}
