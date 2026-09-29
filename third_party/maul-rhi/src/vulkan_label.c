// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Labels on Vulkan (vulkan_label.h).

#include "vulkan_label.h"

#include "invariant.h"

#include "maul-rhi/base.h"

#include <string.h>

// A label with its NUL.
typedef struct Text
{
    char bytes[MRHI_LABEL_BYTES + 1];
} Text;

static Text TextOf(const char* label, size_t labelLength)
{
    MRHI_ASSERT(labelLength <= MRHI_LABEL_BYTES);
    Text text;
    memcpy(text.bytes, label, labelLength);
    text.bytes[labelLength] = '\0';
    return text;
}

void mrhiVulkanName(const mrhiVulkanDevice* api, VkDevice device, VkObjectType type,
                    uint64_t handle, const char* label, size_t labelLength)
{
    if (api->vkSetDebugUtilsObjectNameEXT == nullptr || labelLength == 0 || handle == 0)
    {
        return;
    }
    Text text = TextOf(label, labelLength);
    const VkDebugUtilsObjectNameInfoEXT info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = type,
        .objectHandle = handle,
        .pObjectName = text.bytes,
    };
    // A name only helps tools; one the driver refuses is left out.
    (void)api->vkSetDebugUtilsObjectNameEXT(device, &info);
}

static VkDebugUtilsLabelEXT LabelOf(const Text* text)
{
    return (VkDebugUtilsLabelEXT){
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pLabelName = text->bytes,
    };
}

void mrhiVulkanBeginLabel(const mrhiVulkanDevice* api, VkCommandBuffer commands, const char* label,
                          size_t labelLength)
{
    if (api->vkCmdBeginDebugUtilsLabelEXT != nullptr)
    {
        Text text = TextOf(label, labelLength);
        VkDebugUtilsLabelEXT info = LabelOf(&text);
        api->vkCmdBeginDebugUtilsLabelEXT(commands, &info);
    }
}

void mrhiVulkanEndLabel(const mrhiVulkanDevice* api, VkCommandBuffer commands)
{
    if (api->vkCmdEndDebugUtilsLabelEXT != nullptr)
    {
        api->vkCmdEndDebugUtilsLabelEXT(commands);
    }
}

void mrhiVulkanInsertLabel(const mrhiVulkanDevice* api, VkCommandBuffer commands, const char* label,
                           size_t labelLength)
{
    if (api->vkCmdInsertDebugUtilsLabelEXT != nullptr)
    {
        Text text = TextOf(label, labelLength);
        VkDebugUtilsLabelEXT info = LabelOf(&text);
        api->vkCmdInsertDebugUtilsLabelEXT(commands, &info);
    }
}
