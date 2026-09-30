// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Vulkan device's swapchains (mrhi-0003, mrhi-0007, mrhi-0013): a
// configured surface's swapchain, its images and the semaphores that
// order acquiring, the frame's work and presenting. Each swapchain has a
// semaphore per image signalled for its present, and one more acquire
// semaphore than images, each tagged with the frame that waited on it,
// reused once that frame finishes. An image a frame gives back stays
// acquired and is handed out at the next acquire.

#ifndef MAUL_RHI_SRC_VULKAN_SWAPCHAIN_H
#define MAUL_RHI_SRC_VULKAN_SWAPCHAIN_H

#include "driver.h"
#include "vulkan_api.h"

// The images a swapchain may have.
#define MRHI_VULKAN_SWAPCHAIN_IMAGES 16

typedef struct mrhiVulkanSwapchain
{
    VkSwapchainKHR swapchain;
    VkSurfaceKHR surface;
    uint32_t imageCount;
    VkImage images[MRHI_VULKAN_SWAPCHAIN_IMAGES];
    VkSemaphore presentReady[MRHI_VULKAN_SWAPCHAIN_IMAGES];
    VkSemaphore acquired[MRHI_VULKAN_SWAPCHAIN_IMAGES + 1];
    // The frame that waited on each acquire semaphore (0 for none yet),
    // or UINT64_MAX while an image holds it unsubmitted.
    uint64_t acquiredBy[MRHI_VULKAN_SWAPCHAIN_IMAGES + 1];
    // The acquire semaphore each image's frame waits on.
    uint32_t waitOf[MRHI_VULKAN_SWAPCHAIN_IMAGES];
    // The image a frame gave back, plus one; 0 for none.
    uint32_t kept;
    // The last present's result, reported at the next acquire.
    VkResult presented;
    // Bumped each time the slot is freed, so that a handle of a
    // swapchain gone never names the next one.
    uint32_t generation;
} mrhiVulkanSwapchain;

typedef struct mrhiVulkanSwapchains
{
    const mrhiVulkan* vulkan;
    const mrhiVulkanDevice* api;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    VkSemaphore timeline;
    // Whether the device has VK_KHR_swapchain_mutable_format.
    bool mutableFormat;
    VkFormat depthStencil;
    mrhiVulkanSwapchain* slots;
    uint32_t capacity;
    // Room for one frame's surface images, a swapchain acquiring once a
    // frame: the semaphores its submission waits on and signals (the
    // first wait and the first signal left for the caller), and what it
    // presents.
    VkSemaphoreSubmitInfo* waits;
    VkSemaphoreSubmitInfo* signals;
    VkSwapchainKHR* presentChains;
    uint32_t* presentImages;
    VkSemaphore* presentWaits;
    VkResult* presentResults;
    uint32_t* presentSlots;
    uint32_t presentCount;
} mrhiVulkanSwapchains;

// Makes a surface's swapchain from a config the core has checked,
// retiring the old one (0 for none) whether or not it succeeds, once the
// queue is idle: its handle; mrhi_errorOutOfDate for a size the window
// does not take now, mrhi_errorUnsupported for the sRGB twin without
// VK_KHR_swapchain_mutable_format, mrhi_errorCapacity, or
// mrhi_errorPlatform.
mrhiResult mrhiVulkanConfigure(mrhiVulkanSwapchains* swapchains, VkSurfaceKHR surface,
                               const mrhiSurfaceConfig* config, uint64_t old, uint64_t* handleOut);

// Destroys a swapchain once the queue is idle.
void mrhiVulkanUnconfigure(mrhiVulkanSwapchains* swapchains, uint64_t handle);

// Acquires a swapchain's next image, the one given back if there is one:
// mrhi_success or mrhi_suboptimal with it, never zero; mrhi_occluded or
// mrhi_errorOutOfDate without one; or mrhi_errorDeviceLost.
mrhiResult mrhiVulkanAcquire(mrhiVulkanSwapchains* swapchains, uint64_t handle, uint64_t* imageOut);

// Keeps an image a frame acquired and gave back for the next acquire;
// nothing when its swapchain is gone.
void mrhiVulkanGiveBack(mrhiVulkanSwapchains* swapchains, uint64_t handle, uint64_t image);

// The image of a swapchain a frame acquired.
VkImage mrhiVulkanSwapchainImage(const mrhiVulkanSwapchains* swapchains, uint64_t handle,
                                 uint64_t image);

// Fills the semaphores a frame's surface images add to its submission:
// the acquire semaphores it waits on, from waits[1], and the present
// semaphores it signals, from signals[1]; tags the acquire semaphores
// with the frame's serial. Returns how many images there are.
uint32_t mrhiVulkanPresentSemaphores(mrhiVulkanSwapchains* swapchains, const mrhiDriverFrame* frame,
                                     uint64_t serial);

// Presents the images the last mrhiVulkanPresentSemaphores named, once
// their frame is submitted: false when the device is lost.
bool mrhiVulkanPresent(mrhiVulkanSwapchains* swapchains);

// Destroys every swapchain left, on an idle device.
void mrhiVulkanSwapchainsDestroy(mrhiVulkanSwapchains* swapchains);

#endif // MAUL_RHI_SRC_VULKAN_SWAPCHAIN_H
