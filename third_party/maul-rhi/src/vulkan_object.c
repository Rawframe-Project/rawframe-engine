// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Vulkan device's objects (vulkan_object.h), as the contract's Vulkan
// rows map their defs. Until frames run on the device nothing on the
// GPU can name an object, so destruction frees at once.

#include "vulkan_object.h"

#include "capabilities_core.h"
#include "chain.h"
#include "invariant.h"
#include "vulkan_adapter.h"
#include "vulkan_label.h"
#include "vulkan_resource.h"

#include "maul-rhi/vulkan.h"

void mrhiVulkanSlotsInit(mrhiVulkanSlots* slots, uint32_t* next, uint32_t capacity)
{
    for (uint32_t i = 0; i < capacity; ++i)
    {
        next[i] = i + 1 < capacity ? i + 2 : 0;
    }
    *slots = (mrhiVulkanSlots){.next = next, .head = capacity > 0 ? 1 : 0, .capacity = capacity};
}

uint32_t mrhiVulkanTakeSlot(mrhiVulkanSlots* slots)
{
    uint32_t handle = slots->head;
    if (handle != 0)
    {
        slots->head = slots->next[handle - 1];
    }
    return handle;
}

void mrhiVulkanGiveSlot(mrhiVulkanSlots* slots, uint64_t handle)
{
    MRHI_ASSERT(handle != 0 && handle <= slots->capacity);
    slots->next[handle - 1] = slots->head;
    slots->head = (uint32_t)handle;
}

static mrhiResult StatusOf(VkResult result)
{
    return result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY
               ? mrhi_errorCapacity
               : mrhi_errorPlatform;
}

mrhiResult mrhiVulkanCreateBuffer(mrhiVulkanObjects* objects, const mrhiBufferDef* def,
                                  uint64_t* handleOut)
{
    uint32_t handle = mrhiVulkanTakeSlot(&objects->bufferSlots);
    if (handle == 0)
    {
        return mrhi_errorCapacity;
    }
    mrhiVulkanBuffer* made = &objects->buffers[handle - 1];
    const VkBufferCreateInfo info = mrhiVulkanBufferOf(def);
    VkResult result = objects->api->vkCreateBuffer(objects->device, &info, nullptr, &made->buffer);
    mrhiResult status = result == VK_SUCCESS ? mrhiVulkanPlace(objects->memory, made->buffer,
                                                               VK_NULL_HANDLE, &made->allocation)
                                             : StatusOf(result);
    if (status != mrhi_success)
    {
        if (result == VK_SUCCESS)
        {
            objects->api->vkDestroyBuffer(objects->device, made->buffer, nullptr);
        }
        mrhiVulkanGiveSlot(&objects->bufferSlots, handle);
        return status;
    }
    mrhiVulkanName(objects->api, objects->device, VK_OBJECT_TYPE_BUFFER,
                   MRHI_VULKAN_HANDLE(made->buffer), def->label, def->labelLength);
    *handleOut = handle;
    return mrhi_success;
}

void mrhiVulkanDestroyBuffer(mrhiVulkanObjects* objects, uint64_t handle)
{
    mrhiVulkanBuffer* buffer = &objects->buffers[handle - 1];
    objects->api->vkDestroyBuffer(objects->device, buffer->buffer, nullptr);
    mrhiVulkanRelease(objects->memory, &buffer->allocation);
    *buffer = (mrhiVulkanBuffer){0};
    mrhiVulkanGiveSlot(&objects->bufferSlots, handle);
}

mrhiResult mrhiVulkanCreateTexture(mrhiVulkanObjects* objects, const mrhiTextureDef* def,
                                   uint64_t* handleOut)
{
    uint32_t handle = mrhiVulkanTakeSlot(&objects->textureSlots);
    if (handle == 0)
    {
        return mrhi_errorCapacity;
    }
    mrhiVulkanTexture* made = &objects->textures[handle - 1];
    const mrhiTextureVulkanAdopt* adopt =
        (const mrhiTextureVulkanAdopt*)mrhiFindStruct(def->next, mrhi_structTextureVulkanAdopt);
    if (adopt != nullptr)
    {
        *made = (mrhiVulkanTexture){.image = (VkImage)adopt->image, .adopted = true};
        mrhiVulkanName(objects->api, objects->device, VK_OBJECT_TYPE_IMAGE,
                       MRHI_VULKAN_HANDLE(made->image), def->label, def->labelLength);
        *handleOut = handle;
        return mrhi_success;
    }
    mrhiVulkanImage image;
    mrhiVulkanImageOf(def, objects->depthStencil, &image);
    VkResult result =
        objects->api->vkCreateImage(objects->device, &image.info, nullptr, &made->image);
    mrhiResult status = result == VK_SUCCESS ? mrhiVulkanPlace(objects->memory, VK_NULL_HANDLE,
                                                               made->image, &made->allocation)
                                             : StatusOf(result);
    if (status != mrhi_success)
    {
        if (result == VK_SUCCESS)
        {
            objects->api->vkDestroyImage(objects->device, made->image, nullptr);
        }
        mrhiVulkanGiveSlot(&objects->textureSlots, handle);
        return status;
    }
    mrhiVulkanName(objects->api, objects->device, VK_OBJECT_TYPE_IMAGE,
                   MRHI_VULKAN_HANDLE(made->image), def->label, def->labelLength);
    *handleOut = handle;
    return mrhi_success;
}

