// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's swapchains (mrhi-0003): a device's configured
// surfaces, each a flip model swapchain of its window in a table of the
// device's block, with its images and the waitable object that paces
// acquiring them. Included by the driver's files only.

#ifndef MAUL_RHI_SRC_D3D12_SWAPCHAIN_H
#define MAUL_RHI_SRC_D3D12_SWAPCHAIN_H

#include "allocator.h"
#include "d3d12_api.h"
#include "d3d12_slots.h"
#include "driver.h"

#include "maul-rhi/surface.h"

// The images a swapchain holds, and the presents it queues at most.
#define MRHI_D3D12_IMAGES  3
#define MRHI_D3D12_LATENCY 2

typedef struct mrhiD3d12Swapchain
{
    IDXGISwapChain3* swapchain;
    HANDLE waitable;
    HWND window;
    ID3D12Resource* images[MRHI_D3D12_IMAGES];
    // The flags it was made with, and how it presents.
    UINT flags;
    UINT syncInterval;
    UINT presentFlags;
    uint32_t width;
    uint32_t height;
    // Whether the last wait for an image was not spent on a present, so
    // that the next acquire takes that image without waiting.
    bool waited;
} mrhiD3d12Swapchain;

typedef struct mrhiD3d12Swapchains
{
    IDXGIFactory4* factory;
    ID3D12CommandQueue* queue;
    mrhiD3d12Swapchain* swapchains;
    mrhiD3d12Slots slots;
} mrhiD3d12Swapchains;

// Where the table lies in a device's block.
typedef struct mrhiD3d12SwapchainRoom
{
    size_t swapchains;
    size_t slots;
} mrhiD3d12SwapchainRoom;

// Adds the table a device's limits need to its layout, and sets it up in
// its block.
mrhiD3d12SwapchainRoom mrhiD3d12PlanSwapchains(mrhiLayout* layout, const mrhiDeviceLimits* limits);
void mrhiD3d12LaySwapchains(mrhiD3d12Swapchains* swapchains, unsigned char* block,
                            const mrhiD3d12SwapchainRoom* room, const mrhiDeviceLimits* limits);

// Makes a surface's swapchain: success with its handle, mrhi_errorUnsupported
// for a color the swapchain cannot show, mrhi_errorCapacity when the
// table is full, or mrhi_errorPlatform when DXGI refuses.
mrhiResult mrhiD3d12Configure(mrhiD3d12Swapchains* swapchains, uint64_t surface,
                              const mrhiSurfaceConfig* config, uint64_t* handleOut);

// Releases a swapchain no frame uses any more.
void mrhiD3d12ReleaseSwapchain(mrhiD3d12Swapchains* swapchains, uint64_t handle);

// Takes a swapchain's next image, waiting until DXGI can queue its
// present: success or mrhi_suboptimal (the window's size is no longer
// the swapchain's) with the image, never zero, or mrhi_occluded for a
// minimized window.
mrhiResult mrhiD3d12Acquire(mrhiD3d12Swapchains* swapchains, uint64_t handle, uint64_t* imageOut);

// Gives back an image acquired and not presented.
void mrhiD3d12GiveBack(mrhiD3d12Swapchains* swapchains, uint64_t handle, uint64_t image);

// A swapchain's image.
ID3D12Resource* mrhiD3d12ImageOf(const mrhiD3d12Swapchains* swapchains, uint64_t handle,
                                 uint64_t image);

// Presents a swapchain's acquired image after the work queued so far:
// false when the device is lost.
bool mrhiD3d12Present(mrhiD3d12Swapchains* swapchains, uint64_t handle);

#endif // MAUL_RHI_SRC_D3D12_SWAPCHAIN_H
