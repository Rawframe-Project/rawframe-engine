// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Window chrome on macOS (macos.h): styles, custom chrome and the hit
// regions, size limits, the aspect ratio and opacity. Custom chrome is
// a titled window whose content fills the frame, its title bar clear and
// its buttons hidden, so the program draws everything while the window
// keeps its shadow, its edges and its full screen. A press on a caption
// region moves the window through AppKit, and a double click on one does
// what the user set for title bars; a press on an edge region resizes it
// here, AppKit having no way to start a resize from inside the content,
// within its limits and ratio. Neither is reported. An edge of a window
// that cannot be resized is swallowed, as a border; the button regions
// are the program's.

#include "chrome.h"
#include "macos.h"

#include <math.h>

NSWindowStyleMask mwinMacStyleMaskOf(mwinWindowStyle style)
{
    NSWindowStyleMask mask = NSWindowStyleMaskBorderless;
    if ((style & (mwin_styleDecorated | mwin_styleCustomChrome)) != 0)
    {
        mask |=
            NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
    }
    if ((style & mwin_styleCustomChrome) != 0)
    {
        mask |= NSWindowStyleMaskFullSizeContentView;
    }
    if ((style & mwin_styleResizable) != 0)
    {
        mask |= NSWindowStyleMaskResizable;
    }
    return mask;
}

void mwinMacApplyStyle(NSWindow* window, mwinWindowStyle style)
{
    bool custom = (style & mwin_styleCustomChrome) != 0;
    window.styleMask = mwinMacStyleMaskOf(style);
    window.titlebarAppearsTransparent = custom;
    window.titleVisibility = custom ? NSWindowTitleHidden : NSWindowTitleVisible;
    static const NSWindowButton buttons[] = {NSWindowCloseButton, NSWindowMiniaturizeButton,
                                             NSWindowZoomButton};
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++)
    {
        [window standardWindowButton:buttons[i]].hidden = custom;
    }
    window.level =
        (style & mwin_styleAlwaysOnTop) != 0 ? NSFloatingWindowLevel : NSNormalWindowLevel;
}

// What the user set a double click on a title bar to do.
static void DoubleClick(NSWindow* window)
{
    NSString* action =
        [NSUserDefaults.standardUserDefaults stringForKey:@"AppleActionOnDoubleClick"];
    if ([action isEqual:@"Minimize"])
    {
        [window miniaturize:nil];
    }
    else if (![action isEqual:@"None"])
    {
        [window zoom:nil];
    }
}

static bool CanResize(NSWindow* window)
{
    return (window.styleMask & NSWindowStyleMaskResizable) != 0 && !window.zoomed &&
           (window.styleMask & NSWindowStyleMaskFullScreen) == 0;
}

bool mwinMacPressChrome(mwinMacPlatform* platform, uint32_t slot, NSEvent* event)
{
    mwinMacWindow* window = &platform->windows[slot];
    NSPoint point = [window->view convertPoint:event.locationInWindow fromView:nil];
    mwinHitKind kind = mwinHitAt(&platform->context->windows[slot], (float)point.x, (float)point.y);
    if (!mwinHitMoves(kind))
    {
        return false;
    }
    if (kind == mwin_hitCaption)
    {
        if (event.clickCount == 2)
        {
            DoubleClick(window->window);
        }
        else
        {
            [window->window performWindowDragWithEvent:event];
        }
        return true;
    }
    if (CanResize(window->window))
    {
        window->resizing = kind;
        window->resizeFrame = window->window.frame;
        window->resizeStart = [window->window convertPointToScreen:event.locationInWindow];
    }
    return true;
}

// A content size within the window's limits, then at its ratio, the side
// the edge moves leading.
static NSSize Bounded(NSWindow* window, NSSize size, bool widthLeads)
{
    NSSize low = window.contentMinSize;
    NSSize high = window.contentMaxSize;
    size.width = fmin(fmax(size.width, low.width), high.width);
    size.height = fmin(fmax(size.height, low.height), high.height);
    NSSize ratio = window.contentAspectRatio;
    if (ratio.width > 0.0 && ratio.height > 0.0)
    {
        if (widthLeads)
        {
            size.height = size.width * ratio.height / ratio.width;
        }
        else
        {
            size.width = size.height * ratio.width / ratio.height;
        }
    }
    return size;
}

