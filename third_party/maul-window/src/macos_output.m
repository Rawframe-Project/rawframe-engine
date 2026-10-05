// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// macOS monitors (macos.h): each NSScreen a monitor under its display id,
// the screen holding the menu bar first and primary. Bounds and work
// areas are in pixels at the screen's backing scale, from the top left
// of the primary screen.

#include "macos.h"

#include <math.h>
#include <string.h>

CGFloat mwinMacPrimaryHeight(void)
{
    NSArray<NSScreen*>* screens = [NSScreen screens];
    return screens.count > 0 ? NSMaxY(screens[0].frame) : 0.0;
}

static uint32_t DisplayOf(NSScreen* screen)
{
    NSNumber* number = screen.deviceDescription[@"NSScreenNumber"];
    return number != nil ? number.unsignedIntValue : 0;
}

// A rectangle of points, bottom-left based, as pixels from the top left.
static mwinPixelRect PixelsOf(NSRect rect, CGFloat scale, CGFloat primaryHeight)
{
    CGFloat top = primaryHeight - NSMaxY(rect);
    return (mwinPixelRect){
        (int32_t)lround(NSMinX(rect) * scale),
        (int32_t)lround(top * scale),
        (uint32_t)lround(NSWidth(rect) * scale),
        (uint32_t)lround(NSHeight(rect) * scale),
    };
}

// The screen's top refresh rate, and whether it varies (ProMotion); before
// macOS 12, the display mode's rate, which says nothing of variation.
static void RefreshOf(NSScreen* screen, mwinMonitorInfo* info)
{
    if (@available(macOS 12.0, *))
    {
        info->refreshMilliHz = (uint32_t)lround(screen.maximumFramesPerSecond * 1000.0);
        info->variableRefresh = screen.minimumRefreshInterval != screen.maximumRefreshInterval;
        return;
    }
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(DisplayOf(screen));
    double rate = mode != nullptr ? CGDisplayModeGetRefreshRate(mode) : 0.0;
    info->refreshMilliHz = (uint32_t)lround(rate * 1000.0);
    CGDisplayModeRelease(mode);
}

static mwinMonitorInfo InfoOf(NSScreen* screen, bool primary, CGFloat primaryHeight)
{
    mwinMonitorInfo info = {0};
    CGFloat scale = screen.backingScaleFactor;
    const char* name = screen.localizedName.UTF8String;
    size_t length = name != nullptr ? strlen(name) : 0;
    if (length > MWIN_MONITOR_NAME_BYTES)
    {
        // Cut at a character's boundary.
        length = MWIN_MONITOR_NAME_BYTES;
        while (length > 0 && ((unsigned char)name[length] & 0xC0) == 0x80)
        {
            --length;
        }
    }
    if (length > 0)
    {
        memcpy(info.name, name, length);
    }
    info.nameLength = (uint32_t)length;
    info.bounds = PixelsOf(screen.frame, scale, primaryHeight);
    info.workArea = PixelsOf(screen.visibleFrame, scale, primaryHeight);
    CGSize millimeters = CGDisplayScreenSize(DisplayOf(screen));
    info.widthMm = (uint32_t)lround(millimeters.width);
    info.heightMm = (uint32_t)lround(millimeters.height);
    info.scale = (float)scale;
    RefreshOf(screen, &info);
    info.primary = primary;
    return info;
}

int32_t mwinMacMonitorOf(const mwinMacPlatform* platform, NSScreen* screen)
{
    uint32_t display = screen != nil ? DisplayOf(screen) : 0;
    for (uint32_t i = 0; display != 0 && i < platform->context->limits.monitors; i++)
    {
        if (platform->displays[i] == display)
        {
            return (int32_t)i;
        }
    }
    return -1;
}

void mwinMacReadScreens(mwinMacPlatform* platform, uint64_t timeNs)
{
    mwinContext* context = platform->context;
    NSArray<NSScreen*>* screens = [NSScreen screens];
    CGFloat primaryHeight = mwinMacPrimaryHeight();
    // Monitors whose screen is gone.
    for (uint32_t i = 0; i < context->limits.monitors; i++)
    {
        bool present = false;
        for (NSScreen* screen in screens)
        {
            present = present || DisplayOf(screen) == platform->displays[i];
        }
        if (platform->displays[i] != 0 && !present)
        {
            mwinRemoveMonitor(context, i, timeNs);
            platform->displays[i] = 0;
        }
    }
    for (NSUInteger s = 0; s < screens.count; s++)
    {
        mwinMonitorInfo info = InfoOf(screens[s], s == 0, primaryHeight);
        int32_t slot = mwinMacMonitorOf(platform, screens[s]);
        if (slot >= 0)
        {
            mwinChangeMonitor(context, (uint32_t)slot, &info, timeNs);
            continue;
        }
        slot = mwinAddMonitor(context, &info, timeNs);
        if (slot >= 0)
        {
            platform->displays[slot] = DisplayOf(screens[s]);
        }
    }
}

void mwinMacForgetScreens(mwinMacPlatform* platform)
{
    for (uint32_t i = 0; i < platform->context->limits.monitors; i++)
    {
        platform->displays[i] = 0;
    }
}
