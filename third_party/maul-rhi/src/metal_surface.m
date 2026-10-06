// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Metal driver's surfaces (mrhi-0003). A configuration sets the
// layer's device, pixel format, color space, extended range, drawable
// size, synchronization and opacity. The layer scales drawables of any
// size to its bounds, so a program's size is its own; the drawables are
// out of date once something else, a view resizing them, changes their
// size, and suboptimal while the layer's bounds in pixels differ from
// it. Immediate presentation is macOS's: the display's synchronization
// turned off.

#include "metal_surface.h"

#include "allocator.h"
#include "invariant.h"
#include "metal_names.h"

#import <QuartzCore/CAMetalLayer.h>
#import <TargetConditionals.h>
#include <stdalign.h>

typedef struct MetalSwapchain
{
    CAMetalLayer* layer;
    uint32_t width;
    uint32_t height;
} MetalSwapchain;

uint32_t mrhiMetalSurfaceColors(mrhiSurfaceColor* colors)
{
    static const mrhiSurfaceColor kColors[] = {
        {mrhi_formatBgra8Unorm, mrhi_primariesBt709, mrhi_transferSrgb, mrhi_rangeStandard},
        {mrhi_formatBgra8Unorm, mrhi_primariesDisplayP3, mrhi_transferSrgb, mrhi_rangeStandard},
        {mrhi_formatRgba16Float, mrhi_primariesBt709, mrhi_transferLinear, mrhi_rangeStandard},
        {mrhi_formatRgba16Float, mrhi_primariesBt709, mrhi_transferLinear, mrhi_rangeExtended},
        {mrhi_formatRgba16Float, mrhi_primariesDisplayP3, mrhi_transferLinear, mrhi_rangeStandard},
        {mrhi_formatRgba16Float, mrhi_primariesDisplayP3, mrhi_transferLinear, mrhi_rangeExtended},
    };
    static_assert(sizeof(kColors) / sizeof(kColors[0]) <= MRHI_SURFACE_COLORS, "colors fit");
    // A layer takes extended range content on iOS from 16 only.
    bool extended = false;
    if (@available(macOS 10.11, iOS 16.0, *))
    {
        extended = true;
    }
    uint32_t count = 0;
    for (uint32_t i = 0; i < sizeof(kColors) / sizeof(kColors[0]); ++i)
    {
        if (extended || kColors[i].range != mrhi_rangeExtended)
        {
            colors[count++] = kColors[i];
        }
    }
    return count;
}

// A color's color space, which the caller releases.
static CGColorSpaceRef NewColorSpace(const mrhiSurfaceColor* color)
{
    bool p3 = color->primaries == mrhi_primariesDisplayP3;
    CFStringRef name = kCGColorSpaceSRGB;
    if (color->transfer == mrhi_transferSrgb)
    {
        name = p3 ? kCGColorSpaceDisplayP3 : kCGColorSpaceSRGB;
    }
    else if (color->range == mrhi_rangeExtended)
    {
        name = p3 ? kCGColorSpaceExtendedLinearDisplayP3 : kCGColorSpaceExtendedLinearSRGB;
    }
    else
    {
        name = p3 ? kCGColorSpaceLinearDisplayP3 : kCGColorSpaceLinearSRGB;
    }
    return CGColorSpaceCreateWithName(name);
}

static void Set(CAMetalLayer* layer, id<MTLDevice> device, const mrhiSurfaceConfig* config)
{
    layer.device = device;
    layer.pixelFormat = mrhiMetalFormat(config->color.format);
    CGColorSpaceRef space = NewColorSpace(&config->color);
    layer.colorspace = space;
    CGColorSpaceRelease(space);
    if (@available(macOS 10.11, iOS 16.0, *))
    {
        layer.wantsExtendedDynamicRangeContent = config->color.range == mrhi_rangeExtended;
    }
    layer.drawableSize = CGSizeMake(config->width, config->height);
    layer.framebufferOnly = config->usage == mrhi_textureRenderTarget;
    layer.maximumDrawableCount = 3;
    layer.opaque = config->alphaMode == mrhi_alphaOpaque;
#if TARGET_OS_OSX
    layer.displaySyncEnabled = config->presentMode != mrhi_presentImmediate;
#endif
}

mrhiResult mrhiMetalConfigure(const mrhiAllocator* allocator, id<MTLDevice> device,
                              uint64_t surface, const mrhiSurfaceConfig* config,
                              uint64_t oldSwapchain, uint64_t* swapchainOut)
{
    if (oldSwapchain != 0)
    {
        mrhiMetalUnconfigure(allocator, oldSwapchain);
    }
    MetalSwapchain* swapchain =
        mrhiAllocate(allocator, sizeof(MetalSwapchain), alignof(MetalSwapchain));
    if (swapchain == nullptr)
    {
        return mrhi_errorCapacity;
    }
    CAMetalLayer* layer = (CAMetalLayer*)(void*)(uintptr_t)surface;
    *swapchain = (MetalSwapchain){
        .layer = [layer retain],
        .width = config->width,
        .height = config->height,
    };
    @autoreleasepool
    {
        Set(layer, device, config);
    }
    *swapchainOut = (uint64_t)(uintptr_t)swapchain;
    return mrhi_success;
}

void mrhiMetalUnconfigure(const mrhiAllocator* allocator, uint64_t swapchain)
{
    MetalSwapchain* made = (MetalSwapchain*)(uintptr_t)swapchain;
    [made->layer release];
    mrhiRelease(allocator, made, sizeof(MetalSwapchain), alignof(MetalSwapchain));
}

mrhiResult mrhiMetalAcquire(uint64_t swapchain, uint64_t* imageOut)
{
    const MetalSwapchain* made = (const MetalSwapchain*)(uintptr_t)swapchain;
    mrhiResult status = mrhi_success;
    @autoreleasepool
    {
        CAMetalLayer* layer = made->layer;
        CGSize size = layer.drawableSize;
        if (size.width != made->width || size.height != made->height)
        {
            return mrhi_errorOutOfDate;
        }
        id<CAMetalDrawable> drawable = [layer nextDrawable];
        if (drawable == nil)
        {
            return mrhi_occluded;
        }
        *imageOut = (uint64_t)(uintptr_t)(void*)[drawable retain];
        CGSize bounds = layer.bounds.size;
        double scale = layer.contentsScale;
        bool shown = bounds.width > 0.0 && bounds.height > 0.0;
        status =
            shown && (bounds.width * scale != size.width || bounds.height * scale != size.height)
                ? mrhi_suboptimal
                : mrhi_success;
    }
    return status;
}

id<MTLTexture> mrhiMetalImageTexture(uint64_t image)
{
    id<CAMetalDrawable> drawable = (id<CAMetalDrawable>)(void*)(uintptr_t)image;
    return drawable.texture;
}

void mrhiMetalPresent(id<MTLCommandBuffer> commands, uint64_t image)
{
    [commands presentDrawable:(id<CAMetalDrawable>)(void*)(uintptr_t)image];
}

void mrhiMetalReleaseImage(uint64_t image)
{
    [(id<CAMetalDrawable>)(void*)(uintptr_t)image release];
}
