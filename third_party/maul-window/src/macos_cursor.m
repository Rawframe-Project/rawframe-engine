// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Cursors on macOS (macos.h). A window's cursor is its view's cursor
// rectangle: the shape's system cursor, or a blank one while the mode
// hides it. A captured cursor is moved to the view's middle, parted from
// the mouse and hidden while the window is the key window, its motion
// coming as the events' deltas; it comes back when the window loses
// focus. macOS has no way to keep a cursor inside a window short of
// warping it back after it left, so confinement is unsupported. Shapes
// macOS lacks (the diagonal resizes before macOS 15, waiting and
// progress) are the arrow.

#include "macos.h"

static NSCursor* ShapeCursor(mwinCursorShape shape)
{
    switch (shape)
    {
    case mwin_shapeText:
        return [NSCursor IBeamCursor];
    case mwin_shapePointer:
        return [NSCursor pointingHandCursor];
    case mwin_shapeCrosshair:
        return [NSCursor crosshairCursor];
    case mwin_shapeMove:
        return [NSCursor openHandCursor];
    case mwin_shapeResizeEastWest:
        return [NSCursor resizeLeftRightCursor];
    case mwin_shapeResizeNorthSouth:
        return [NSCursor resizeUpDownCursor];
    case mwin_shapeResizeNortheastSouthwest:
        if (@available(macOS 15.0, *))
        {
            return [NSCursor frameResizeCursorFromPosition:NSCursorFrameResizePositionTopRight
                                              inDirections:NSCursorFrameResizeDirectionsAll];
        }
        return [NSCursor arrowCursor];
    case mwin_shapeResizeNorthwestSoutheast:
        if (@available(macOS 15.0, *))
        {
            return [NSCursor frameResizeCursorFromPosition:NSCursorFrameResizePositionTopLeft
                                              inDirections:NSCursorFrameResizeDirectionsAll];
        }
        return [NSCursor arrowCursor];
    case mwin_shapeNotAllowed:
        return [NSCursor operationNotAllowedCursor];
    default:
        return [NSCursor arrowCursor];
    }
}

static bool IsHidden(mwinCursorMode mode)
{
    return mode == mwin_cursorHidden || mode == mwin_cursorCaptured;
}

NSCursor* mwinMacCursorOf(mwinMacPlatform* platform, const mwinMacWindow* window)
{
    if (!IsHidden(window->cursorMode))
    {
        return ShapeCursor(window->cursorShape);
    }
    if (platform->blankCursor == nil)
    {
        NSImage* image = [[NSImage alloc] initWithSize:NSMakeSize(1.0, 1.0)];
        platform->blankCursor = [[NSCursor alloc] initWithImage:image hotSpot:NSZeroPoint];
        [image release];
    }
    return platform->blankCursor;
}

// Shows the window's cursor at once where the pointer is over it.
static void Refresh(mwinMacPlatform* platform, const mwinMacWindow* window)
{
    [window->window invalidateCursorRectsForView:window->view];
    if (window->pointerInside)
    {
        [mwinMacCursorOf(platform, window) set];
    }
}

// Moves the cursor to the middle of a window's view, on the display's
// top-left coordinates.
static void Center(const mwinMacWindow* window)
{
    NSRect bounds = window->view.bounds;
    NSRect inWindow = [window->view convertRect:bounds toView:nil];
    NSRect onScreen = [window->window convertRectToScreen:inWindow];
    CGPoint middle = CGPointMake(NSMidX(onScreen), mwinMacPrimaryHeight() - NSMidY(onScreen));
    CGWarpMouseCursorPosition(middle);
}

static void Release(mwinMacPlatform* platform)
{
    if (platform->captured == 0)
    {
        return;
    }
    CGAssociateMouseAndMouseCursorPosition(true);
    [NSCursor unhide];
    platform->captured = 0;
}

void mwinMacApplyCapture(mwinMacPlatform* platform, uint32_t slot, bool focused)
{
    mwinMacWindow* window = &platform->windows[slot];
    bool wanted = focused && window->cursorMode == mwin_cursorCaptured;
    if (wanted && platform->captured != slot + 1)
    {
        Release(platform);
        Center(window);
        CGAssociateMouseAndMouseCursorPosition(false);
        [NSCursor hide];
        platform->captured = slot + 1;
    }
    else if (!wanted && platform->captured == slot + 1)
    {
        Release(platform);
    }
}

mwinOutcome mwinMacSetCursorMode(mwinMacPlatform* platform, uint32_t slot, mwinCursorMode mode)
{
    if (mode == mwin_cursorConfined || mode == mwin_cursorConfinedHidden)
    {
        return mwin_outcomeUnsupported;
    }
    mwinMacWindow* window = &platform->windows[slot];
    window->cursorMode = mode;
    mwinMacApplyCapture(platform, slot, window->window.keyWindow);
    Refresh(platform, window);
    return mwin_outcomeDone;
}

mwinOutcome mwinMacSetCursorShape(mwinMacPlatform* platform, uint32_t slot, mwinCursorShape shape)
{
    mwinMacWindow* window = &platform->windows[slot];
    window->cursorShape = shape;
    Refresh(platform, window);
    return mwin_outcomeDone;
}

void mwinMacForgetCursors(mwinMacPlatform* platform)
{
    Release(platform);
    [platform->blankCursor release];
    platform->blankCursor = nil;
}
