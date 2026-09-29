// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Buffers and textures as Vulkan creates them (mrhi-0003).

#include "vulkan_resource.h"

#include "capabilities_core.h"
#include "vulkan_adapter.h"

VkImageUsageFlags mrhiVulkanImageUsage(mrhiTextureUsage usage, mrhiFormat format)
{
    bool depth = mrhiFormatHasDepth(format) || mrhiFormatHasStencil(format);
    VkImageUsageFlags flags = 0;
    flags |= (usage & mrhi_textureSampled) != 0 ? VK_IMAGE_USAGE_SAMPLED_BIT : 0;
    flags |= (usage & mrhi_textureStorage) != 0 ? VK_IMAGE_USAGE_STORAGE_BIT : 0;
    if ((usage & mrhi_textureRenderTarget) != 0)
    {
        flags |= depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                       : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    flags |= (usage & mrhi_textureTransient) != 0 ? VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT : 0;
    flags |= (usage & mrhi_textureCopySource) != 0 ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0;
    flags |= (usage & mrhi_textureCopyDestination) != 0 ? VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0;
    return flags;
}

void mrhiVulkanImageOf(const mrhiTextureDef* def, VkFormat depthStencil, mrhiVulkanImage* imageOut)
{
    bool cube = def->kind == mrhi_textureCube || def->kind == mrhi_textureCubeArray;
    bool volume = def->kind == mrhi_texture3d;
    *imageOut = (mrhiVulkanImage){
        .info =
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .flags = (cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0u) |
                         (volume && (def->usage & mrhi_textureRenderTarget) != 0
                              ? VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT
                              : 0u),
                .imageType = volume ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D,
                .format = mrhiVulkanFormat(def->format, depthStencil),
                .extent = {def->width, def->height, volume ? def->depthOrLayers : 1},
                .mipLevels = def->mipLevels,
                .arrayLayers = volume ? 1 : def->depthOrLayers,
                .samples = (VkSampleCountFlagBits)def->sampleCount,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = mrhiVulkanImageUsage(def->usage, def->format),
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            },
        .list = {.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO},
    };
    // Views in other formats make the image mutable, with its formats
    // listed so that the driver may still compress it.
    uint32_t count = 0;
    imageOut->formats[count++] = imageOut->info.format;
    for (uint32_t i = 0; i < MRHI_VIEW_FORMATS; ++i)
    {
        if (def->viewFormats[i] != mrhi_formatNone && def->viewFormats[i] != def->format)
        {
            imageOut->formats[count++] = mrhiVulkanFormat(def->viewFormats[i], depthStencil);
        }
    }
    if (count > 1)
    {
        imageOut->info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
        imageOut->list.viewFormatCount = count;
        imageOut->list.pViewFormats = imageOut->formats;
        imageOut->info.pNext = &imageOut->list;
    }
}

VkBufferCreateInfo mrhiVulkanBufferOf(const mrhiBufferDef* def)
{
    mrhiBufferUsage usage = def->usage;
    VkBufferUsageFlags flags = 0;
    flags |= (usage & mrhi_bufferVertex) != 0 ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT : 0;
    flags |= (usage & mrhi_bufferIndex) != 0 ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT : 0;
    flags |= (usage & mrhi_bufferUniform) != 0 ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT : 0;
    flags |= (usage & mrhi_bufferStorage) != 0 ? VK_BUFFER_USAGE_STORAGE_BUFFER_BIT : 0;
    flags |= (usage & mrhi_bufferIndirect) != 0 ? VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT : 0;
    flags |= (usage & mrhi_bufferCopySource) != 0 ? VK_BUFFER_USAGE_TRANSFER_SRC_BIT : 0;
    flags |= (usage & (mrhi_bufferCopyDestination | mrhi_bufferQueryResolve)) != 0
                 ? VK_BUFFER_USAGE_TRANSFER_DST_BIT
                 : 0;
    return (VkBufferCreateInfo){
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = def->size,
        .usage = flags,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
}
