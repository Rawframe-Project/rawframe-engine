// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Vulkan physical devices as adapters (mrhi-0003), read through the Vulkan
// 1.1, 1.2 and 1.3 feature and property structs, with each feature,
// limit and format capability as the contract's Vulkan row maps it.

#include "vulkan_adapter.h"

#include "capabilities_core.h"
#include "invariant.h"
#include "stage_limits.h"

#include <string.h>

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

// Each contract format's Vulkan format, as its mapping row names it;
// the depth and stencil format's first choice. The formats run from 1
// to MRHI_KNOWN_FORMATS.
static_assert(mrhi_formatAstc12x12UnormSrgb == MRHI_KNOWN_FORMATS, "formats are dense");
static const VkFormat s_formats[MRHI_KNOWN_FORMATS + 1] = {
    [mrhi_formatRgba8Unorm] = VK_FORMAT_R8G8B8A8_UNORM,
    [mrhi_formatRgba8UnormSrgb] = VK_FORMAT_R8G8B8A8_SRGB,
    [mrhi_formatBgra8Unorm] = VK_FORMAT_B8G8R8A8_UNORM,
    [mrhi_formatBgra8UnormSrgb] = VK_FORMAT_B8G8R8A8_SRGB,
    [mrhi_formatR8Unorm] = VK_FORMAT_R8_UNORM,
    [mrhi_formatRg8Unorm] = VK_FORMAT_R8G8_UNORM,
    [mrhi_formatR16Float] = VK_FORMAT_R16_SFLOAT,
    [mrhi_formatRg16Float] = VK_FORMAT_R16G16_SFLOAT,
    [mrhi_formatRgba16Float] = VK_FORMAT_R16G16B16A16_SFLOAT,
    [mrhi_formatR32Float] = VK_FORMAT_R32_SFLOAT,
    [mrhi_formatRg32Float] = VK_FORMAT_R32G32_SFLOAT,
    [mrhi_formatRgba32Float] = VK_FORMAT_R32G32B32A32_SFLOAT,
    [mrhi_formatR32Uint] = VK_FORMAT_R32_UINT,
    [mrhi_formatR32Sint] = VK_FORMAT_R32_SINT,
    [mrhi_formatRgb10a2Unorm] = VK_FORMAT_A2B10G10R10_UNORM_PACK32,
    [mrhi_formatRg11b10Ufloat] = VK_FORMAT_B10G11R11_UFLOAT_PACK32,
    [mrhi_formatDepth32Float] = VK_FORMAT_D32_SFLOAT,
    [mrhi_formatDepthStencil] = VK_FORMAT_D24_UNORM_S8_UINT,
    [mrhi_formatBc1RgbaUnorm] = VK_FORMAT_BC1_RGBA_UNORM_BLOCK,
    [mrhi_formatBc1RgbaUnormSrgb] = VK_FORMAT_BC1_RGBA_SRGB_BLOCK,
    [mrhi_formatBc2RgbaUnorm] = VK_FORMAT_BC2_UNORM_BLOCK,
    [mrhi_formatBc2RgbaUnormSrgb] = VK_FORMAT_BC2_SRGB_BLOCK,
    [mrhi_formatBc3RgbaUnorm] = VK_FORMAT_BC3_UNORM_BLOCK,
    [mrhi_formatBc3RgbaUnormSrgb] = VK_FORMAT_BC3_SRGB_BLOCK,
    [mrhi_formatBc4RUnorm] = VK_FORMAT_BC4_UNORM_BLOCK,
    [mrhi_formatBc4RSnorm] = VK_FORMAT_BC4_SNORM_BLOCK,
    [mrhi_formatBc5RgUnorm] = VK_FORMAT_BC5_UNORM_BLOCK,
    [mrhi_formatBc5RgSnorm] = VK_FORMAT_BC5_SNORM_BLOCK,
    [mrhi_formatBc6hRgbUfloat] = VK_FORMAT_BC6H_UFLOAT_BLOCK,
    [mrhi_formatBc6hRgbFloat] = VK_FORMAT_BC6H_SFLOAT_BLOCK,
    [mrhi_formatBc7RgbaUnorm] = VK_FORMAT_BC7_UNORM_BLOCK,
    [mrhi_formatBc7RgbaUnormSrgb] = VK_FORMAT_BC7_SRGB_BLOCK,
    [mrhi_formatEtc2Rgb8Unorm] = VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK,
    [mrhi_formatEtc2Rgb8UnormSrgb] = VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK,
    [mrhi_formatEtc2Rgb8a1Unorm] = VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK,
    [mrhi_formatEtc2Rgb8a1UnormSrgb] = VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK,
    [mrhi_formatEtc2Rgba8Unorm] = VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK,
    [mrhi_formatEtc2Rgba8UnormSrgb] = VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK,
    [mrhi_formatEacR11Unorm] = VK_FORMAT_EAC_R11_UNORM_BLOCK,
    [mrhi_formatEacR11Snorm] = VK_FORMAT_EAC_R11_SNORM_BLOCK,
    [mrhi_formatEacRg11Unorm] = VK_FORMAT_EAC_R11G11_UNORM_BLOCK,
    [mrhi_formatEacRg11Snorm] = VK_FORMAT_EAC_R11G11_SNORM_BLOCK,
    [mrhi_formatAstc4x4Unorm] = VK_FORMAT_ASTC_4x4_UNORM_BLOCK,
    [mrhi_formatAstc4x4UnormSrgb] = VK_FORMAT_ASTC_4x4_SRGB_BLOCK,
    [mrhi_formatAstc5x4Unorm] = VK_FORMAT_ASTC_5x4_UNORM_BLOCK,
    [mrhi_formatAstc5x4UnormSrgb] = VK_FORMAT_ASTC_5x4_SRGB_BLOCK,
    [mrhi_formatAstc5x5Unorm] = VK_FORMAT_ASTC_5x5_UNORM_BLOCK,
    [mrhi_formatAstc5x5UnormSrgb] = VK_FORMAT_ASTC_5x5_SRGB_BLOCK,
    [mrhi_formatAstc6x5Unorm] = VK_FORMAT_ASTC_6x5_UNORM_BLOCK,
    [mrhi_formatAstc6x5UnormSrgb] = VK_FORMAT_ASTC_6x5_SRGB_BLOCK,
    [mrhi_formatAstc6x6Unorm] = VK_FORMAT_ASTC_6x6_UNORM_BLOCK,
    [mrhi_formatAstc6x6UnormSrgb] = VK_FORMAT_ASTC_6x6_SRGB_BLOCK,
    [mrhi_formatAstc8x5Unorm] = VK_FORMAT_ASTC_8x5_UNORM_BLOCK,
    [mrhi_formatAstc8x5UnormSrgb] = VK_FORMAT_ASTC_8x5_SRGB_BLOCK,
    [mrhi_formatAstc8x6Unorm] = VK_FORMAT_ASTC_8x6_UNORM_BLOCK,
    [mrhi_formatAstc8x6UnormSrgb] = VK_FORMAT_ASTC_8x6_SRGB_BLOCK,
    [mrhi_formatAstc8x8Unorm] = VK_FORMAT_ASTC_8x8_UNORM_BLOCK,
    [mrhi_formatAstc8x8UnormSrgb] = VK_FORMAT_ASTC_8x8_SRGB_BLOCK,
    [mrhi_formatAstc10x5Unorm] = VK_FORMAT_ASTC_10x5_UNORM_BLOCK,
    [mrhi_formatAstc10x5UnormSrgb] = VK_FORMAT_ASTC_10x5_SRGB_BLOCK,
    [mrhi_formatAstc10x6Unorm] = VK_FORMAT_ASTC_10x6_UNORM_BLOCK,
    [mrhi_formatAstc10x6UnormSrgb] = VK_FORMAT_ASTC_10x6_SRGB_BLOCK,
    [mrhi_formatAstc10x8Unorm] = VK_FORMAT_ASTC_10x8_UNORM_BLOCK,
    [mrhi_formatAstc10x8UnormSrgb] = VK_FORMAT_ASTC_10x8_SRGB_BLOCK,
    [mrhi_formatAstc10x10Unorm] = VK_FORMAT_ASTC_10x10_UNORM_BLOCK,
    [mrhi_formatAstc10x10UnormSrgb] = VK_FORMAT_ASTC_10x10_SRGB_BLOCK,
    [mrhi_formatAstc12x10Unorm] = VK_FORMAT_ASTC_12x10_UNORM_BLOCK,
    [mrhi_formatAstc12x10UnormSrgb] = VK_FORMAT_ASTC_12x10_SRGB_BLOCK,
    [mrhi_formatAstc12x12Unorm] = VK_FORMAT_ASTC_12x12_UNORM_BLOCK,
    [mrhi_formatAstc12x12UnormSrgb] = VK_FORMAT_ASTC_12x12_SRGB_BLOCK,
};