void mrhiVulkanDestroyTexture(mrhiVulkanObjects* objects, uint64_t handle)
{
    mrhiVulkanTexture* texture = &objects->textures[handle - 1];
    if (!texture->adopted)
    {
        objects->api->vkDestroyImage(objects->device, texture->image, nullptr);
        mrhiVulkanRelease(objects->memory, &texture->allocation);
    }
    *texture = (mrhiVulkanTexture){0};
    mrhiVulkanGiveSlot(&objects->textureSlots, handle);
}

static VkImageViewType ViewTypeOf(mrhiTextureKind kind)
{
    static const VkImageViewType types[] = {
        [mrhi_texture2d] = VK_IMAGE_VIEW_TYPE_2D,
        [mrhi_texture2dArray] = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
        [mrhi_textureCube] = VK_IMAGE_VIEW_TYPE_CUBE,
        [mrhi_textureCubeArray] = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY,
        [mrhi_texture3d] = VK_IMAGE_VIEW_TYPE_3D,
    };
    MRHI_ASSERT(kind <= mrhi_texture3d);
    return types[kind];
}

static VkImageAspectFlags AspectOf(mrhiTextureAspect aspect, mrhiFormat format)
{
    switch (aspect)
    {
    case mrhi_aspectDepthOnly:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    case mrhi_aspectStencilOnly:
        return VK_IMAGE_ASPECT_STENCIL_BIT;
    default:
        break;
    }
    VkImageAspectFlags flags = 0;
    flags |= mrhiFormatHasDepth(format) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0;
    flags |= mrhiFormatHasStencil(format) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0;
    return flags != 0 ? flags : VK_IMAGE_ASPECT_COLOR_BIT;
}

mrhiResult mrhiVulkanCreateView(mrhiVulkanObjects* objects, uint64_t texture,
                                const mrhiViewDef* def, uint64_t* handleOut)
{
    uint32_t handle = mrhiVulkanTakeSlot(&objects->viewSlots);
    if (handle == 0)
    {
        return mrhi_errorCapacity;
    }
    const VkImageViewUsageCreateInfo usage = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
        .usage = mrhiVulkanImageUsage(def->usage, def->format),
    };
    const VkImageViewCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = &usage,
        .image = objects->textures[texture - 1].image,
        .viewType = ViewTypeOf(def->kind),
        .format = mrhiVulkanFormat(def->format, objects->depthStencil),
        .subresourceRange =
            {
                .aspectMask = AspectOf(def->aspect, def->format),
                .baseMipLevel = def->baseMip,
                .levelCount = def->mipCount,
                .baseArrayLayer = def->baseLayer,
                .layerCount = def->layerCount,
            },
    };
    VkResult result = objects->api->vkCreateImageView(objects->device, &info, nullptr,
                                                      &objects->views[handle - 1]);
    if (result != VK_SUCCESS)
    {
        mrhiVulkanGiveSlot(&objects->viewSlots, handle);
        return StatusOf(result);
    }
    mrhiVulkanName(objects->api, objects->device, VK_OBJECT_TYPE_IMAGE_VIEW,
                   MRHI_VULKAN_HANDLE(objects->views[handle - 1]), def->label, def->labelLength);
    *handleOut = handle;
    return mrhi_success;
}

void mrhiVulkanDestroyView(mrhiVulkanObjects* objects, uint64_t handle)
{
    objects->api->vkDestroyImageView(objects->device, objects->views[handle - 1], nullptr);
    objects->views[handle - 1] = VK_NULL_HANDLE;
    mrhiVulkanGiveSlot(&objects->viewSlots, handle);
}

static VkSamplerAddressMode AddressOf(mrhiAddressMode mode)
{
    static const VkSamplerAddressMode modes[] = {
        [mrhi_addressClampToEdge] = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        [mrhi_addressRepeat] = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        [mrhi_addressMirrorRepeat] = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
    };
    MRHI_ASSERT(mode <= mrhi_addressMirrorRepeat);
    return modes[mode];
}

