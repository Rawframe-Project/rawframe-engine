// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's surfaces (d3d12_surface.h). A surface is its
// window alone: the swapchain comes when a device configures it, and
// any adapter presents there, DXGI's compositor taking what another
// adapter renders. Flip model swapchains show 8-bit and 10-bit unorm
// buffers in sRGB and half floats in scRGB; HDR10 is offered only where
// the window's monitor is in HDR, which the output's color space tells.
// Alpha is opaque, since only composition swapchains blend.

#include "d3d12_surface.h"

#include "invariant.h"

#include <string.h>

static_assert(sizeof(HWND) <= sizeof(uint64_t), "a handle holds a window");

mrhiResult mrhiD3d12CreateSurface(const mrhiChain* source, uint64_t* handleOut)
{
    *handleOut = 0;
    if (source->type != mrhi_structSurfaceSourceWin32)
    {
        return mrhi_errorUnsupported;
    }
    HWND window = ((const mrhiSurfaceSourceWin32*)source)->hwnd;
    if (window == nullptr || !IsWindow(window))
    {
        return mrhi_errorUnsupported;
    }
    memcpy(handleOut, (const void*)&window, sizeof(HWND));
    return mrhi_success;
}

HWND mrhiD3d12WindowOf(uint64_t surface)
{
    HWND window = nullptr;
    memcpy((void*)&window, &surface, sizeof(HWND));
    return window;
}

bool mrhiD3d12Tears(IDXGIFactory4* factory)
{
    IDXGIFactory5* five = nullptr;
    BOOL tears = FALSE;
    if (SUCCEEDED(IDXGIFactory4_QueryInterface(factory, &IID_IDXGIFactory5, (void**)&five)))
    {
        if (FAILED(IDXGIFactory5_CheckFeatureSupport(five, DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                                     &tears, sizeof(tears))))
        {
            tears = FALSE;
        }
        IDXGIFactory5_Release(five);
    }
    return tears != FALSE;
}

// Whether an output is the monitor's and in HDR.
static bool IsHdrOutput(IDXGIOutput* output, HMONITOR monitor)
{
    IDXGIOutput6* six = nullptr;
    if (FAILED(IDXGIOutput_QueryInterface(output, &IID_IDXGIOutput6, (void**)&six)))
    {
        return false;
    }
    DXGI_OUTPUT_DESC1 desc;
    bool hdr = SUCCEEDED(IDXGIOutput6_GetDesc1(six, &desc)) && desc.Monitor == monitor &&
               desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    IDXGIOutput6_Release(six);
    return hdr;
}

// Whether the monitor showing most of a window is in HDR.
static bool IsHdr(IDXGIFactory4* factory, HWND window)
{
    HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    bool hdr = false;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT a = 0; !hdr && IDXGIFactory4_EnumAdapters1(factory, a, &adapter) == S_OK; ++a)
    {
        IDXGIOutput* output = nullptr;
        for (UINT o = 0; !hdr && IDXGIAdapter1_EnumOutputs(adapter, o, &output) == S_OK; ++o)
        {
            hdr = IsHdrOutput(output, monitor);
            IDXGIOutput_Release(output);
        }
        IDXGIAdapter1_Release(adapter);
    }
    return hdr;
}

void mrhiD3d12SurfaceCaps(IDXGIFactory4* factory, uint64_t surface, mrhiSurfaceCaps* capsOut)
{
    static const mrhiSurfaceColor kColors[] = {
        {mrhi_formatBgra8Unorm, mrhi_primariesBt709, mrhi_transferSrgb, mrhi_rangeStandard},
        {mrhi_formatRgba8Unorm, mrhi_primariesBt709, mrhi_transferSrgb, mrhi_rangeStandard},
        {mrhi_formatRgb10a2Unorm, mrhi_primariesBt709, mrhi_transferSrgb, mrhi_rangeStandard},
        {mrhi_formatRgba16Float, mrhi_primariesBt709, mrhi_transferLinear, mrhi_rangeExtended},
    };
    static const mrhiSurfaceColor kHdr10 = {mrhi_formatRgb10a2Unorm, mrhi_primariesBt2020,
                                            mrhi_transferPq, mrhi_rangeStandard};
    *capsOut = (mrhiSurfaceCaps){0};
    HWND window = mrhiD3d12WindowOf(surface);
    if (!IsWindow(window))
    {
        return;
    }
    uint32_t count = sizeof(kColors) / sizeof(kColors[0]);
    static_assert(sizeof(kColors) / sizeof(kColors[0]) < MRHI_SURFACE_COLORS, "colors fit");
    memcpy(capsOut->colors, kColors, sizeof(kColors));
    if (IsHdr(factory, window))
    {
        capsOut->colors[count++] = kHdr10;
    }
    capsOut->presentable = true;
    capsOut->colorCount = count;
    capsOut->presentModes = mrhi_presentFifo | mrhi_presentMailbox |
                            (mrhiD3d12Tears(factory) ? mrhi_presentImmediate : 0u);
    capsOut->alphaModes = mrhi_alphaOpaque;
    capsOut->usages = mrhi_textureRenderTarget | mrhi_textureSampled | mrhi_textureCopySource |
                      mrhi_textureCopyDestination;
    // The flip model takes no sRGB buffer, only sRGB views of one.
    capsOut->twinViews = true;
}

DXGI_COLOR_SPACE_TYPE mrhiD3d12ColorSpace(const mrhiSurfaceColor* color)
{
    if (color->transfer == mrhi_transferPq)
    {
        return DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    }
    return color->transfer == mrhi_transferLinear ? DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709
                                                  : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
}