// What a device reports, read once for a description.
typedef struct DeviceFacts
{
    VkPhysicalDeviceProperties2 properties;
    VkPhysicalDeviceVulkan11Properties properties11;
    VkPhysicalDeviceVulkan12Properties properties12;
    VkPhysicalDeviceVulkan13Properties properties13;
    VkPhysicalDeviceFeatures2 features;
    VkPhysicalDeviceVulkan11Features features11;
    VkPhysicalDeviceVulkan12Features features12;
    VkPhysicalDeviceVulkan13Features features13;
    // Read only when the device offers VK_EXT_mutable_descriptor_type.
    VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT mutableType;
    // The queue family with graphics and compute, or UINT32_MAX.
    uint32_t family;
    uint32_t familyTimestampBits;
} DeviceFacts;

// The first queue family with graphics and compute, and its timestamp
// bits.
static void FindFamily(const mrhiVulkan* vulkan, VkPhysicalDevice device, DeviceFacts* facts)
{
    VkQueueFamilyProperties families[16];
    uint32_t count = 16;
    vulkan->vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families);
    facts->family = UINT32_MAX;
    for (uint32_t i = 0; i < count && facts->family == UINT32_MAX; ++i)
    {
        VkQueueFlags wanted = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        if ((families[i].queueFlags & wanted) == wanted)
        {
            facts->family = i;
            facts->familyTimestampBits = families[i].timestampValidBits;
        }
    }
}

