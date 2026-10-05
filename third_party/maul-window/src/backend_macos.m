// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The macOS backend (mwin-0024). AppKit owns the loop: mwinRun calls
// init, then runs the application, whose run loop calls the program's
// frames from a display link of the main screen (a timer at 60 Hz before
// macOS 14, or without a screen); the frame that stops the program stops
// the application, and mwinRun calls quit and returns. AppKit delivers
// the platform's events to the windows' views and delegates between
// frames, which post them at once. Every entry point that touches
// Objective-C objects drains its own autorelease pool.

#include "allocator.h"
#include "backend.h"
#include "macos.h"

#include <string.h>
#include <time.h>

// What the display link or the timer calls: a frame of the program, or
// its end.
@interface MwinMacStepper : NSObject
{
  @public
    mwinContext* context;
}
- (void)step:(id)sender;
@end

// Where the platform block's parts lie, laid out with checked
// arithmetic: the platform, its windows and its monitors' displays.
typedef struct PlatformParts
{
    mwinLayout layout;
    size_t windows;
    size_t displays;
} PlatformParts;

static PlatformParts PartsOf(const mwinLimits* limits)
{
    PlatformParts parts = {0};
    mwinLayout* layout = &parts.layout;
    (void)mwinLayoutAdd(layout, 1, sizeof(mwinMacPlatform), alignof(mwinMacPlatform));
    parts.windows =
        mwinLayoutAdd(layout, limits->windows, sizeof(mwinMacWindow), alignof(mwinMacWindow));
    parts.displays = mwinLayoutAdd(layout, limits->monitors, sizeof(uint32_t), alignof(uint32_t));
    return parts;
}

mwinMacPlatform* mwinMacPlatformOf(const mwinContext* context)
{
    return (mwinMacPlatform*)context->backendData;
}

uint64_t mwinMacNow(void)
{
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

// Platform events come to their objects between frames; the display is
// kept awake as the windows ask, panels whose requests went are
// cancelled, and the pads are read.
static void Pump(mwinContext* context)
{
    mwinMacPlatform* platform = mwinMacPlatformOf(context);
    mwinMacKeepAwake(platform, mwinWantsAwake(context));
    mwinMacPumpDialogs(platform);
#ifdef MAUL_WINDOW_GAMEPAD
    mwinApplePumpPads(&platform->pads, mwinMacNow());
#endif
}

// Stops the frames and the application, whose run then returns once it
// takes the event posted to wake it.
static void StopApplication(mwinMacPlatform* platform)
{
    if (platform->driver != nil)
    {
        [platform->driver invalidate];
        [platform->driver release];
        platform->driver = nil;
    }
    [NSApp stop:nil];
    NSEvent* wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                       location:NSZeroPoint
                                  modifierFlags:0
                                      timestamp:0
                                   windowNumber:0
                                        context:nil
                                        subtype:0
                                          data1:0
                                          data2:0];
    [NSApp postEvent:wake atStart:YES];
}

@implementation MwinMacStepper
- (void)step:(id)sender
{
    (void)sender;
    @autoreleasepool
    {
        if (!mwinStepProgram(context, Pump))
        {
            StopApplication(mwinMacPlatformOf(context));
        }
    }
}
@end

