// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Labels on Vulkan (mrhi-0003): object names and command labels through
// VK_EXT_debug_utils where the instance has it, and nothing where it has
// not. Labels are UTF-8 without NUL, at most MRHI_LABEL_BYTES; Vulkan's
// are NUL-terminated, so each is copied first.

#ifndef MAUL_RHI_SRC_VULKAN_LABEL_H
#define MAUL_RHI_SRC_VULKAN_LABEL_H

#include "vulkan_api.h"

#include <stddef.h>
#include <stdint.h>

// Names an object; a label of 0 bytes names nothing. A non-dispatchable
// handle is passed as its bits, a dispatchable one as its pointer's.
void mrhiVulkanName(const mrhiVulkanDevice* api, VkDevice device, VkObjectType type,
                    uint64_t handle, const char* label, size_t labelLength);

// The bits of a non-dispatchable handle, which is a pointer where the
// Khronos headers say so and a 64-bit integer elsewhere. A dispatchable
// handle is always a pointer.
#if VK_USE_64_BIT_PTR_DEFINES == 1
#define MRHI_VULKAN_HANDLE(handle) ((uint64_t)(uintptr_t)(handle))
#else
#define MRHI_VULKAN_HANDLE(handle) ((uint64_t)(handle))
#endif
#define MRHI_VULKAN_DISPATCHABLE(handle) ((uint64_t)(uintptr_t)(handle))

// Opens and closes a labelled region of a command buffer, and marks a
// point in it.
void mrhiVulkanBeginLabel(const mrhiVulkanDevice* api, VkCommandBuffer commands, const char* label,
                          size_t labelLength);
void mrhiVulkanEndLabel(const mrhiVulkanDevice* api, VkCommandBuffer commands);
void mrhiVulkanInsertLabel(const mrhiVulkanDevice* api, VkCommandBuffer commands, const char* label,
                           size_t labelLength);

#endif // MAUL_RHI_SRC_VULKAN_LABEL_H