// Reads the device's properties, and its features when it is Vulkan
// 1.3, whose structs a 1.2 device may not fill: false below 1.3.
static bool ReadFacts(const mrhiVulkan* vulkan, const mrhiAllocator* allocator,
                      VkPhysicalDevice device, DeviceFacts* facts)
{
    *facts = (DeviceFacts){
        .properties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2},
        .properties11 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES},
        .properties12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES},
        .properties13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES},
        .features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2},
        .features11 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES},
        .features12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES},
        .features13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES},
        .mutableType = {.sType =
                            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT},
    };
    VkPhysicalDeviceProperties2 core = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    vulkan->vkGetPhysicalDeviceProperties2(device, &core);
    if (core.properties.apiVersion < VK_API_VERSION_1_3)
    {
        return false;
    }
    facts->properties.pNext = &facts->properties11;
    facts->properties11.pNext = &facts->properties12;
    facts->properties12.pNext = &facts->properties13;
    vulkan->vkGetPhysicalDeviceProperties2(device, &facts->properties);
    facts->features.pNext = &facts->features11;
    facts->features11.pNext = &facts->features12;
    facts->features12.pNext = &facts->features13;
    const char* mutableName = VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME;
    if (mrhiVulkanExtensions(vulkan, allocator, device, &mutableName, 1) != 0)
    {
        facts->features13.pNext = &facts->mutableType;
    }
    vulkan->vkGetPhysicalDeviceFeatures2(device, &facts->features);
    FindFamily(vulkan, device, facts);
    return true;
}

// Whether a device has the Vulkan 1.0 features the contract's floor
// uses, which every device enables.
static bool HasFloorFeatures(const VkPhysicalDeviceFeatures* core)
{
    return core->fullDrawIndexUint32 && core->imageCubeArray && core->independentBlend &&
           core->sampleRateShading && core->depthBiasClamp && core->fragmentStoresAndAtomics &&
           core->samplerAnisotropy && core->shaderStorageImageExtendedFormats;
}

// Whether a device meets the driver's floor beyond its version.
static bool MeetsFloor(const DeviceFacts* facts)
{
    return facts->family != UINT32_MAX && HasFloorFeatures(&facts->features.features) &&
           facts->features13.dynamicRendering && facts->features13.synchronization2 &&
           facts->features12.timelineSemaphore && facts->features12.bufferDeviceAddress &&
           facts->features12.descriptorIndexing;
}

static VkFormatFeatureFlags OptimalFeatures(const mrhiVulkan* vulkan, VkPhysicalDevice device,
                                            VkFormat format)
{
    VkFormatProperties properties = {0};
    vulkan->vkGetPhysicalDeviceFormatProperties(device, format, &properties);
    return properties.optimalTilingFeatures;
}