// Starts what calls the frames: the main screen's display link on
// macOS 14, else a timer, both on the main run loop in its common modes
// so that frames go on while a window is resized.
static void StartFrames(mwinMacPlatform* platform)
{
    NSScreen* screen = [NSScreen mainScreen];
    if (@available(macOS 14.0, *))
    {
        if (screen != nil)
        {
            CADisplayLink* link = [screen displayLinkWithTarget:platform->stepper
                                                       selector:@selector(step:)];
            [link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
            platform->driver = [link retain];
            return;
        }
    }
    NSTimer* timer = [NSTimer timerWithTimeInterval:1.0 / 60.0
                                             target:platform->stepper
                                           selector:@selector(step:)
                                           userInfo:nil
                                            repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:timer forMode:NSRunLoopCommonModes];
    platform->driver = [timer retain];
}

static void Stop(mwinContext* context)
{
    mwinMacPlatform* platform = mwinMacPlatformOf(context);
    @autoreleasepool
    {
        if (platform->driver != nil)
        {
            [platform->driver invalidate];
            [platform->driver release];
        }
        if (platform->screensObserver != nil)
        {
            [[NSNotificationCenter defaultCenter] removeObserver:platform->screensObserver];
            [platform->screensObserver release];
        }
        [platform->stepper release];
#ifdef MAUL_WINDOW_GAMEPAD
        mwinAppleStopPads(&platform->pads);
#endif
        mwinMacUnwatchKeyboard(platform);
        mwinMacUnwatchSystem(platform);
        mwinMacForgetCursors(platform);
        mwinMacKeepAwake(platform, false);
        mwinMacCloseDialogs(platform, -1);
        mwinMacForgetScreens(platform);
    }
    mwinRelease(&context->allocator, platform, PartsOf(&context->limits).layout.size,
                alignof(max_align_t));
    context->backendData = nullptr;
}

static mwinResult Start(mwinContext* context)
{
    PlatformParts parts = PartsOf(&context->limits);
    unsigned char* block =
        parts.layout.overflow
            ? nullptr
            : mwinAllocate(&context->allocator, parts.layout.size, alignof(max_align_t));
    if (block == nullptr)
    {
        return mwin_errorCapacity;
    }
    memset(block, 0, parts.layout.size);
    mwinMacPlatform* platform = (mwinMacPlatform*)block;
    platform->windows = (mwinMacWindow*)(block + parts.windows);
    platform->displays = (uint32_t*)(block + parts.displays);
    platform->context = context;
    context->backendData = platform;
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        MwinMacStepper* stepper = [[MwinMacStepper alloc] init];
        stepper->context = context;
        platform->stepper = stepper;
        platform->screensObserver = [[[NSNotificationCenter defaultCenter]
            addObserverForName:NSApplicationDidChangeScreenParametersNotification
                        object:nil
                         queue:nil
                    usingBlock:^(NSNotification* note) {
                      (void)note;
                      mwinMacReadScreens(platform, mwinMacNow());
                    }] retain];
        mwinMacReadScreens(platform, mwinMacNow());
        mwinMacWatchKeyboard(platform);
        mwinMacWatchSystem(platform);
#ifdef MAUL_WINDOW_GAMEPAD
        mwinAppleStartPads(&platform->pads, context);
#endif
    }
    return mwin_success;
}

static mwinResult Run(mwinContext* context)
{
    if (!mwinStartProgram(context))
    {
        return mwinEndProgram(context);
    }
    @autoreleasepool
    {
        StartFrames(mwinMacPlatformOf(context));
        if (@available(macOS 14.0, *))
        {
            [NSApp activate];
        }
        [NSApp run];
    }
    return mwinEndProgram(context);
}

static uint64_t Now(const mwinContext* context)
{
    (void)context;
    return mwinMacNow();
}

static mwinKey MapKeyCode(const mwinContext* context, mwinKeyCode code)
{
    return mwinMacMapKeyCode(mwinMacPlatformOf(context), code);
}

static mwinResult KeyboardLayout(const mwinContext* context, char* buffer, size_t capacity,
                                 size_t* lengthOut)
{
    return mwinMacKeyboardLayout(mwinMacPlatformOf(context), buffer, capacity, lengthOut);
}

static void NativeHandles(const mwinContext* context, uint32_t slot, mwinNativeHandles* out)
{
    const mwinMacWindow* window = &mwinMacPlatformOf(context)->windows[slot];
    out->platform = mwin_platformMacOS;
    out->handles.apple.view = (void*)window->view;
    out->handles.apple.layer = (void*)window->layer;
}

static mwinResult Rumble(mwinContext* context, uint32_t slot, float low, float high,
                         uint32_t durationMs)
{
#ifdef MAUL_WINDOW_GAMEPAD
    mwinMacPlatform* platform = mwinMacPlatformOf(context);
    @autoreleasepool
    {
        return mwinPadTrackerRumble(&platform->pads.tracker, slot, low, high, durationMs,
                                    mwinMacNow());
    }
#else
    (void)context;
    (void)slot;
    (void)low;
    (void)high;
    (void)durationMs;
    return mwin_errorUnsupported;
#endif
}

const mwinBackendOps mwinMacBackend = {
    Start,         Stop, Run,        mwinMacCreateWindow, mwinMacDestroyWindow,
    mwinMacSubmit, Now,  MapKeyCode, KeyboardLayout,      NativeHandles,
    Rumble,
};
