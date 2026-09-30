// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's swapchains (d3d12_swapchain.h). A swapchain is a
// flip-discard swapchain of three images on the device's queue, its
// window left to the program: DXGI's Alt+Enter is turned off. Its color
// space is set as the surface's color asks, and its images stretch to
// the window, whose size never refuses one. Acquiring waits on the
// frame latency waitable object, for a second at most, so that a present
// never blocks; a wait whose image was given back unpresented serves the
// next acquire. Fifo presents at sync interval 1, mailbox at 0, and
// immediate at 0 with tearing allowed.

#include "d3d12_swapchain.h"

#include "d3d12_names.h"
#include "d3d12_surface.h"
#include "invariant.h"

#include <stdalign.h>
#include <string.h>

// The longest an acquire waits for DXGI to take a present.
#define WAIT_MS 1000

mrhiD3d12SwapchainRoom mrhiD3d12PlanSwapchains(mrhiLayout* layout, const mrhiDeviceLimits* limits)
{
    return (mrhiD3d12SwapchainRoom){
        .swapchains = mrhiLayoutAdd(layout, limits->surfaces, sizeof(mrhiD3d12Swapchain),
                                    alignof(mrhiD3d12Swapchain)),
        .slots = mrhiLayoutAdd(layout, limits->surfaces, sizeof(uint32_t), alignof(uint32_t)),
    };
}

void mrhiD3d12LaySwapchains(mrhiD3d12Swapchains* swapchains, unsigned char* block,
                            const mrhiD3d12SwapchainRoom* room, const mrhiDeviceLimits* limits)
{
    swapchains->swapchains = (mrhiD3d12Swapchain*)(block + room->swapchains);
    // Free entries hold nothing, which the device's end relies on.
    memset(swapchains->swapchains, 0, limits->surfaces * sizeof(mrhiD3d12Swapchain));
    (void)mrhiD3d12InitSlots(&swapchains->slots, (uint32_t*)(block + room->slots),
                             limits->surfaces);
}

static mrhiD3d12Swapchain* At(const mrhiD3d12Swapchains* swapchains, uint64_t handle)
{
    MRHI_ASSERT(handle != 0 && handle <= swapchains->slots.capacity);
    return &swapchains->swapchains[handle - 1];
}

// Releases what a swapchain holds.
static void Free(mrhiD3d12Swapchain* swapchain)
{
    for (uint32_t i = 0; i < MRHI_D3D12_IMAGES; ++i)
    {
        if (swapchain->images[i] != nullptr)
        {
            ID3D12Resource_Release(swapchain->images[i]);
        }
    }
    if (swapchain->waitable != nullptr)
    {
        (void)CloseHandle(swapchain->waitable);
    }
    if (swapchain->swapchain != nullptr)
    {
        IDXGISwapChain3_Release(swapchain->swapchain);
    }
    *swapchain = (mrhiD3d12Swapchain){0};
}

static DXGI_USAGE UsageOf(mrhiTextureUsage usage)
{
    return DXGI_USAGE_RENDER_TARGET_OUTPUT |
           ((usage & mrhi_textureSampled) != 0 ? DXGI_USAGE_SHADER_INPUT : 0u);
}