static mrhiAdapterKind KindOf(VkPhysicalDeviceType type)
{
    switch (type)
    {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return mrhi_adapterDiscrete;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return mrhi_adapterIntegrated;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return mrhi_adapterVirtual;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return mrhi_adapterSoftware;
    default:
        return mrhi_adapterUnknown;
    }
}

static mrhiAdapterInfo InfoOf(const VkPhysicalDeviceProperties* properties)
{
    mrhiAdapterInfo info = {
        .driver = mrhi_driverVulkan,
        .kind = KindOf(properties->deviceType),
        .vendorId = properties->vendorID,
        .deviceId = properties->deviceID,
    };
    const char* end = memchr(properties->deviceName, 0, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE);
    size_t length =
        end != nullptr ? (size_t)(end - properties->deviceName) : VK_MAX_PHYSICAL_DEVICE_NAME_SIZE;
    length = length < MRHI_ADAPTER_NAME_BYTES ? length : MRHI_ADAPTER_NAME_BYTES;
    memcpy(info.name, properties->deviceName, length);
    info.nameLength = (uint32_t)length;
    return info;
}

// Whether every format has all the wanted optimal tiling features.
static bool AllHave(const mrhiVulkan* vulkan, VkPhysicalDevice device, const VkFormat* formats,
                    size_t count, VkFormatFeatureFlags wanted)
{
    for (size_t i = 0; i < count; ++i)
    {
        if ((OptimalFeatures(vulkan, device, formats[i]) & wanted) != wanted)
        {
            return false;
        }
    }
    return true;
}

// Whether a device has what a heap of sampled images and samplers
// needs: runtime arrays, partially bound and update after bind bindings
// updatable while pending, non-uniform indexing, and a set after the
// four tables.
static bool HasBindlessSampling(const DeviceFacts* facts)
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
static bool HasBindlessHeterogeneous(const DeviceFacts* facts)
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

