// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The system's preferences and facts on macOS (macos.h): the light or
// dark appearance of the application, the accent color, reduced motion,
// the power source and Low Power Mode, and the preferred languages.
// macOS has no text scale for every program, so it is 1. Each change is
// announced on the main thread (the appearance by key-value observing,
// the power source by a run loop source, the rest by notifications)
// and reads everything again; the core posts what changed.

#include "apple_locale.h"
#include "macos.h"

#import <IOKit/ps/IOPSKeys.h>
#import <IOKit/ps/IOPowerSources.h>
#include <math.h>
#include <string.h>

// What watches the application's appearance.
@interface MwinMacAppearanceWatcher : NSObject
{
  @public
    mwinMacPlatform* platform;
}
@end

@implementation MwinMacAppearanceWatcher
- (void)observeValueForKeyPath:(NSString*)path
                      ofObject:(id)object
                        change:(NSDictionary*)change
                       context:(void*)context
{
    (void)path;
    (void)object;
    (void)change;
    (void)context;
    mwinMacReadSystem(platform);
}
@end

static mwinTheme ThemeOf(void)
{
    NSAppearanceName name = [NSApp.effectiveAppearance
        bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]];
    return [name isEqual:NSAppearanceNameDarkAqua] ? mwin_themeDark
           : name != nil                           ? mwin_themeLight
                                                   : mwin_themeUnknown;
}

static uint32_t Channel(CGFloat value)
{
    return (uint32_t)lround(value <= 0.0 ? 0.0 : value >= 1.0 ? 255.0 : value * 255.0);
}

// The accent color as 0xRRGGBBAA, in sRGB.
static bool AccentOf(uint32_t* accent)
{
    NSColor* color = [NSColor.controlAccentColor colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    if (color == nil)
    {
        return false;
    }
    *accent = Channel(color.redComponent) << 24 | Channel(color.greenComponent) << 16 |
              Channel(color.blueComponent) << 8 | Channel(color.alphaComponent);
    return true;
}

// On battery when the battery provides the power; unknown without a
// power source to say.
static mwinTristate OnBatteryOf(void)
{
    CFTypeRef info = IOPSCopyPowerSourcesInfo();
    CFStringRef source = info != nullptr ? IOPSGetProvidingPowerSourceType(info) : nullptr;
    mwinTristate onBattery = source == nullptr                              ? mwin_unknown
                             : CFEqual(source, CFSTR(kIOPMBatteryPowerKey)) ? mwin_yes
                                                                            : mwin_no;
    if (info != nullptr)
    {
        CFRelease(info);
    }
    return onBattery;
}

static mwinTristate LowPowerOf(void)
{
    if (@available(macOS 12.0, *))
    {
        return NSProcessInfo.processInfo.lowPowerModeEnabled ? mwin_yes : mwin_no;
    }
    return mwin_unknown;
}

void mwinMacReadSystem(mwinMacPlatform* platform)
{
    @autoreleasepool
    {
        uint64_t nowNs = mwinMacNow();
        mwinSystemFacts facts = {
            .theme = ThemeOf(),
            .reducedMotion = NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion,
            .textScale = 1.0f,
            .onBattery = OnBatteryOf(),
            .lowPower = LowPowerOf()};
        facts.hasAccent = AccentOf(&facts.accent);
        mwinSetSystemFacts(platform->context, &facts, nowNs);
        mwinAppleReadLocales(platform->context, nowNs);
    }
}

static void OnPowerSource(void* data)
{
    mwinMacReadSystem(data);
}

static id Watch(mwinMacPlatform* platform, NSNotificationCenter* center, NSNotificationName name)
{
    // Some come on other threads: each is taken on the main one.
    return [[center addObserverForName:name
                                object:nil
                                 queue:NSOperationQueue.mainQueue
                            usingBlock:^(NSNotification* note) {
                              (void)note;
                              mwinMacReadSystem(platform);
                            }] retain];
}

void mwinMacWatchSystem(mwinMacPlatform* platform)
{
    NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
    platform->systemObservers[0] = Watch(platform, center, NSSystemColorsDidChangeNotification);
    platform->systemObservers[1] = Watch(platform, center, NSCurrentLocaleDidChangeNotification);
    platform->systemObservers[2] =
        Watch(platform, NSWorkspace.sharedWorkspace.notificationCenter,
              NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification);
    if (@available(macOS 12.0, *))
    {
        platform->systemObservers[3] =
            Watch(platform, center, NSProcessInfoPowerStateDidChangeNotification);
    }
    MwinMacAppearanceWatcher* watcher = [[MwinMacAppearanceWatcher alloc] init];
    watcher->platform = platform;
    [NSApp addObserver:watcher forKeyPath:@"effectiveAppearance" options:0 context:nullptr];
    platform->appearanceWatcher = watcher;
    CFRunLoopSourceRef power = IOPSNotificationCreateRunLoopSource(OnPowerSource, platform);
    if (power != nullptr)
    {
        CFRunLoopAddSource(CFRunLoopGetMain(), power, kCFRunLoopCommonModes);
        platform->powerSource = power;
    }
    mwinMacReadSystem(platform);
}

void mwinMacUnwatchSystem(mwinMacPlatform* platform)
{
    NSNotificationCenter* centers[4] = {
        NSNotificationCenter.defaultCenter, NSNotificationCenter.defaultCenter,
        NSWorkspace.sharedWorkspace.notificationCenter, NSNotificationCenter.defaultCenter};
    for (int i = 0; i < 4; i++)
    {
        if (platform->systemObservers[i] != nil)
        {
            [centers[i] removeObserver:platform->systemObservers[i]];
            [platform->systemObservers[i] release];
        }
    }
    if (platform->appearanceWatcher != nil)
    {
        [NSApp removeObserver:platform->appearanceWatcher forKeyPath:@"effectiveAppearance"];
        [platform->appearanceWatcher release];
    }
    if (platform->powerSource != nullptr)
    {
        CFRunLoopSourceInvalidate((CFRunLoopSourceRef)platform->powerSource);
        CFRelease(platform->powerSource);
    }
}
