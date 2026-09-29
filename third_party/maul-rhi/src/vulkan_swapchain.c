// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Vulkan device's swapchains (vulkan_swapchain.h). A handle is the
// slot's index plus one, with the slot's generation in its upper half.

#include "vulkan_swapchain.h"

#include "capabilities_core.h"
#include "invariant.h"
#include "vulkan_adapter.h"
#include "vulkan_resource.h"
#include "vulkan_surface.h"

static mrhiVulkanSwapchain* Find(const mrhiVulkanSwapchains* swapchains, uint64_t handle)
{
    uint32_t index1 = (uint32_t)handle;
    if (index1 == 0 || index1 > swapchains->capacity)
    {
        return nullptr;
    }
    mrhiVulkanSwapchain* slot = &swapchains->slots[index1 - 1];
    return slot->swapchain != VK_NULL_HANDLE && slot->generation == (uint32_t)(handle >> 32)
               ? slot
               : nullptr;
}

// Destroys a swapchain and its semaphores, on an idle queue.
static void Free(const mrhiVulkanSwapchains* swapchains, mrhiVulkanSwapchain* slot)
{
    const mrhiVulkanDevice* api = swapchains->api;
    for (uint32_t i = 0; i < MRHI_VULKAN_SWAPCHAIN_IMAGES; ++i)
    {
        api->vkDestroySemaphore(swapchains->device, slot->presentReady[i], nullptr);
    }
    for (uint32_t i = 0; i < MRHI_VULKAN_SWAPCHAIN_IMAGES + 1; ++i)
    {
        api->vkDestroySemaphore(swapchains->device, slot->acquired[i], nullptr);
    }
    api->vkDestroySwapchainKHR(swapchains->device, slot->swapchain, nullptr);
    *slot = (mrhiVulkanSwapchain){.generation = slot->generation + 1};
}

// The result for a swapchain the window has changed under: occluded
// while the window has no area, out of date otherwise.
static mrhiResult Outdated(const mrhiVulkanSwapchains* swapchains, VkSurfaceKHR surface)
{
    VkSurfaceCapabilitiesKHR caps;
    VkResult result = swapchains->vulkan->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        swapchains->physical, surface, &caps);
    bool empty =
        result == VK_SUCCESS && (caps.currentExtent.width == 0 || caps.currentExtent.height == 0);
    return empty ? mrhi_occluded : mrhi_errorOutOfDate;
}

static mrhiResult StatusOf(VkResult result)
{
    switch (result)
    {
    case VK_SUCCESS:
        return mrhi_success;
    case VK_ERROR_OUT_OF_DATE_KHR:
    case VK_ERROR_SURFACE_LOST_KHR:
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
        return mrhi_errorOutOfDate;
    case VK_ERROR_OUT_OF_HOST_MEMORY:
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return mrhi_errorCapacity;
    default:
        return mrhi_errorPlatform;
    }
}

// Whether the size is one the window takes now: its own where it fixes
// the extent, else one within the surface's bounds.
static bool TakesSize(const VkSurfaceCapabilitiesKHR* caps, VkExtent2D size)
{
    if (caps->currentExtent.width != UINT32_MAX)
    {
        return caps->currentExtent.width == size.width && caps->currentExtent.height == size.height;
    }
    return size.width >= caps->minImageExtent.width && size.width <= caps->maxImageExtent.width &&
           size.height >= caps->minImageExtent.height && size.height <= caps->maxImageExtent.height;
}

