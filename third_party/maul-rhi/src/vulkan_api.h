// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Vulkan loader, opened at run time (mrhi-0003): the Khronos headers with
// no prototypes, and the functions the driver reaches through
// vkGetInstanceProcAddr.

#ifndef MAUL_RHI_SRC_VULKAN_API_H
#define MAUL_RHI_SRC_VULKAN_API_H

#define VK_NO_PROTOTYPES
#include <stdbool.h>
#include <vulkan/vulkan_core.h>

// The functions reached without an instance.
#define MRHI_VULKAN_GLOBAL(X)                                                                      \
    X(vkCreateInstance)                                                                            \
    X(vkEnumerateInstanceVersion)                                                                  \
    X(vkEnumerateInstanceExtensionProperties)

// The functions reached through an instance.
#define MRHI_VULKAN_INSTANCE(X)                                                                    \
    X(vkDestroyInstance)                                                                           \
    X(vkEnumeratePhysicalDevices)                                                                  \
    X(vkEnumerateDeviceExtensionProperties)                                                        \
    X(vkGetPhysicalDeviceProperties2)                                                              \
    X(vkGetPhysicalDeviceFeatures2)                                                                \
    X(vkGetPhysicalDeviceQueueFamilyProperties)                                                    \
    X(vkGetPhysicalDeviceFormatProperties)                                                         \
    X(vkGetPhysicalDeviceImageFormatProperties)                                                    \
    X(vkGetPhysicalDeviceMemoryProperties)                                                         \
    X(vkCreateDevice)                                                                              \
    X(vkGetDeviceProcAddr)

// The functions reached through a device, past the loader's dispatch.
#define MRHI_VULKAN_DEVICE(X)                                                                      \
    X(vkDestroyDevice)                                                                             \
    X(vkDeviceWaitIdle)                                                                            \
    X(vkQueueWaitIdle)                                                                             \
    X(vkGetDeviceQueue)                                                                            \
    X(vkCreateSemaphore)                                                                           \
    X(vkDestroySemaphore)                                                                          \
    X(vkGetSemaphoreCounterValue)                                                                  \
    X(vkWaitSemaphores)                                                                            \
    X(vkGetDeviceBufferMemoryRequirements)                                                         \
    X(vkGetDeviceImageMemoryRequirements)                                                          \
    X(vkAllocateMemory)                                                                            \
    X(vkFreeMemory)                                                                                \
    X(vkGetBufferMemoryRequirements2)                                                              \
    X(vkGetImageMemoryRequirements2)                                                               \
    X(vkBindBufferMemory)                                                                          \
    X(vkBindImageMemory)                                                                           \
    X(vkCreateBuffer)                                                                              \
    X(vkDestroyBuffer)                                                                             \
    X(vkCreateImage)                                                                               \
    X(vkDestroyImage)                                                                              \
    X(vkCreateImageView)                                                                           \
    X(vkDestroyImageView)                                                                          \
    X(vkCreateSampler)                                                                             \
    X(vkDestroySampler)                                                                            \
    X(vkCreateQueryPool)                                                                           \
    X(vkDestroyQueryPool)                                                                          \
    X(vkCreateShaderModule)                                                                        \
    X(vkDestroyShaderModule)                                                                       \
    X(vkCreateDescriptorSetLayout)                                                                 \
    X(vkDestroyDescriptorSetLayout)                                                                \
    X(vkCreatePipelineLayout)                                                                      \
    X(vkDestroyPipelineLayout)                                                                     \
    X(vkCreateComputePipelines)                                                                    \
    X(vkCreateGraphicsPipelines)                                                                   \
    X(vkDestroyPipeline)                                                                           \
    X(vkCreatePipelineCache)                                                                       \
    X(vkDestroyPipelineCache)                                                                      \
    X(vkGetPipelineCacheData)                                                                      \
    X(vkCreateCommandPool)                                                                         \
    X(vkDestroyCommandPool)                                                                        \
    X(vkResetCommandPool)                                                                          \
    X(vkAllocateCommandBuffers)                                                                    \
    X(vkBeginCommandBuffer)                                                                        \
    X(vkEndCommandBuffer)                                                                          \
    X(vkQueueSubmit2)                                                                              \
    X(vkMapMemory)                                                                                 \
    X(vkInvalidateMappedMemoryRanges)                                                              \
    X(vkCmdPipelineBarrier2)                                                                       \
    X(vkCmdCopyBuffer)                                                                             \
    X(vkCmdCopyBufferToImage)                                                                      \
    X(vkCmdCopyImageToBuffer)                                                                      \
    X(vkCmdCopyImage)                                                                              \
    X(vkCreateDescriptorPool)                                                                      \
    X(vkDestroyDescriptorPool)                                                                     \
    X(vkResetDescriptorPool)                                                                       \
    X(vkAllocateDescriptorSets)                                                                    \
    X(vkUpdateDescriptorSets)                                                                      \
    X(vkCmdBeginRendering)                                                                         \
    X(vkCmdEndRendering)                                                                           \
    X(vkCmdBindPipeline)                                                                           \
    X(vkCmdBindDescriptorSets)                                                                     \
    X(vkCmdPushConstants)                                                                          \
    X(vkCmdSetViewport)                                                                            \
    X(vkCmdSetScissor)                                                                             \
    X(vkCmdSetBlendConstants)                                                                      \
    X(vkCmdSetStencilReference)                                                                    \
    X(vkCmdBindVertexBuffers2)                                                                     \
    X(vkCmdBindIndexBuffer)                                                                        \
    X(vkCmdDraw)                                                                                   \
    X(vkCmdDrawIndexed)                                                                            \
    X(vkCmdDispatch)                                                                               \
    X(vkCmdDrawIndirect)                                                                           \
    X(vkCmdDrawIndexedIndirect)                                                                    \
    X(vkCmdDispatchIndirect)                                                                       \
    X(vkCmdDrawIndirectCount)                                                                      \
    X(vkCmdDrawIndexedIndirectCount)                                                               \
    X(vkCmdResetQueryPool)                                                                         \
    X(vkCmdBeginQuery)                                                                             \
    X(vkCmdEndQuery)                                                                               \
    X(vkCmdWriteTimestamp2)                                                                        \
    X(vkCmdCopyQueryPoolResults)                                                                   \
    X(vkCmdFillBuffer)

