// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The system's preferences and facts on iOS (ios.h): the light or dark
// style of the first window (or of the scenes before one), reduced
// motion, the text scale Dynamic Type gives the body text, whether the
// battery provides the power (with the device's battery watched from the
// backend's start to its stop) and Low Power Mode, and the preferred
// languages. iOS has no accent color of the user's, only each
// application's tint, so there is none. Each change is announced on the
// main thread (the style by a window's view, the rest by notifications)
// and reads everything again; the core posts what changed. A device that
// cannot watch its battery (the simulator) leaves it unknown.

#include "apple_locale.h"
#include "ios.h"

// The text the scale is measured on: the body's default size.
#define BODY_POINTS 17.0

// The traits of the first window, else of a connected scene.
static UITraitCollection* TraitsOf(const mwinIOSPlatform* platform)
{
    for (uint32_t i = 0; i < platform->context->limits.windows; i++)
    {
        if (platform->windows[i].view != nil)
        {
            return platform->windows[i].view.traitCollection;
        }
    }
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
    {
        if ([scene isKindOfClass:[UIWindowScene class]])
        {
            return ((UIWindowScene*)scene).traitCollection;
        }
    }
    return nil;
}

static mwinTheme ThemeOf(UITraitCollection* traits)
{
    switch (traits.userInterfaceStyle)
    {
    case UIUserInterfaceStyleDark:
        return mwin_themeDark;
    case UIUserInterfaceStyleLight:
        return mwin_themeLight;
    default:
        return mwin_themeUnknown;
    }
}

static float TextScaleOf(UITraitCollection* traits)
{
    if (traits == nil)
    {
        return 1.0f;
    }
    CGFloat scaled = [UIFontMetrics.defaultMetrics scaledValueForValue:BODY_POINTS
                                         compatibleWithTraitCollection:traits];
    return (float)(scaled / BODY_POINTS);
}

static mwinTristate OnBatteryOf(void)
{
    switch (UIDevice.currentDevice.batteryState)
    {
    case UIDeviceBatteryStateUnplugged:
        return mwin_yes;
    case UIDeviceBatteryStateCharging:
    case UIDeviceBatteryStateFull:
        return mwin_no;
    default:
        return mwin_unknown;
    }
}

void mwinIOSReadSystem(mwinIOSPlatform* platform)
{
    @autoreleasepool
    {
        uint64_t nowNs = mwinIOSNow();
        UITraitCollection* traits = TraitsOf(platform);
        mwinSystemFacts facts = {
            .theme = ThemeOf(traits),
            .reducedMotion = UIAccessibilityIsReduceMotionEnabled(),
            .textScale = TextScaleOf(traits),
            .onBattery = OnBatteryOf(),
            .lowPower = NSProcessInfo.processInfo.lowPowerModeEnabled ? mwin_yes : mwin_no};
        mwinSetSystemFacts(platform->context, &facts, nowNs);
        mwinAppleReadLocales(platform->context, nowNs);
    }
}

void mwinIOSWatchSystem(mwinIOSPlatform* platform)
{
    static NSString* const* const names[] = {
        &UIAccessibilityReduceMotionStatusDidChangeNotification,
        &UIContentSizeCategoryDidChangeNotification, &UIDeviceBatteryStateDidChangeNotification,
        &NSProcessInfoPowerStateDidChangeNotification, &NSCurrentLocaleDidChangeNotification};
    _Static_assert(sizeof(names) / sizeof(names[0]) == MWIN_IOS_SYSTEM_OBSERVERS,
                   "an observer per notification");
    UIDevice* device = UIDevice.currentDevice;
    platform->batteryWatched = device.batteryMonitoringEnabled;
    device.batteryMonitoringEnabled = YES;
    for (size_t i = 0; i < MWIN_IOS_SYSTEM_OBSERVERS; i++)
    {
        // Some come on other threads: each is taken on the main one.
        platform->systemObservers[i] =
            [[NSNotificationCenter.defaultCenter addObserverForName:*names[i]
                                                             object:nil
                                                              queue:NSOperationQueue.mainQueue
                                                         usingBlock:^(NSNotification* note) {
                                                           (void)note;
                                                           mwinIOSReadSystem(platform);
                                                         }] retain];
    }
    mwinIOSReadSystem(platform);
}

void mwinIOSUnwatchSystem(mwinIOSPlatform* platform)
{
    for (size_t i = 0; i < MWIN_IOS_SYSTEM_OBSERVERS; i++)
    {
        if (platform->systemObservers[i] != nil)
        {
            [NSNotificationCenter.defaultCenter removeObserver:platform->systemObservers[i]];
            [platform->systemObservers[i] release];
            platform->systemObservers[i] = nil;
        }
    }
    UIDevice.currentDevice.batteryMonitoringEnabled = platform->batteryWatched;
}
