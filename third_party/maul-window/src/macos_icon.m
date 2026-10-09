// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Window icons on macOS (macos.h). A macOS window has no icon of its
// own: the Dock and the application switcher show the application's.
// So a window's icon request sets the application's icon, the last
// request of any window winning, and no images give back the bundle's
// own. The images are the representations of one image, each of the
// largest's size, so AppKit draws the one nearest the pixels it needs.

#include "icon.h"
#include "macos.h"

#include <string.h>

NSBitmapImageRep* mwinMacImageRep(const mwinIconCopyImage* image, NSSize size)
{
    NSBitmapImageRep* rep =
        [[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                                 pixelsWide:image->width
                                                 pixelsHigh:image->height
                                              bitsPerSample:8
                                            samplesPerPixel:4
                                                   hasAlpha:YES
                                                   isPlanar:NO
                                             colorSpaceName:NSDeviceRGBColorSpace
                                               bitmapFormat:NSBitmapFormatAlphaNonpremultiplied
                                                bytesPerRow:(NSInteger)image->width * 4
                                               bitsPerPixel:32] autorelease];
    if (rep != nil)
    {
        memcpy(rep.bitmapData, image->pixels, (size_t)image->width * 4 * image->height);
        rep.size = size;
    }
    return rep;
}

mwinOutcome mwinMacSetIcon(const mwinRequest* request)
{
    const mwinIconCopy* copy = request->value.icon;
    if (copy == nullptr || copy->count == 0)
    {
        NSApp.applicationIconImage = nil;
        return mwin_outcomeDone;
    }
    const mwinIconCopyImage* largest = mwinIconFor(copy, UINT32_MAX);
    NSSize size = NSMakeSize(largest->width, largest->height);
    NSImage* icon = [[[NSImage alloc] initWithSize:size] autorelease];
    for (uint32_t i = 0; i < copy->count; i++)
    {
        NSBitmapImageRep* rep = mwinMacImageRep(&copy->images[i], size);
        if (rep == nil)
        {
            return mwin_outcomeFailed;
        }
        [icon addRepresentation:rep];
    }
    NSApp.applicationIconImage = icon;
    return mwin_outcomeDone;
}
