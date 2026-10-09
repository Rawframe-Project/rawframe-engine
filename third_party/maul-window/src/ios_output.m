// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// iOS monitors (ios.h): the screens the connected scenes show on, each a
// monitor while a scene shows on it. The device's own screen, the one
// the application's scenes use, is primary and at the origin; external
// displays lie to its right in the order found. Bounds are in pixels at
// the screen's scale, in the orientation of the interface; iOS has no
// work area beside a window's safe area, so the work area is the
// screen. A screen above 60 Hz is ProMotion's, whose rate varies. iOS
// tells no display's physical size.

#include "ios.h"

#include <math.h>
#include <string.h>

int32_t mwinIOSMonitorOf(const mwinIOSPlatform* platform, UIScreen* screen)
{
    for (uint32_t i = 0; screen != nil && i < platform->context->limits.monitors; i++)
    {
        if (platform->screens[i] == screen)
        {
            return (int32_t)i;
        }
    }
    return -1;
}

// The screens the connected scenes show on, the application's first.
static NSArray<UIScreen*>* ScreensOf(void)
{
    NSMutableArray<UIScreen*>* screens = [NSMutableArray array];
    for (int pass = 0; pass < 2; pass++)
    {
        for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
        {
            bool application = [scene.session.role isEqual:UIWindowSceneSessionRoleApplication];
            if (![scene isKindOfClass:[UIWindowScene class]] || application != (pass == 0))
            {
                continue;
            }
            UIScreen* screen = ((UIWindowScene*)scene).screen;
            if (screen != nil && ![screens containsObject:screen])
            {
                [screens addObject:screen];
            }
        }
    }
    return screens;
}

static mwinMonitorInfo InfoOf(UIScreen* screen, bool primary, CGFloat left)
{
    mwinMonitorInfo info = {0};
    static const char internal[] = "Built-in display";
    static const char external[] = "External display";
    const char* name = primary ? internal : external;
    size_t length = primary ? sizeof(internal) - 1 : sizeof(external) - 1;
    memcpy(info.name, name, length);
    info.nameLength = (uint32_t)length;
    CGFloat scale = screen.scale;
    CGRect bounds = screen.bounds;
    info.bounds = (mwinPixelRect){(int32_t)lround(left * scale), 0,
                                  (uint32_t)lround(CGRectGetWidth(bounds) * scale),
                                  (uint32_t)lround(CGRectGetHeight(bounds) * scale)};
    info.workArea = info.bounds;
    info.scale = (float)scale;
    info.refreshMilliHz = (uint32_t)screen.maximumFramesPerSecond * 1000u;
    info.variableRefresh = screen.maximumFramesPerSecond > 60;
    // Extended dynamic range as on macOS (mwin-0036), from iOS 16.
    if (@available(iOS 16.0, *))
    {
        CGFloat potential = screen.potentialEDRHeadroom;
        info.hdr = (mwinHdrFacts){
            .known = true,
            .active = screen.currentEDRHeadroom > 1.0,
            .headroom = potential > 1.0 ? (float)potential : 1.0f,
        };
    }
    info.primary = primary;
    return info;
}

void mwinIOSReadScreens(mwinIOSPlatform* platform, uint64_t timeNs)
{
    mwinContext* context = platform->context;
    NSArray<UIScreen*>* screens = ScreensOf();
    // Monitors whose screen no scene shows on.
    for (uint32_t i = 0; i < context->limits.monitors; i++)
    {
        if (platform->screens[i] != nil && ![screens containsObject:platform->screens[i]])
        {
            mwinRemoveMonitor(context, i, timeNs);
            [platform->screens[i] release];
            platform->screens[i] = nil;
        }
    }
    CGFloat left = 0.0;
    for (NSUInteger s = 0; s < screens.count; s++)
    {
        UIScreen* screen = screens[s];
        mwinMonitorInfo info = InfoOf(screen, s == 0, left);
        left += CGRectGetWidth(screen.bounds);
        int32_t slot = mwinIOSMonitorOf(platform, screen);
        if (slot >= 0)
        {
            if (!mwinSameMonitorInfo(&info, &platform->screenInfo[slot]))
            {
                platform->screenInfo[slot] = info;
                mwinChangeMonitor(context, (uint32_t)slot, &info, timeNs);
            }
            continue;
        }
        slot = mwinAddMonitor(context, &info, timeNs);
        if (slot >= 0)
        {
            platform->screens[slot] = [screen retain];
            platform->screenInfo[slot] = info;
        }
    }
}

void mwinIOSForgetScreens(mwinIOSPlatform* platform)
{
    for (uint32_t i = 0; i < platform->context->limits.monitors; i++)
    {
        [platform->screens[i] release];
        platform->screens[i] = nil;
    }
}
