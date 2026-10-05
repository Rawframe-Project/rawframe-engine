// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The iOS backend (mwin-0025). UIKit owns the loop and the main thread:
// mwinRun hands the context to the library's application delegate
// through the main thread's dictionary (the library keeps no global)
// and calls UIApplicationMain, which never returns. The application
// takes the scene life cycle, as current SDKs require: the delegate
// gives every scene the library's scene delegate, so the program's
// Info.plist names no class, only a UIApplicationSceneManifest. The
// program's init runs when the first scene connects, so that its
// windows have a scene to take; frames come from a display link on the
// main run loop once a scene is in the foreground. When the last scene
// leaves the foreground the program is told the application is
// suspending, in a frame of its own, then suspended, and frames pause;
// the first scene back resumes them the same way. Lifecycle records
// coalesce: a suspended one still waiting when the application resumes
// gives way to the resuming one. The frame that stops the program, or the application's
// end, calls quit and frees the context. Nothing returns from
// UIApplicationMain, and the library never ends the process: the
// application runs on without frames until the system ends it, or the
// program does from quit.

#include "allocator.h"
#include "backend.h"
#include "ios.h"

#include <crt_externs.h>
#include <string.h>
#include <time.h>

// Where mwinRun leaves the context for the application delegate.
#define CONTEXT_KEY @"maul-window context"

// What the display link calls: a frame of the program, or its end.
@interface MwinIOSStepper : NSObject
{
  @public
    mwinContext* context;
}
- (void)step:(CADisplayLink*)link;
@end

@interface MwinIOSAppDelegate : UIResponder <UIApplicationDelegate>
{
  @public
    mwinContext* context;
}
@end

@interface MwinIOSSceneDelegate : UIResponder <UIWindowSceneDelegate>
@end

// Where the platform block's parts lie, laid out with checked
// arithmetic: the platform, its windows and its monitors' screens.
typedef struct PlatformParts
{
    mwinLayout layout;
    size_t windows;
    size_t screens;
    size_t screenInfo;
} PlatformParts;

static PlatformParts PartsOf(const mwinLimits* limits)
{
    PlatformParts parts = {0};
    mwinLayout* layout = &parts.layout;
    (void)mwinLayoutAdd(layout, 1, sizeof(mwinIOSPlatform), alignof(mwinIOSPlatform));
    parts.windows =
        mwinLayoutAdd(layout, limits->windows, sizeof(mwinIOSWindow), alignof(mwinIOSWindow));
    parts.screens = mwinLayoutAdd(layout, limits->monitors, sizeof(id), alignof(id));
    parts.screenInfo =
        mwinLayoutAdd(layout, limits->monitors, sizeof(mwinMonitorInfo), alignof(mwinMonitorInfo));
    return parts;
}

mwinIOSPlatform* mwinIOSPlatformOf(const mwinContext* context)
{
    return (mwinIOSPlatform*)context->backendData;
}