mrhiResult mrhiVulkanCreateSampler(mrhiVulkanObjects* objects, const mrhiSamplerDef* def,
                                   uint64_t* handleOut)
{
    uint32_t handle = mrhiVulkanTakeSlot(&objects->samplerSlots);
    if (handle == 0)
    {
        return mrhi_errorCapacity;
    }
    float anisotropy = (float)def->maxAnisotropy;
    anisotropy = anisotropy < objects->maxAnisotropy ? anisotropy : objects->maxAnisotropy;
    // The contract's compare functions follow Vulkan's order from 1.
    static_assert(mrhi_compareNever - 1 == VK_COMPARE_OP_NEVER &&
                      mrhi_compareAlways - 1 == VK_COMPARE_OP_ALWAYS,
                  "compare functions line up");
    const VkSamplerCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = def->magFilter == mrhi_filterLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
        .minFilter = def->minFilter == mrhi_filterLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
        .mipmapMode = def->mipFilter == mrhi_filterLinear ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                                          : VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = AddressOf(def->addressU),
        .addressModeV = AddressOf(def->addressV),
        .addressModeW = AddressOf(def->addressW),
        .anisotropyEnable = anisotropy > 1.0f,
        .maxAnisotropy = anisotropy > 1.0f ? anisotropy : 1.0f,
        .compareEnable = def->compare != mrhi_compareNone,
        .compareOp = def->compare != mrhi_compareNone ? (VkCompareOp)(def->compare - 1)
                                                      : VK_COMPARE_OP_NEVER,
        .minLod = def->lodMin,
        .maxLod = def->lodMax,
        .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
    };
    VkResult result = objects->api->vkCreateSampler(objects->device, &info, nullptr,
                                                    &objects->samplers[handle - 1]);
    if (result != VK_SUCCESS)
    {
        mrhiVulkanGiveSlot(&objects->samplerSlots, handle);
        return StatusOf(result);
    }
    mrhiVulkanName(objects->api, objects->device, VK_OBJECT_TYPE_SAMPLER,
                   MRHI_VULKAN_HANDLE(objects->samplers[handle - 1]), def->label, def->labelLength);
    *handleOut = handle;
    return mrhi_success;
}

void mrhiVulkanDestroySampler(mrhiVulkanObjects* objects, uint64_t handle)
{
    objects->api->vkDestroySampler(objects->device, objects->samplers[handle - 1], nullptr);
    objects->samplers[handle - 1] = VK_NULL_HANDLE;
    mrhiVulkanGiveSlot(&objects->samplerSlots, handle);
}

mrhiResult mrhiVulkanCreateQuerySet(mrhiVulkanObjects* objects, const mrhiQuerySetDef* def,
                                    uint64_t* handleOut)
{
    uint32_t handle = mrhiVulkanTakeSlot(&objects->querySetSlots);
    if (handle == 0)
    {
        return mrhi_errorCapacity;
    }
    static const VkQueryType types[] = {
        [mrhi_queryOcclusion] = VK_QUERY_TYPE_OCCLUSION,
        [mrhi_queryTimestamp] = VK_QUERY_TYPE_TIMESTAMP,
        [mrhi_queryPipelineStatistics] = VK_QUERY_TYPE_PIPELINE_STATISTICS,
    };
    // Every counter, in bit order, which is the order a resolve writes
    // them in (mrhi-0023).
    const VkQueryPoolCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = types[def->type],
        .queryCount = def->count,
        .pipelineStatistics =
            def->type == mrhi_queryPipelineStatistics ? (1u << MRHI_STATISTICS_COUNTERS) - 1 : 0,
    };
    mrhiVulkanQuerySet* set = &objects->querySets[handle - 1];
    *set = (mrhiVulkanQuerySet){.count = def->count, .type = def->type};
    VkResult result = objects->api->vkCreateQueryPool(objects->device, &info, nullptr, &set->pool);
    if (result != VK_SUCCESS)
    {
        mrhiVulkanGiveSlot(&objects->querySetSlots, handle);
        return StatusOf(result);
    }
    mrhiVulkanName(objects->api, objects->device, VK_OBJECT_TYPE_QUERY_POOL,
                   MRHI_VULKAN_HANDLE(set->pool), def->label, def->labelLength);
    *handleOut = handle;
    return mrhi_success;
}

void mrhiVulkanDestroyQuerySet(mrhiVulkanObjects* objects, uint64_t handle)
{
    mrhiVulkanQuerySet* set = &objects->querySets[handle - 1];
    objects->api->vkDestroyQueryPool(objects->device, set->pool, nullptr);
    set->pool = VK_NULL_HANDLE;
    mrhiVulkanGiveSlot(&objects->querySetSlots, handle);
}