// The swapchain's shape: at least three images within the surface's
// bounds, its present mode and its alpha.
static void Shape(const VkSurfaceCapabilitiesKHR* caps, const mrhiSurfaceConfig* config,
                  VkSwapchainCreateInfoKHR* info)
{
    uint32_t count = caps->minImageCount > 3 ? caps->minImageCount : 3;
    count = caps->maxImageCount != 0 && count > caps->maxImageCount ? caps->maxImageCount : count;
    info->minImageCount = count;
    info->presentMode = config->presentMode == mrhi_presentMailbox ? VK_PRESENT_MODE_MAILBOX_KHR
                        : config->presentMode == mrhi_presentImmediate
                            ? VK_PRESENT_MODE_IMMEDIATE_KHR
                            : VK_PRESENT_MODE_FIFO_KHR;
    bool opaque = (caps->supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0;
    info->compositeAlpha = config->alphaMode == mrhi_alphaPremultiplied
                               ? VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR
                           : opaque ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                                    : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    info->preTransform = caps->currentTransform;
}

// Reads a new swapchain's images and makes its semaphores.
static VkResult Fill(const mrhiVulkanSwapchains* swapchains, mrhiVulkanSwapchain* slot)
{
    const mrhiVulkanDevice* api = swapchains->api;
    uint32_t count = MRHI_VULKAN_SWAPCHAIN_IMAGES;
    VkResult result =
        api->vkGetSwapchainImagesKHR(swapchains->device, slot->swapchain, &count, slot->images);
    if (result != VK_SUCCESS)
    {
        return result == VK_INCOMPLETE ? VK_ERROR_INITIALIZATION_FAILED : result;
    }
    slot->imageCount = count;
    const VkSemaphoreCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (uint32_t i = 0; i < count && result == VK_SUCCESS; ++i)
    {
        result = api->vkCreateSemaphore(swapchains->device, &info, nullptr, &slot->presentReady[i]);
    }
    for (uint32_t i = 0; i < count + 1 && result == VK_SUCCESS; ++i)
    {
        result = api->vkCreateSemaphore(swapchains->device, &info, nullptr, &slot->acquired[i]);
    }
    return result;
}

// Makes a swapchain in a free slot.
static mrhiResult Make(mrhiVulkanSwapchains* swapchains, VkSurfaceKHR surface,
                       const mrhiSurfaceConfig* config, VkSwapchainKHR old,
                       mrhiVulkanSwapchain** slotOut)
{
    mrhiVulkanSwapchain* slot = nullptr;
    for (uint32_t i = 0; i < swapchains->capacity && slot == nullptr; ++i)
    {
        slot = swapchains->slots[i].swapchain == VK_NULL_HANDLE ? &swapchains->slots[i] : nullptr;
    }
    VkSurfaceCapabilitiesKHR caps;
    VkResult result = swapchains->vulkan->vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        swapchains->physical, surface, &caps);
    if (slot == nullptr || result != VK_SUCCESS)
    {
        return slot == nullptr ? mrhi_errorCapacity : StatusOf(result);
    }
    const VkExtent2D size = {config->width, config->height};
    VkSurfaceFormatKHR format;
    if (!TakesSize(&caps, size))
    {
        return mrhi_errorOutOfDate;
    }
    if (!mrhiVulkanSurfaceFormat(swapchains->vulkan, swapchains->physical, surface, config->color,
                                 &format))
    {
        return mrhi_errorUnsupported;
    }
    mrhiFormat twin = mrhiFormatSrgbPair(config->color.format);
    const VkFormat formats[2] = {mrhiVulkanFormat(config->color.format, swapchains->depthStencil),
                                 mrhiVulkanFormat(twin, swapchains->depthStencil)};
    bool twinViews = false;
    for (uint32_t i = 0; i < MRHI_VIEW_FORMATS; ++i)
    {
        twinViews = twinViews || (twin != mrhi_formatNone && config->viewFormats[i] == twin);
    }
    // An sRGB image stands for its unorm twin through the same list.
    bool mutableFormat = twinViews || format.format != formats[0];
    if (mutableFormat && !swapchains->mutableFormat)
    {
        return mrhi_errorUnsupported;
    }
    const VkImageFormatListCreateInfo list = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,
        .viewFormatCount = 2,
        .pViewFormats = formats,
    };
    VkSwapchainCreateInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = mutableFormat ? &list : nullptr,
        .flags = mutableFormat ? VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR : 0,
        .surface = surface,
        .imageFormat = format.format,
        .imageColorSpace = format.colorSpace,
        .imageExtent = size,
        .imageArrayLayers = 1,
        .imageUsage = mrhiVulkanImageUsage(config->usage, config->color.format),
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .clipped = VK_TRUE,
        .oldSwapchain = old,
    };
    Shape(&caps, config, &info);
    *slot = (mrhiVulkanSwapchain){.surface = surface, .generation = slot->generation};
    result =
        swapchains->api->vkCreateSwapchainKHR(swapchains->device, &info, nullptr, &slot->swapchain);
    if (result != VK_SUCCESS)
    {
        slot->swapchain = VK_NULL_HANDLE;
    }
    else
    {
        result = Fill(swapchains, slot);
        if (result != VK_SUCCESS)
        {
            Free(swapchains, slot);
        }
    }
    *slotOut = slot;
    return StatusOf(result);
}

