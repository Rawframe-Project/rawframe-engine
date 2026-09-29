// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Buffers and textures as Vulkan creates them (mrhi-0003): their create
// infos from the defs the core has checked, as the contract's Vulkan
// rows map them.

#ifndef MAUL_RHI_SRC_VULKAN_RESOURCE_H
#define MAUL_RHI_SRC_VULKAN_RESOURCE_H

#include "vulkan_api.h"

#include "maul-rhi/resources.h"

// An image's create info with the format list it points to.
typedef struct mrhiVulkanImage
{
    VkImageCreateInfo info;
    VkImageFormatListCreateInfo list;
    VkFormat formats[1 + MRHI_VIEW_FORMATS];
} mrhiVulkanImage;

// Fills an image's create info from a texture def, with the device's
// depth and stencil format. The info points into the struct, which must
// not move before it is used.
void mrhiVulkanImageOf(const mrhiTextureDef* def, VkFormat depthStencil, mrhiVulkanImage* imageOut);

// The image usage a texture usage is for a format.
VkImageUsageFlags mrhiVulkanImageUsage(mrhiTextureUsage usage, mrhiFormat format);

// A buffer's create info from a buffer def.
VkBufferCreateInfo mrhiVulkanBufferOf(const mrhiBufferDef* def);

#endif // MAUL_RHI_SRC_VULKAN_RESOURCE_H