bool mwinMacDragChrome(mwinMacPlatform* platform, uint32_t slot, NSEvent* event)
{
    mwinMacWindow* window = &platform->windows[slot];
    mwinHitKind edge = window->resizing;
    if (edge == mwin_hitClient)
    {
        return false;
    }
    NSPoint now = [window->window convertPointToScreen:event.locationInWindow];
    CGFloat dx = now.x - window->resizeStart.x;
    CGFloat dy = now.y - window->resizeStart.y;
    bool left = edge == mwin_hitLeft || edge == mwin_hitTopLeft || edge == mwin_hitBottomLeft;
    bool right = edge == mwin_hitRight || edge == mwin_hitTopRight || edge == mwin_hitBottomRight;
    bool top = edge == mwin_hitTop || edge == mwin_hitTopLeft || edge == mwin_hitTopRight;
    bool bottom =
        edge == mwin_hitBottom || edge == mwin_hitBottomLeft || edge == mwin_hitBottomRight;
    NSRect start = window->resizeFrame;
    NSRect content = [window->window contentRectForFrameRect:start];
    // AppKit's y runs up: the top edge moves with the frame's top.
    NSSize wanted = NSMakeSize(content.size.width + (right  ? dx
                                                     : left ? -dx
                                                            : 0.0),
                               content.size.height + (top      ? dy
                                                      : bottom ? -dy
                                                               : 0.0));
    NSSize size = Bounded(window->window, wanted, left || right);
    NSRect frame =
        [window->window frameRectForContentRect:NSMakeRect(NSMinX(content), NSMinY(content),
                                                           size.width, size.height)];
    // The far edges stay where they were.
    frame.origin.x = left ? NSMaxX(start) - NSWidth(frame) : NSMinX(start);
    frame.origin.y = bottom ? NSMaxY(start) - NSHeight(frame) : NSMinY(start);
    [window->window setFrame:frame display:YES];
    return true;
}

bool mwinMacReleaseChrome(mwinMacPlatform* platform, uint32_t slot)
{
    mwinMacWindow* window = &platform->windows[slot];
    bool resizing = window->resizing != mwin_hitClient;
    window->resizing = mwin_hitClient;
    return resizing;
}

mwinOutcome mwinMacSetLimits(mwinMacPlatform* platform, uint32_t slot, mwinSize minimum,
                             mwinSize maximum)
{
    NSWindow* window = platform->windows[slot].window;
    window.contentMinSize = NSMakeSize((CGFloat)minimum.width, (CGFloat)minimum.height);
    window.contentMaxSize =
        NSMakeSize(maximum.width > 0.0f ? (CGFloat)maximum.width : CGFLOAT_MAX,
                   maximum.height > 0.0f ? (CGFloat)maximum.height : CGFLOAT_MAX);
    // A window outside its new limits is brought into them.
    NSSize current = [window contentRectForFrameRect:window.frame].size;
    NSSize bounded = Bounded(window, current, true);
    if (!NSEqualSizes(current, bounded))
    {
        [window setContentSize:bounded];
    }
    return mwin_outcomeDone;
}

mwinOutcome mwinMacSetAspect(mwinMacPlatform* platform, uint32_t slot, uint32_t width,
                             uint32_t height)
{
    NSWindow* window = platform->windows[slot].window;
    if (width == 0 || height == 0)
    {
        // Increments and a ratio exclude each other: steps of one lift it.
        window.contentResizeIncrements = NSMakeSize(1.0, 1.0);
    }
    else
    {
        window.contentAspectRatio = NSMakeSize(width, height);
    }
    return mwin_outcomeDone;
}

mwinOutcome mwinMacSetOpacity(mwinMacPlatform* platform, uint32_t slot, float opacity)
{
    platform->windows[slot].window.alphaValue = (CGFloat)opacity;
    return mwin_outcomeDone;
}