uint64_t mwinIOSNow(void)
{
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

mwinContext* mwinIOSCurrentContext(void)
{
    id delegate = UIApplication.sharedApplication.delegate;
    return [delegate isKindOfClass:[MwinIOSAppDelegate class]]
               ? ((MwinIOSAppDelegate*)delegate)->context
               : nullptr;
}

// The platform, from any of UIKit's callbacks; null before the program
// started or after it ended.
static mwinIOSPlatform* CurrentPlatform(void)
{
    mwinContext* context = mwinIOSCurrentContext();
    return context != nullptr ? mwinIOSPlatformOf(context) : nullptr;
}

// Between frames the display is kept awake as the windows ask, pickers
// whose requests went are taken away, and the pads are read.
static void Pump(mwinContext* context)
{
    mwinIOSPlatform* platform = mwinIOSPlatformOf(context);
    mwinIOSKeepAwake(platform, mwinWantsAwake(context));
    mwinIOSPumpDialogs(platform);
#ifdef MAUL_WINDOW_GAMEPAD
    mwinApplePumpPads(&platform->pads, mwinIOSNow());
#endif
}

// Ends the program: quit, and the context freed. UIKit's callbacks find
// no program after it.
static void Finish(mwinContext* context)
{
    id delegate = UIApplication.sharedApplication.delegate;
    if ([delegate isKindOfClass:[MwinIOSAppDelegate class]])
    {
        ((MwinIOSAppDelegate*)delegate)->context = nullptr;
    }
    (void)mwinEndProgram(context);
    mwinFinishRun(context);
}

@implementation MwinIOSStepper
- (void)step:(CADisplayLink*)link
{
    (void)link;
    @autoreleasepool
    {
        if (!mwinStepProgram(context, Pump))
        {
            Finish(context);
        }
    }
}
@end

static void PostLifecycle(mwinContext* context, mwinEventType type)
{
    mwinEvent event = {.type = type, .timeNs = mwinIOSNow()};
    mwinPostGlobal(context, &event);
}

// The application went away or came back: the program hears of it at
// once, in a frame of its own, which may stop it.
static void Lifecycle(mwinIOSPlatform* platform, bool running)
{
    mwinContext* context = platform->context;
    platform->suspended = !running;
    platform->link.paused = !running;
    // Suspending resets the input state: keys held are forgotten.
    memset(platform->held, 0, sizeof(platform->held));
    PostLifecycle(context, running ? mwin_eventResuming : mwin_eventSuspending);
    mwinRunCriticalFrame(context);
    if (context->stopping)
    {
        Finish(context);
        return;
    }
    PostLifecycle(context, running ? mwin_eventResumed : mwin_eventSuspended);
}

@implementation MwinIOSSceneDelegate
- (void)scene:(UIScene*)scene
    willConnectToSession:(UISceneSession*)session
                 options:(UISceneConnectionOptions*)options
{
    (void)options;
    mwinIOSPlatform* platform = CurrentPlatform();
    if (platform == nullptr)
    {
        return;
    }
    if ([session.role isEqual:UIWindowSceneSessionRoleApplication] &&
        platform->waitingScene == nil && [scene isKindOfClass:[UIWindowScene class]])
    {
        platform->waitingScene = (UIWindowScene*)[scene retain];
    }
    mwinIOSReadScreens(platform, mwinIOSNow());
    if (platform->link != nil)
    {
        return;
    }
    // The first scene: the system's facts are watched, and the program
    // starts, and its frames.
    mwinContext* context = platform->context;
    mwinIOSWatchSystem(platform);
    if (!mwinStartProgram(context))
    {
        Finish(context);
        return;
    }
    platform->link = [[CADisplayLink displayLinkWithTarget:platform->stepper
                                                  selector:@selector(step:)] retain];
    // Frames begin once a scene comes to the foreground: a scene the
    // system connects ahead (prewarming) runs none.
    platform->link.paused = YES;
    [platform->link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

- (void)sceneDidDisconnect:(UIScene*)scene
{
    mwinIOSPlatform* platform = CurrentPlatform();
    if (platform == nullptr)
    {
        return;
    }
    if (platform->waitingScene == scene)
    {
        [platform->waitingScene release];
        platform->waitingScene = nil;
    }
    // The system let the scene go: the window can no longer show.
    int32_t slot = mwinIOSSlotOfScene(platform, scene);
    if (slot >= 0)
    {
        mwinEvent event = {.type = mwin_eventCloseRequested, .timeNs = mwinIOSNow()};
        mwinPost(platform->context, (uint32_t)slot, &event);
    }
    mwinIOSReadScreens(platform, mwinIOSNow());
}

- (void)sceneWillEnterForeground:(UIScene*)scene
{
    (void)scene;
    mwinIOSPlatform* platform = CurrentPlatform();
    if (platform != nullptr && platform->suspended)
    {
        Lifecycle(platform, true);
    }
    else if (platform != nullptr)
    {
        platform->link.paused = NO;
    }
}

- (void)sceneDidEnterBackground:(UIScene*)scene
{
    mwinIOSPlatform* platform = CurrentPlatform();
    if (platform == nullptr || platform->suspended)
    {
        return;
    }
    // Another scene still in the foreground keeps the program running.
    for (UIScene* other in UIApplication.sharedApplication.connectedScenes)
    {
        if (other != scene && other.activationState != UISceneActivationStateBackground &&
            other.activationState != UISceneActivationStateUnattached)
        {
            return;
        }
    }
    Lifecycle(platform, false);
}
@end

@implementation MwinIOSAppDelegate
- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)options
{
    (void)application;
    (void)options;
    NSMutableDictionary* shared = NSThread.mainThread.threadDictionary;
    context = [shared[CONTEXT_KEY] pointerValue];
    [shared removeObjectForKey:CONTEXT_KEY];
    return YES;
}

- (UISceneConfiguration*)application:(UIApplication*)application
    configurationForConnectingSceneSession:(UISceneSession*)session
                                   options:(UISceneConnectionOptions*)options
{
    (void)application;
    (void)options;
    UISceneConfiguration* configuration =
        [[[UISceneConfiguration alloc] initWithName:nil sessionRole:session.role] autorelease];
    configuration.delegateClass = [MwinIOSSceneDelegate class];
    return configuration;
}

- (void)applicationWillTerminate:(UIApplication*)application
{
    (void)application;
    if (context != nullptr)
    {
        Finish(context);
    }
}
@end

static void Stop(mwinContext* context)
{
    mwinIOSPlatform* platform = mwinIOSPlatformOf(context);
    @autoreleasepool
    {
        [platform->link invalidate];
        [platform->link release];
        [platform->stepper release];
        mwinIOSCloseDialogs(platform, -1);
        for (uint32_t i = 0; i < context->limits.windows; i++)
        {
            if (platform->windows[i].window != nil)
            {
                mwinIOSDestroyWindow(context, i);
            }
        }
        [platform->waitingScene release];
        mwinIOSUnwatchKeyboard(platform);
        mwinIOSUnwatchSystem(platform);
        mwinIOSKeepAwake(platform, false);
#ifdef MAUL_WINDOW_GAMEPAD
        mwinAppleStopPads(&platform->pads);
#endif
        mwinIOSForgetScreens(platform);
    }
    mwinRelease(&context->allocator, platform, PartsOf(&context->limits).layout.size,
                alignof(max_align_t));
    context->backendData = nullptr;
}

static mwinResult Start(mwinContext* context)
{
    // A process without a bundle identifier (a tool, not a launched
    // application) cannot be one: UIKit would wait for it forever.
    if (NSBundle.mainBundle.bundleIdentifier.length == 0)
    {
        return mwin_errorPlatform;
    }
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
    mwinIOSPlatform* platform = (mwinIOSPlatform*)block;
    platform->windows = (mwinIOSWindow*)(block + parts.windows);
    platform->screens = (id*)(block + parts.screens);
    platform->screenInfo = (mwinMonitorInfo*)(block + parts.screenInfo);
    platform->context = context;
    context->backendData = platform;
    MwinIOSStepper* stepper = [[MwinIOSStepper alloc] init];
    stepper->context = context;
    platform->stepper = stepper;
    mwinIOSWatchKeyboard(platform);
#ifdef MAUL_WINDOW_GAMEPAD
    mwinAppleStartPads(&platform->pads, context);
#endif
    return mwin_success;
}

static mwinResult Run(mwinContext* context)
{
    @autoreleasepool
    {
        NSThread.mainThread.threadDictionary[CONTEXT_KEY] = [NSValue valueWithPointer:context];
        NSString* delegate = NSStringFromClass([MwinIOSAppDelegate class]);
        (void)UIApplicationMain(*_NSGetArgc(), *_NSGetArgv(), nil, delegate);
    }
    return mwin_errorPlatform;
}

static uint64_t Now(const mwinContext* context)
{
    (void)context;
    return mwinIOSNow();
}

static mwinKey MapKeyCode(const mwinContext* context, mwinKeyCode code)
{
    return mwinIOSMapKeyCode(mwinIOSPlatformOf(context), code);
}

static mwinResult KeyboardLayout(const mwinContext* context, char* buffer, size_t capacity,
                                 size_t* lengthOut)
{
    return mwinIOSKeyboardLayout(mwinIOSPlatformOf(context), buffer, capacity, lengthOut);
}

static void NativeHandles(const mwinContext* context, uint32_t slot, mwinNativeHandles* out)
{
    const mwinIOSWindow* window = &mwinIOSPlatformOf(context)->windows[slot];
    out->platform = mwin_platformIOS;
    out->handles.apple.view = (void*)window->view;
    out->handles.apple.layer = (void*)window->layer;
}

static mwinResult Rumble(mwinContext* context, uint32_t slot, float low, float high,
                         uint32_t durationMs)
{
#ifdef MAUL_WINDOW_GAMEPAD
    mwinIOSPlatform* platform = mwinIOSPlatformOf(context);
    @autoreleasepool
    {
        return mwinPadTrackerRumble(&platform->pads.tracker, slot, low, high, durationMs,
                                    mwinIOSNow());
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

const mwinBackendOps mwinIOSBackend = {
    Start,         Stop, Run,        mwinIOSCreateWindow, mwinIOSDestroyWindow,
    mwinIOSSubmit, Now,  MapKeyCode, KeyboardLayout,      NativeHandles,
    Rumble,
};