mrhiResult mrhiVulkanConfigure(mrhiVulkanSwapchains* swapchains, VkSurfaceKHR surface,
                               const mrhiSurfaceConfig* config, uint64_t old, uint64_t* handleOut)
{
    mrhiVulkanSwapchain* retired = old != 0 ? Find(swapchains, old) : nullptr;
    mrhiVulkanSwapchain* slot = nullptr;
    mrhiResult status = Make(swapchains, surface, config,
                             retired != nullptr ? retired->swapchain : VK_NULL_HANDLE, &slot);
    if (retired != nullptr)
    {
        (void)swapchains->api->vkQueueWaitIdle(swapchains->queue);
        Free(swapchains, retired);
    }
    if (status == mrhi_success)
    {
        *handleOut = (uint64_t)slot->generation << 32 | (uint64_t)(slot - swapchains->slots + 1);
    }
    return status;
}

void mrhiVulkanUnconfigure(mrhiVulkanSwapchains* swapchains, uint64_t handle)
{
    mrhiVulkanSwapchain* slot = Find(swapchains, handle);
    MRHI_ASSERT(slot != nullptr);
    (void)swapchains->api->vkQueueWaitIdle(swapchains->queue);
    Free(swapchains, slot);
}

// An acquire semaphore no frame still needs, waiting for the frame that
// used the oldest one if none is free yet: UINT32_MAX when the device is
// lost.
static uint32_t FreeSemaphore(const mrhiVulkanSwapchains* swapchains,
                              const mrhiVulkanSwapchain* slot)
{
    uint64_t done = 0;
    if (swapchains->api->vkGetSemaphoreCounterValue(swapchains->device, swapchains->timeline,
                                                    &done) != VK_SUCCESS)
    {
        return UINT32_MAX;
    }
    uint32_t oldest = UINT32_MAX;
    for (uint32_t i = 0; i < slot->imageCount + 1; ++i)
    {
        uint64_t by = slot->acquiredBy[i];
        if (by != UINT64_MAX && by <= done)
        {
            return i;
        }
        oldest = by != UINT64_MAX && (oldest == UINT32_MAX || by < slot->acquiredBy[oldest])
                     ? i
                     : oldest;
    }
    MRHI_ASSERT(oldest != UINT32_MAX);
    const VkSemaphoreWaitInfo wait = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &swapchains->timeline,
        .pValues = &slot->acquiredBy[oldest],
    };
    return swapchains->api->vkWaitSemaphores(swapchains->device, &wait, UINT64_MAX) == VK_SUCCESS
               ? oldest
               : UINT32_MAX;
}

mrhiResult mrhiVulkanAcquire(mrhiVulkanSwapchains* swapchains, uint64_t handle, uint64_t* imageOut)
{
    mrhiVulkanSwapchain* slot = Find(swapchains, handle);
    MRHI_ASSERT(slot != nullptr);
    if (slot->kept != 0)
    {
        *imageOut = slot->kept;
        slot->kept = 0;
        return mrhi_success;
    }
    if (slot->presented == VK_ERROR_OUT_OF_DATE_KHR || slot->presented == VK_ERROR_SURFACE_LOST_KHR)
    {
        return Outdated(swapchains, slot->surface);
    }
    uint32_t semaphore = FreeSemaphore(swapchains, slot);
    if (semaphore == UINT32_MAX)
    {
        return mrhi_errorDeviceLost;
    }
    uint32_t index = 0;
    VkResult result =
        swapchains->api->vkAcquireNextImageKHR(swapchains->device, slot->swapchain, UINT64_MAX,
                                               slot->acquired[semaphore], VK_NULL_HANDLE, &index);
    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)
    {
        MRHI_ASSERT(index < slot->imageCount);
        slot->acquiredBy[semaphore] = UINT64_MAX;
        slot->waitOf[index] = semaphore;
        *imageOut = index + 1;
        bool suboptimal = result == VK_SUBOPTIMAL_KHR || slot->presented == VK_SUBOPTIMAL_KHR;
        return suboptimal ? mrhi_suboptimal : mrhi_success;
    }
    return result == VK_ERROR_DEVICE_LOST ? mrhi_errorDeviceLost
                                          : Outdated(swapchains, slot->surface);
}

