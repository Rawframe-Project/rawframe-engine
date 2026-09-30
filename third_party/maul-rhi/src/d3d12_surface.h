// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's surfaces (mrhi-0003): Win32 windows, each surface's
// handle holding its window, and what DXGI's flip model shows there.
// Included by the driver's files only.

#ifndef MAUL_RHI_SRC_D3D12_SURFACE_H
#define MAUL_RHI_SRC_D3D12_SURFACE_H

#include "d3d12_api.h"
#include "driver.h"

#include "maul-rhi/surface.h"

// Makes the surface of the Win32 window a source names: success with its
// handle, or mrhi_errorUnsupported for another source or no window.
mrhiResult mrhiD3d12CreateSurface(const mrhiChain* source, uint64_t* handleOut);

// The window a surface's handle holds.
HWND mrhiD3d12WindowOf(uint64_t surface);

// What a surface shows through the factory's flip model swapchains:
// sRGB 8-bit and 10-bit colors, scRGB half floats, and HDR10 where the
// window's monitor is in HDR; fifo and mailbox, immediate where DXGI
// tears; opaque alpha.
void mrhiD3d12SurfaceCaps(IDXGIFactory4* factory, uint64_t surface, mrhiSurfaceCaps* capsOut);

// Whether the factory's swapchains may tear.
bool mrhiD3d12Tears(IDXGIFactory4* factory);

// The DXGI color space of a surface color the caps report.
DXGI_COLOR_SPACE_TYPE mrhiD3d12ColorSpace(const mrhiSurfaceColor* color);

#endif // MAUL_RHI_SRC_D3D12_SURFACE_H
