// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A device def's VkDeviceCreateInfo (vulkan_recipe.h).

#include "vulkan_recipe.h"

#include "allocator.h"
#include "chain.h"
#include "vulkan_adapter.h"

#include <stdalign.h>
#include <stddef.h>
#include <string.h>

// The floor's features, and the granted ones as the contract's Vulkan
// rows name them.
static void Enable(const mrhiFeatures* granted, mrhiVulkanEnabled* enabled)
{
    *enabled = (mrhiVulkanEnabled){
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

// ORs the VkBool32 members of a feature struct from an offset on.
static void Merge(void* into, const void* from, size_t offset, size_t size)
{
    VkBool32* to = (VkBool32*)((unsigned char*)into + offset);
    const VkBool32* add = (const VkBool32*)((const unsigned char*)from + offset);
    for (size_t i = 0; i < (size - offset) / sizeof(VkBool32); ++i)
    {
        to[i] = to[i] | add[i];
    }
}

// Merges the def's features chain (mrhi-0018) into the recipe's: the
// core feature structs' true members; false for a struct of another
// type, which the library cannot place without knowing its size.
static bool MergeFeatures(const VkBaseInStructure* chain, mrhiVulkanEnabled* enabled)
{
    for (const VkBaseInStructure* node = chain; node != nullptr; node = node->pNext)
    {
        switch (node->sType)
        {
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2:
            Merge(&enabled->features, node, offsetof(VkPhysicalDeviceFeatures2, features),
                  sizeof(VkPhysicalDeviceFeatures2));
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES:
            Merge(&enabled->features11, node,
                  offsetof(VkPhysicalDeviceVulkan11Features, storageBuffer16BitAccess),
                  sizeof(VkPhysicalDeviceVulkan11Features));
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES:
            Merge(&enabled->features12, node,
                  offsetof(VkPhysicalDeviceVulkan12Features, samplerMirrorClampToEdge),
                  sizeof(VkPhysicalDeviceVulkan12Features));
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES:
            Merge(&enabled->features13, node,
                  offsetof(VkPhysicalDeviceVulkan13Features, robustImageAccess),
                  sizeof(VkPhysicalDeviceVulkan13Features));
            break;
        default:
            return false;
        }
    }
    return true;
}

bool mrhiVulkanDefFeaturesKnown(const mrhiDeviceDef* def)
{
    const mrhiDeviceVulkanExtensions* extra = (const mrhiDeviceVulkanExtensions*)mrhiFindStruct(
        def->next, mrhi_structDeviceVulkanExtensions);
    mrhiVulkanEnabled scratch = {0};
    return extra == nullptr || MergeFeatures(extra->features, &scratch);
}

bool mrhiVulkanDefExtensions(const mrhiDeviceDef* def, const char** namesOut, size_t* bytesOut)
{
    *namesOut = nullptr;
    *bytesOut = 0;
    for (const mrhiChain* node = def->next; node != nullptr; node = node->next)
    {
        if (node->type == mrhi_structDeviceVulkanExtensions)
        {
            const mrhiDeviceVulkanExtensions* extensions = (const mrhiDeviceVulkanExtensions*)node;
            *namesOut = extensions->extensions;
            *bytesOut = extensions->extensionsLength;
        }
    }
    const char* names = *namesOut;
    size_t bytes = *bytesOut;
    // Every name is not empty and ends by a NUL within the bytes.
    for (size_t at = 0; at < bytes;)
    {
        const char* end = names != nullptr ? memchr(names + at, 0, bytes - at) : nullptr;
        if (end == nullptr || end == names + at)
        {
            return false;
        }
        at = (size_t)(end - names) + 1;
    }
    return names != nullptr || bytes == 0;
}

// The names' count in a well-formed list.
static uint32_t CountNames(const char* names, size_t bytes)
{
    uint32_t count = 0;
    for (size_t at = 0; at < bytes; ++at)
    {
        count += names[at] == 0 ? 1u : 0u;
    }
    return count;
}

// Names the recipe's extensions: the swapchain's where the instance
// presents, mutable descriptors for heterogeneous heaps, and the def's
// own, copied and each named once. Their count.
static uint32_t NameExtensions(const mrhiVulkan* vulkan, const mrhiAllocator* allocator,
                               VkPhysicalDevice physical, const mrhiDeviceDef* def, const char* own,
                               mrhiVulkanRecipe* recipe)
{
    // Presenting needs VK_KHR_swapchain, and a swapchain whose images
    // take their sRGB twin's views VK_KHR_swapchain_mutable_format.
    static const char* const s_wanted[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                                           VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME};
    uint32_t offered = mrhiVulkanExtensions(vulkan, allocator, physical, s_wanted, 2);
    // The swapchain needs the instance's surfaces, and the mutable format
    // extension the swapchain.
    offered = vulkan->surfaces && (offered & 1u) != 0 ? offered : 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < 2; ++i)
    {
        if ((offered >> i & 1u) != 0)
        {
            recipe->names[count++] = s_wanted[i];
        }
    }
    // Heterogeneous heaps are granted only where mutable descriptors are
    // offered.
    if (def->features.bindlessHeterogeneous)
    {
        recipe->names[count++] = VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME;
    }
    size_t ownBytes = recipe->ownBytes;
    if (ownBytes > 0 && own != nullptr)
    {
        memcpy(recipe->ownNames, own, ownBytes);
    }
    // The library's own extensions are named once.
    for (size_t at = 0; at < ownBytes; at += strlen(recipe->ownNames + at) + 1)
    {
        const char* name = recipe->ownNames + at;
        bool listed = false;
        for (uint32_t i = 0; i < count; ++i)
        {
            listed = listed || strcmp(recipe->names[i], name) == 0;
        }
        recipe->names[count] = name;
        count += listed ? 0u : 1u;
    }
    recipe->swapchain = (offered & 1u) != 0;
    recipe->mutableFormat = (offered & 2u) != 0;
    return count;
}

mrhiResult mrhiVulkanComposeDevice(const mrhiVulkan* vulkan, const mrhiAllocator* allocator,
                                   VkPhysicalDevice physical, const mrhiDeviceDef* def,
                                   mrhiVulkanRecipe* recipeOut)
{
    mrhiVulkanRecipe* recipe = recipeOut;
    *recipe = (mrhiVulkanRecipe){.priority = 1.0f};
    const char* own = nullptr;
    size_t ownBytes = 0;
    if (!mrhiVulkanDefExtensions(def, &own, &ownBytes))
    {
        return mrhi_errorInvalid;
    }
    uint32_t ownCount = CountNames(own, ownBytes);
    // The swapchain, its mutable format, mutable descriptors, and the
    // def's own.
    uint32_t most = 3 + ownCount;
    recipe->names =
        (const char**)mrhiAllocate(allocator, most * sizeof(const char*), alignof(const char*));
    recipe->nameCount = most;
    recipe->ownNames = ownBytes > 0 ? mrhiAllocate(allocator, ownBytes, 1) : nullptr;
    recipe->ownBytes = ownBytes;
    if (recipe->names == nullptr || (ownBytes > 0 && recipe->ownNames == nullptr))
    {
        mrhiVulkanEndRecipe(allocator, recipe);
        return mrhi_errorCapacity;
    }
    uint32_t family = mrhiVulkanQueueFamily(vulkan, physical);
    recipe->queue = (VkDeviceQueueCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family,
        .queueCount = 1,
        .pQueuePriorities = &recipe->priority,
    };
    Enable(&def->features, &recipe->enabled);
    const mrhiDeviceVulkanExtensions* extra = (const mrhiDeviceVulkanExtensions*)mrhiFindStruct(
        def->next, mrhi_structDeviceVulkanExtensions);
    if (extra != nullptr && !MergeFeatures(extra->features, &recipe->enabled))
    {
        mrhiVulkanEndRecipe(allocator, recipe);
        return mrhi_errorUnsupported;
    }
    uint32_t count = NameExtensions(vulkan, allocator, physical, def, own, recipe);
    recipe->info = (VkDeviceCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &recipe->enabled.features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &recipe->queue,
        .enabledExtensionCount = count,
        .ppEnabledExtensionNames = recipe->names,
    };
    return mrhi_success;
}

void mrhiVulkanEndRecipe(const mrhiAllocator* allocator, mrhiVulkanRecipe* recipe)
{
    if (recipe->names != nullptr)
    {
        mrhiRelease(allocator, (void*)recipe->names, recipe->nameCount * sizeof(const char*),
                    alignof(const char*));
    }
    if (recipe->ownNames != nullptr)
    {
        mrhiRelease(allocator, recipe->ownNames, recipe->ownBytes, 1);
    }
    recipe->names = nullptr;
    recipe->ownNames = nullptr;
    recipe->info.ppEnabledExtensionNames = nullptr;
}