static mrhiFeatures FeaturesOf(const mrhiVulkan* vulkan, VkPhysicalDevice device,
                               const DeviceFacts* facts)
{
    const VkPhysicalDeviceFeatures* core = &facts->features.features;
    const VkPhysicalDeviceVulkan11Properties* subgroup = &facts->properties11;
    static const VkFormat float32[] = {VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT,
                                       VK_FORMAT_R32G32B32A32_SFLOAT};
    static const VkFormat rg11b10[] = {VK_FORMAT_B10G11R11_UFLOAT_PACK32};
    return (mrhiFeatures){
        .timestampQuery = facts->properties.properties.limits.timestampComputeAndGraphics &&
                          facts->familyTimestampBits > 0,
        .pipelineStatisticsQuery = core->pipelineStatisticsQuery,
        .textureCompressionBc = core->textureCompressionBC,
        .textureCompressionEtc2 = core->textureCompressionETC2,
        .textureCompressionAstc = core->textureCompressionASTC_LDR,
        .float32Filterable =
            AllHave(vulkan, device, float32, 3, VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT),
        .rg11b10Renderable = AllHave(vulkan, device, rg11b10, 1,
                                     VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                                         VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT),
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
static uint32_t HeapSize(const DeviceFacts* facts)
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
static uint32_t SamplerHeapSize(const DeviceFacts* facts)
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

static mrhiLimits LimitsOf(const DeviceFacts* facts)
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

bool mrhiDescribeVulkanAdapter(const mrhiVulkan* vulkan, const mrhiAllocator* allocator,
                               VkPhysicalDevice device, mrhiDriverAdapter* adapterOut)
{
    DeviceFacts facts;
    if (!ReadFacts(vulkan, allocator, device, &facts) || !MeetsFloor(&facts))
    {
        return false;
    }
    *adapterOut = (mrhiDriverAdapter){
        .handle = (uint64_t)(uintptr_t)device,
        .info = InfoOf(&facts.properties.properties),
        .features = FeaturesOf(vulkan, device, &facts),
        .limits = LimitsOf(&facts),
    };
    return true;
}

VkFormat mrhiVulkanDepthStencil(const mrhiVulkan* vulkan, VkPhysicalDevice device)
{
    bool d24 = (OptimalFeatures(vulkan, device, VK_FORMAT_D24_UNORM_S8_UINT) &
                VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
    return d24 ? VK_FORMAT_D24_UNORM_S8_UINT : VK_FORMAT_D32_SFLOAT_S8_UINT;
}

VkFormat mrhiVulkanFormat(mrhiFormat format, VkFormat depthStencil)
{
    if (!mrhiIsFormatKnown(format))
    {
        return VK_FORMAT_UNDEFINED;
    }
    return format == mrhi_formatDepthStencil ? depthStencil : s_formats[format];
}

uint32_t mrhiVulkanQueueFamily(const mrhiVulkan* vulkan, VkPhysicalDevice device)
{
    DeviceFacts facts;
    FindFamily(vulkan, device, &facts);
    return facts.family;
}

// The sample counts an optimal 2D image of the format has for a usage,
// as the contract's mask: 1, 2 and 4.
static uint8_t SampleCounts(const mrhiVulkan* vulkan, VkPhysicalDevice device, VkFormat format,
                            VkImageUsageFlags usage)
{
    VkImageFormatProperties properties = {0};
    if (vulkan->vkGetPhysicalDeviceImageFormatProperties(device, format, VK_IMAGE_TYPE_2D,
                                                         VK_IMAGE_TILING_OPTIMAL, usage, 0,
                                                         &properties) != VK_SUCCESS)
    {
        return 0;
    }
    return (uint8_t)(properties.sampleCounts &
                     (VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT));
}

void mrhiGetVulkanFormatCaps(const mrhiVulkan* vulkan, VkPhysicalDevice device, mrhiFormat format,
                             mrhiFormatCaps* capsOut)
{
    *capsOut = (mrhiFormatCaps){0};
    VkFormat vulkanFormat = mrhiVulkanFormat(format, mrhiVulkanDepthStencil(vulkan, device));
    if (vulkanFormat == VK_FORMAT_UNDEFINED)
    {
        return;
    }
    VkFormatFeatureFlags features = OptimalFeatures(vulkan, device, vulkanFormat);
    bool color = (features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0;
    bool depth = (features & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
    capsOut->sampling = (features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
    capsOut->filtering = (features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
    capsOut->rendering = color || depth;
    capsOut->blending = (features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) != 0;
    capsOut->storage = (features & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0;
    VkImageUsageFlags usage = capsOut->sampling ? VK_IMAGE_USAGE_SAMPLED_BIT : 0;
    usage |= color ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0;
    usage |= depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : 0;
    capsOut->sampleCounts = usage != 0 ? SampleCounts(vulkan, device, vulkanFormat, usage) : 0;
}

// Reads the instance's or a device's extensions into a list of the
// exact count: its length, or 0 with nothing allocated.
static uint32_t ReadExtensions(const mrhiVulkan* vulkan, const mrhiAllocator* allocator,
                               VkPhysicalDevice device, VkExtensionProperties** listOut)
{
    uint32_t count = 0;
    VkResult result =
        device == VK_NULL_HANDLE
            ? vulkan->vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr)
            : vulkan->vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    VkExtensionProperties* list =
        result == VK_SUCCESS && count > 0
            ? mrhiAllocate(allocator, count * sizeof(VkExtensionProperties),
                           alignof(VkExtensionProperties))
            : nullptr;
    if (list == nullptr)
    {
        return 0;
    }
    uint32_t read = count;
    result = device == VK_NULL_HANDLE
                 ? vulkan->vkEnumerateInstanceExtensionProperties(nullptr, &read, list)
                 : vulkan->vkEnumerateDeviceExtensionProperties(device, nullptr, &read, list);
    if (result != VK_SUCCESS && result != VK_INCOMPLETE)
    {
        mrhiRelease(allocator, list, count * sizeof(VkExtensionProperties),
                    alignof(VkExtensionProperties));
        return 0;
    }
    *listOut = list;
    return count;
}

uint32_t mrhiVulkanExtensions(const mrhiVulkan* vulkan, const mrhiAllocator* allocator,
                              VkPhysicalDevice device, const char* const* names, uint32_t count)
{
    MRHI_ASSERT(count <= 32);
    VkExtensionProperties* list = nullptr;
    uint32_t listed = ReadExtensions(vulkan, allocator, device, &list);
    uint32_t found = 0;
    for (uint32_t i = 0; i < listed; ++i)
    {
        for (uint32_t n = 0; n < count; ++n)
        {
            found |= strcmp(list[i].extensionName, names[n]) == 0 ? 1u << n : 0u;
        }
    }
    if (list != nullptr)
    {
        mrhiRelease(allocator, list, listed * sizeof(VkExtensionProperties),
                    alignof(VkExtensionProperties));
    }
    return found;
}