void mrhiVulkanGiveBack(mrhiVulkanSwapchains* swapchains, uint64_t handle, uint64_t image)
{
    mrhiVulkanSwapchain* slot = Find(swapchains, handle);
    if (slot == nullptr)
    {
        return;
    }
    MRHI_ASSERT(slot->kept == 0 && image != 0 && image <= slot->imageCount);
    slot->kept = (uint32_t)image;
    // Its semaphore is still to be waited on, by the next frame that
    // takes the image.
    slot->acquiredBy[slot->waitOf[image - 1]] = UINT64_MAX;
}

VkImage mrhiVulkanSwapchainImage(const mrhiVulkanSwapchains* swapchains, uint64_t handle,
                                 uint64_t image)
{
    const mrhiVulkanSwapchain* slot = Find(swapchains, handle);
    MRHI_ASSERT(slot != nullptr && image != 0 && image <= slot->imageCount);
    return slot->images[image - 1];
}

uint32_t mrhiVulkanPresentSemaphores(mrhiVulkanSwapchains* swapchains, const mrhiDriverFrame* frame,
                                     uint64_t serial)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < frame->resourceCount; ++i)
    {
        const mrhiDriverResource* resource = &frame->resources[i];
        if (resource->kind != mrhiDriverSurfaceImage)
        {
            continue;
        }
        mrhiVulkanSwapchain* slot = Find(swapchains, resource->handle);
        MRHI_ASSERT(slot != nullptr && count < swapchains->capacity);
        uint32_t image = (uint32_t)resource->image - 1;
        uint32_t semaphore = slot->waitOf[image];
        slot->acquiredBy[semaphore] = serial;
        swapchains->waits[count] = (VkSemaphoreSubmitInfo){
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = slot->acquired[semaphore],
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        };
        swapchains->signals[count + 1] = (VkSemaphoreSubmitInfo){
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .semaphore = slot->presentReady[image],
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        };
        swapchains->presentChains[count] = slot->swapchain;
        swapchains->presentImages[count] = image;
        swapchains->presentWaits[count] = slot->presentReady[image];
        swapchains->presentSlots[count] = (uint32_t)(slot - swapchains->slots);
        ++count;
    }
    swapchains->presentCount = count;
    return count;
}

bool mrhiVulkanPresent(mrhiVulkanSwapchains* swapchains)
{
    uint32_t count = swapchains->presentCount;
    if (count == 0)
    {
        return true;
    }
    const VkPresentInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = count,
        .pWaitSemaphores = swapchains->presentWaits,
        .swapchainCount = count,
        .pSwapchains = swapchains->presentChains,
        .pImageIndices = swapchains->presentImages,
        .pResults = swapchains->presentResults,
    };
    VkResult result = swapchains->api->vkQueuePresentKHR(swapchains->queue, &info);
    for (uint32_t i = 0; i < count; ++i)
    {
        swapchains->slots[swapchains->presentSlots[i]].presented = swapchains->presentResults[i];
    }
    swapchains->presentCount = 0;
    return result != VK_ERROR_DEVICE_LOST;
}

void mrhiVulkanSwapchainsDestroy(mrhiVulkanSwapchains* swapchains)
{
    for (uint32_t i = 0; i < swapchains->capacity; ++i)
    {
        if (swapchains->slots[i].swapchain != VK_NULL_HANDLE)
        {
            Free(swapchains, &swapchains->slots[i]);
        }
    }
}