// The functions of VK_KHR_surface, read when the instance has it.
#define MRHI_VULKAN_SURFACE(X)                                                                     \
    X(vkDestroySurfaceKHR)                                                                         \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)                                                        \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)                                                   \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)                                                        \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR)

// The functions of VK_KHR_swapchain, read when the device has it.
#define MRHI_VULKAN_SWAPCHAIN(X)                                                                   \
    X(vkCreateSwapchainKHR)                                                                        \
    X(vkDestroySwapchainKHR)                                                                       \
    X(vkGetSwapchainImagesKHR)                                                                     \
    X(vkAcquireNextImageKHR)                                                                       \
    X(vkQueuePresentKHR)

// The functions of VK_EXT_debug_utils, read through the instance when it
// has the extension and copied into each device's table; NULL without
// it.
#define MRHI_VULKAN_DEBUG(X)                                                                       \
    X(vkSetDebugUtilsObjectNameEXT)                                                                \
    X(vkCmdBeginDebugUtilsLabelEXT)                                                                \
    X(vkCmdEndDebugUtilsLabelEXT)                                                                  \
    X(vkCmdInsertDebugUtilsLabelEXT)

#define MRHI_VULKAN_FIELD(name) PFN_##name name;

// The loader's library and the functions read from it.
typedef struct mrhiVulkan
{
    void* library;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
    // The instance the functions were read for.
    VkInstance instance;
    MRHI_VULKAN_GLOBAL(MRHI_VULKAN_FIELD)
    MRHI_VULKAN_INSTANCE(MRHI_VULKAN_FIELD)
    // Whether the instance has VK_KHR_surface, and its functions.
    bool surfaces;
    MRHI_VULKAN_SURFACE(MRHI_VULKAN_FIELD)
    MRHI_VULKAN_DEBUG(MRHI_VULKAN_FIELD)
} mrhiVulkan;

// A device's functions.
typedef struct mrhiVulkanDevice
{
    MRHI_VULKAN_DEVICE(MRHI_VULKAN_FIELD)
    MRHI_VULKAN_SWAPCHAIN(MRHI_VULKAN_FIELD)
    MRHI_VULKAN_DEBUG(MRHI_VULKAN_FIELD)
} mrhiVulkanDevice;

// Opens the loader and reads the global functions: false, with nothing
// open, when there is no loader or it lacks one of them.
bool mrhiOpenVulkan(mrhiVulkan* vulkan);

// Reads the global functions through a program's vkGetInstanceProcAddr,
// opening no loader, for an instance made elsewhere (mrhi-0018): false, with
// nothing kept, when one is missing.
bool mrhiAdoptVulkan(mrhiVulkan* vulkan, PFN_vkGetInstanceProcAddr entry);

// Reads the instance's functions: false when one is missing.
bool mrhiLoadVulkanInstance(mrhiVulkan* vulkan, VkInstance instance);

// Reads the functions of VK_KHR_surface, which the instance has: false
// when one is missing.
bool mrhiLoadVulkanSurface(mrhiVulkan* vulkan, VkInstance instance);

// Reads the functions of VK_EXT_debug_utils, which the instance has:
// false, with none kept, when one is missing.
bool mrhiLoadVulkanDebug(mrhiVulkan* vulkan, VkInstance instance);

// Reads a device's functions, and those of VK_KHR_swapchain when it has
// it, and copies the instance's debug functions: false when one is
// missing.
bool mrhiLoadVulkanDevice(const mrhiVulkan* vulkan, VkDevice device, bool swapchain,
                          mrhiVulkanDevice* functions);

// Closes the loader, if one was opened.
void mrhiCloseVulkan(mrhiVulkan* vulkan);

#endif // MAUL_RHI_SRC_VULKAN_API_H
