// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Owned windows and popups on macOS (macos.h). An owned window is a
// child window of its owner's, as AppKit has owners: kept above it,
// moving with it, gone with it. A popup (a menu, a tooltip) is also
// borderless, its place measured from its owner's content; a menu can
// take the keyboard and is asked to close when it goes elsewhere, and a
// tooltip never takes it. A child window shows whenever its parent
// does, so a window is made a child only while it shows.

#include "macos.h"

// A popup's window: borderless windows take no keyboard unless told.
@interface MwinMacPopupWindow : NSWindow
{
  @public
    bool takesKeyboard;
}
@end

@implementation MwinMacPopupWindow
- (BOOL)canBecomeKeyWindow
{
    return takesKeyboard;
}

- (BOOL)canBecomeMainWindow
{
    return NO;
}
@end

static const mwinWindowDef* DefOf(const mwinMacPlatform* platform, uint32_t slot)
{
    return &platform->context->windows[slot].def;
}

static int32_t OwnerOf(const mwinMacPlatform* platform, uint32_t slot)
{
    mwinWindowId owner = DefOf(platform, slot)->owner;
    return owner.index1 != 0 ? (int32_t)owner.index1 - 1 : -1;
}

int32_t mwinMacPopupOwner(const mwinMacPlatform* platform, uint32_t slot)
{
    return DefOf(platform, slot)->kind != mwin_windowNormal ? OwnerOf(platform, slot) : -1;
}

NSWindow* mwinMacMakeWindow(const mwinMacPlatform* platform, uint32_t slot, NSRect content,
                            NSWindowStyleMask style)
{
    mwinWindowKind kind = DefOf(platform, slot)->kind;
    if (kind == mwin_windowNormal)
    {
        return [[NSWindow alloc] initWithContentRect:content
                                           styleMask:style
                                             backing:NSBackingStoreBuffered
                                               defer:NO];
    }
    MwinMacPopupWindow* popup =
        [[MwinMacPopupWindow alloc] initWithContentRect:content
                                              styleMask:NSWindowStyleMaskBorderless
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    popup->takesKeyboard = kind == mwin_windowMenu;
    popup.hasShadow = YES;
    return popup;
}

void mwinMacPlaceNew(mwinMacPlatform* platform, uint32_t slot)
{
    NSWindow* window = platform->windows[slot].window;
    int32_t owner = OwnerOf(platform, slot);
    if (owner < 0)
    {
        [window center];
        return;
    }
    if (DefOf(platform, slot)->kind != mwin_windowNormal)
    {
        mwinMacPlace(platform, slot, DefOf(platform, slot)->position);
        return;
    }
    // An owned window over the middle of its owner.
    NSRect parent = platform->windows[owner].window.frame;
    NSRect frame = window.frame;
    [window setFrameOrigin:NSMakePoint(NSMidX(parent) - NSWidth(frame) / 2.0,
                                       NSMidY(parent) - NSHeight(frame) / 2.0)];
}

void mwinMacShow(mwinMacPlatform* platform, uint32_t slot, bool visible)
{
    NSWindow* window = platform->windows[slot].window;
    int32_t owner = OwnerOf(platform, slot);
    NSWindow* parent = owner >= 0 ? platform->windows[owner].window : nil;
    if (!visible)
    {
        [parent removeChildWindow:window];
        [window orderOut:nil];
        return;
    }
    if (parent != nil && window.parentWindow == nil)
    {
        [parent addChildWindow:window ordered:NSWindowAbove];
    }
    if (window.canBecomeKeyWindow)
    {
        [window makeKeyAndOrderFront:nil];
    }
    else
    {
        [window orderFront:nil];
    }
}

void mwinMacForgetOwner(mwinMacPlatform* platform, uint32_t slot)
{
    NSWindow* window = platform->windows[slot].window;
    [window.parentWindow removeChildWindow:window];
}

bool mwinMacIsMenu(const mwinMacPlatform* platform, uint32_t slot)
{
    return DefOf(platform, slot)->kind == mwin_windowMenu;
}
