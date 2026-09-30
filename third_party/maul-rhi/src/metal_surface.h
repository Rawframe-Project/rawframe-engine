// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Metal driver's surfaces (mrhi-0003): a surface is a CAMetalLayer,
// a swapchain its configuration, and an image one of its drawables,
// retained until a frame presents it or the core takes it back.
// Included by the driver's Objective-C files only.

#ifndef MAUL_RHI_SRC_METAL_SURFACE_H
#define MAUL_RHI_SRC_METAL_SURFACE_H

#include "driver.h"

#import <Metal/Metal.h>

// What a CAMetalLayer can show on any Metal device, the preferred
// first, and how many.
uint32_t mrhiMetalSurfaceColors(mrhiSurfaceColor* colors);

// Configures a surface's layer for a device: success with the swapchain,
// or mrhi_errorCapacity when the allocator fails. The old swapchain, 0
// for none, is freed either way.
mrhiResult mrhiMetalConfigure(const mrhiAllocator* allocator, id<MTLDevice> device,
                              uint64_t surface, const mrhiSurfaceConfig* config,
                              uint64_t oldSwapchain, uint64_t* swapchainOut);
void mrhiMetalUnconfigure(const mrhiAllocator* allocator, uint64_t swapchain);

// Takes the layer's next drawable: mrhi_errorOutOfDate when something
// other than the configuration resized the layer's drawables,
// mrhi_occluded when it gives none, mrhi_suboptimal when the layer's
// bounds no longer match the drawables' size.
mrhiResult mrhiMetalAcquire(uint64_t swapchain, uint64_t* imageOut);

// An image's texture, and its presentation after a command buffer's
// work; an image presented or taken back is released.
id<MTLTexture> mrhiMetalImageTexture(uint64_t image);
void mrhiMetalPresent(id<MTLCommandBuffer> commands, uint64_t image);
void mrhiMetalReleaseImage(uint64_t image);

#endif // MAUL_RHI_SRC_METAL_SURFACE_H