// Makes the swapchain of a window, as far as DXGI goes.
static mrhiResult Make(const mrhiD3d12Swapchains* swapchains, mrhiD3d12Swapchain* made,
                       const mrhiSurfaceConfig* config)
{
    made->flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT |
                  (mrhiD3d12Tears(swapchains->factory) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u);
    const DXGI_SWAP_CHAIN_DESC1 desc = {
        .Width = config->width,
        .Height = config->height,
        .Format = mrhiD3d12Format(config->color.format),
        .SampleDesc = {.Count = 1},
        .BufferUsage = UsageOf(config->usage),
        .BufferCount = MRHI_D3D12_IMAGES,
        .Scaling = DXGI_SCALING_STRETCH,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
        .Flags = made->flags,
    };
    IDXGISwapChain1* one = nullptr;
    if (FAILED(IDXGIFactory4_CreateSwapChainForHwnd(swapchains->factory,
                                                    (IUnknown*)swapchains->queue, made->window,
                                                    &desc, nullptr, nullptr, &one)))
    {
        return mrhi_errorPlatform;
    }
    HRESULT result =
        IDXGISwapChain1_QueryInterface(one, &IID_IDXGISwapChain3, (void**)&made->swapchain);
    IDXGISwapChain1_Release(one);
    if (FAILED(result))
    {
        return mrhi_errorPlatform;
    }
    (void)IDXGIFactory4_MakeWindowAssociation(swapchains->factory, made->window,
                                              DXGI_MWA_NO_ALT_ENTER);
    DXGI_COLOR_SPACE_TYPE space = mrhiD3d12ColorSpace(&config->color);
    UINT support = 0;
    if (FAILED(IDXGISwapChain3_CheckColorSpaceSupport(made->swapchain, space, &support)) ||
        (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0 ||
        FAILED(IDXGISwapChain3_SetColorSpace1(made->swapchain, space)))
    {
        return mrhi_errorUnsupported;
    }
    (void)IDXGISwapChain3_SetMaximumFrameLatency(made->swapchain, MRHI_D3D12_LATENCY);
    made->waitable = IDXGISwapChain3_GetFrameLatencyWaitableObject(made->swapchain);
    for (UINT i = 0; i < MRHI_D3D12_IMAGES; ++i)
    {
        if (FAILED(IDXGISwapChain3_GetBuffer(made->swapchain, i, &IID_ID3D12Resource,
                                             (void**)&made->images[i])))
        {
            return mrhi_errorPlatform;
        }
    }
    return made->waitable != nullptr ? mrhi_success : mrhi_errorPlatform;
}

mrhiResult mrhiD3d12Configure(mrhiD3d12Swapchains* swapchains, uint64_t surface,
                              const mrhiSurfaceConfig* config, uint64_t* handleOut)
{
    *handleOut = 0;
    uint32_t handle = mrhiD3d12TakeSlot(&swapchains->slots);
    if (handle == 0)
    {
        return mrhi_errorCapacity;
    }
    mrhiD3d12Swapchain* made = At(swapchains, handle);
    *made = (mrhiD3d12Swapchain){
        .window = mrhiD3d12WindowOf(surface),
        .syncInterval = config->presentMode == mrhi_presentFifo ? 1 : 0,
        .presentFlags =
            config->presentMode == mrhi_presentImmediate ? DXGI_PRESENT_ALLOW_TEARING : 0,
        .width = config->width,
        .height = config->height,
    };
    mrhiResult status = Make(swapchains, made, config);
    if (status != mrhi_success)
    {
        Free(made);
        mrhiD3d12GiveSlot(&swapchains->slots, handle);
        return status;
    }
    *handleOut = handle;
    return mrhi_success;
}

void mrhiD3d12ReleaseSwapchain(mrhiD3d12Swapchains* swapchains, uint64_t handle)
{
    Free(At(swapchains, handle));
    mrhiD3d12GiveSlot(&swapchains->slots, handle);
}

mrhiResult mrhiD3d12Acquire(mrhiD3d12Swapchains* swapchains, uint64_t handle, uint64_t* imageOut)
{
    *imageOut = 0;
    mrhiD3d12Swapchain* swapchain = At(swapchains, handle);
    if (IsIconic(swapchain->window))
    {
        return mrhi_occluded;
    }
    if (!swapchain->waited)
    {
        (void)WaitForSingleObjectEx(swapchain->waitable, WAIT_MS, FALSE);
        swapchain->waited = true;
    }
    *imageOut = (uint64_t)IDXGISwapChain3_GetCurrentBackBufferIndex(swapchain->swapchain) + 1;
    RECT client = {0};
    bool fits = GetClientRect(swapchain->window, &client) &&
                (uint32_t)(client.right - client.left) == swapchain->width &&
                (uint32_t)(client.bottom - client.top) == swapchain->height;
    return fits ? mrhi_success : mrhi_suboptimal;
}

void mrhiD3d12GiveBack(mrhiD3d12Swapchains* swapchains, uint64_t handle, uint64_t image)
{
    // The wait stays unspent, for the next acquire.
    (void)swapchains;
    (void)handle;
    (void)image;
}

ID3D12Resource* mrhiD3d12ImageOf(const mrhiD3d12Swapchains* swapchains, uint64_t handle,
                                 uint64_t image)
{
    MRHI_ASSERT(image != 0 && image <= MRHI_D3D12_IMAGES);
    return At(swapchains, handle)->images[image - 1];
}

bool mrhiD3d12Present(mrhiD3d12Swapchains* swapchains, uint64_t handle)
{
    mrhiD3d12Swapchain* swapchain = At(swapchains, handle);
    swapchain->waited = false;
    HRESULT result = IDXGISwapChain3_Present(swapchain->swapchain, swapchain->syncInterval,
                                             swapchain->presentFlags);
    return result != DXGI_ERROR_DEVICE_REMOVED && result != DXGI_ERROR_DEVICE_RESET;
}
