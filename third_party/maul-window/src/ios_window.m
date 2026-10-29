// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// iOS windows (ios.h): a window is a scene's UIWindow, whose root view
// controller shows the library's view. A new window takes the scene
// that waits for one (the one the system connected at launch, or the
// last window's when it was destroyed). Without one, where the
// application supports several scenes (iPadOS), it asks UIKit for a new
// scene and is made when that scene connects; its creation fails when
// UIKit refuses, or when no scene came in SCENE_DEADLINE_NS. Elsewhere it
// is unsupported. A destroyed window's scene is let go while other
// windows remain, so no empty scene stays on the screen. A scene the
// user closes asks its window to close (mwin_eventCloseRequested), and
// the window shows no more. The system sizes and
// places scenes, so a window's size is its scene's, in points, and its
// place is on its screen; the mode is borderless full screen while the
// window covers its screen, windowed otherwise (an iPad's split view or
// Stage Manager). Requests to size, place, restyle or set the mode of a
// window are unsupported; the title is the scene's, which the app
// switcher shows.

#include "ios.h"

#include <math.h>
#include <string.h>

// How long a window waits for the scene it asked for: the deadline the
// library's other answers from outside get (linux_bus.h).
#define SCENE_DEADLINE_NS 10000000000ull

static void Post(mwinIOSPlatform* platform, uint32_t slot, mwinEvent* event)
{
    event->timeNs = mwinIOSNow();
    mwinPost(platform->context, slot, event);
}

static void PostType(mwinIOSPlatform* platform, uint32_t slot, mwinEventType type)
{
    mwinEvent event = {.type = type};
    Post(platform, slot, &event);
}

// Posts the scale, then the size in points and pixels, where they
// changed.
static void PostSize(mwinIOSPlatform* platform, uint32_t slot)
{
    mwinIOSWindow* window = &platform->windows[slot];
    CGSize points = window->view.bounds.size;
    float scale = (float)window->view.contentScaleFactor;
    mwinSize size = {(float)points.width, (float)points.height};
    uint32_t width = (uint32_t)lround(points.width * (CGFloat)scale);
    uint32_t height = (uint32_t)lround(points.height * (CGFloat)scale);
    mwinEvent event = {0};
    if (scale != window->scale)
    {
        window->scale = scale;
        event.type = mwin_eventScaleChanged;
        event.data.scale = (mwinScaleChange){scale, size};
        Post(platform, slot, &event);
    }
    if (width != window->width || height != window->height)
    {
        window->width = width;
        window->height = height;
        event.type = mwin_eventResized;
        event.data.size = size;
        Post(platform, slot, &event);
        event.type = mwin_eventPixelSizeChanged;
        event.data.pixelSize = (mwinPixelSize){width, height};
        Post(platform, slot, &event);
    }
}

static void PostMode(mwinIOSPlatform* platform, uint32_t slot)
{
    mwinIOSWindow* window = &platform->windows[slot];
    UIScreen* screen = window->scene.screen;
    mwinWindowMode mode =
        screen != nil && CGSizeEqualToSize(window->window.bounds.size, screen.bounds.size)
            ? mwin_modeBorderlessFullscreen
            : mwin_modeWindowed;
    if (mode == window->mode)
    {
        return;
    }
    window->mode = mode;
    mwinEvent event = {.type = mwin_eventModeChanged};
    event.data.mode = mode;
    Post(platform, slot, &event);
}

// The window's top left on its screen.
static void PostMove(mwinIOSPlatform* platform, uint32_t slot)
{
    mwinIOSWindow* window = &platform->windows[slot];
    UIScreen* screen = window->scene.screen;
    CGPoint place = screen != nil ? [window->window convertPoint:CGPointZero
                                               toCoordinateSpace:screen.coordinateSpace]
                                  : CGPointZero;
    mwinPosition position = {(float)place.x, (float)place.y};
    if (position.x == window->position.x && position.y == window->position.y)
    {
        return;
    }
    window->position = position;
    mwinEvent event = {.type = mwin_eventMoved};
    event.data.position = position;
    Post(platform, slot, &event);
}

static void PostSafeArea(mwinIOSPlatform* platform, uint32_t slot)
{
    mwinIOSWindow* window = &platform->windows[slot];
    UIEdgeInsets insets = window->view.safeAreaInsets;
    mwinInsets safeArea = {(float)insets.top, (float)insets.right, (float)insets.bottom,
                           (float)insets.left};
    if (safeArea.top == window->safeArea.top && safeArea.right == window->safeArea.right &&
        safeArea.bottom == window->safeArea.bottom && safeArea.left == window->safeArea.left)
    {
        return;
    }
    window->safeArea = safeArea;
    mwinEvent event = {.type = mwin_eventSafeAreaChanged};
    event.data.insets = safeArea;
    Post(platform, slot, &event);
}

static void PostMonitor(mwinIOSPlatform* platform, uint32_t slot)
{
    mwinIOSWindow* window = &platform->windows[slot];
    int32_t monitor = mwinIOSMonitorOf(platform, window->scene.screen);
    if (monitor < 0 || monitor == window->monitor)
    {
        return;
    }
    window->monitor = monitor;
    mwinEvent event = {.type = mwin_eventDisplayChanged};
    event.data.monitor = mwinMonitorIdOf(platform->context, (uint32_t)monitor);
    Post(platform, slot, &event);
}

void mwinIOSLayout(mwinIOSPlatform* platform, uint32_t slot)
{
    if (platform->windows[slot].window == nil)
    {
        return;
    }
    // A rotation changes the screen's bounds too.
    mwinIOSReadScreens(platform, mwinIOSNow());
    PostMonitor(platform, slot);
    PostSize(platform, slot);
    PostMode(platform, slot);
    PostMove(platform, slot);
    PostSafeArea(platform, slot);
}

int32_t mwinIOSSlotOfScene(const mwinIOSPlatform* platform, UIScene* scene)
{
    for (uint32_t i = 0; scene != nil && i < platform->context->limits.windows; i++)
    {
        if (platform->windows[i].scene == scene)
        {
            return (int32_t)i;
        }
    }
    return -1;
}

static NSString* StringOf(const char* bytes, size_t length)
{
    return [[[NSString alloc] initWithBytes:bytes length:length
                                   encoding:NSUTF8StringEncoding] autorelease];
}

static void Show(mwinIOSPlatform* platform, uint32_t slot, bool visible)
{
    UIWindow* window = platform->windows[slot].window;
    if (visible)
    {
        [window makeKeyAndVisible];
        // Text and a hardware keyboard's presses come to the view.
        [platform->windows[slot].view becomeFirstResponder];
    }
    else
    {
        window.hidden = YES;
    }
    PostType(platform, slot, visible ? mwin_eventShown : mwin_eventHidden);
}

// Completes a window's creation request, where it is still active.
static void Created(mwinContext* context, uint32_t slot, mwinOutcome outcome)
{
    int32_t request = mwinFindActiveRequest(&context->windows[slot],
                                            context->limits.requestsPerWindow, mwin_requestCreate);
    if (request >= 0)
    {
        mwinComplete(context, slot, (uint32_t)request, outcome);
    }
}

// Makes a window on a scene, which it holds.
static void Attach(mwinIOSPlatform* platform, uint32_t slot, UIWindowScene* scene)
{
    mwinContext* context = platform->context;
    const mwinWindow* core = &context->windows[slot];
    mwinIOSWindow* window = &platform->windows[slot];
    window->asking = false;
    @autoreleasepool
    {
        UIWindow* made = [[UIWindow alloc] initWithWindowScene:scene];
        // Opaque from the start: the system's hit test passes over a
        // window that shows nothing, as one does before its view's layer
        // first presents, and its touches would go to what is behind it.
        made.backgroundColor = UIColor.blackColor;
        UIView* view = mwinIOSCreateView(platform, slot, made.bounds);
        window->scene = scene;
        window->window = made;
        window->view = view;
        window->layer = (CAMetalLayer*)view.layer;
        window->controller = mwinIOSCreateController(platform, slot, view);
        made.rootViewController = window->controller;
        scene.title = StringOf(core->title, core->titleLength);
        view.contentScaleFactor = scene.screen.scale;
        PostType(platform, slot, mwin_eventWindowCreated);
        mwinIOSLayout(platform, slot);
        if (core->def.visible)
        {
            Show(platform, slot, true);
        }
        // A scene already in the background shows nothing of the window.
        mwinIOSSceneSeen(platform, scene,
                         scene.activationState != UISceneActivationStateBackground);
    }
    Created(context, slot, mwin_outcomeDone);
}

// UIKit refused the scene a window asked for.
static void Refused(uint32_t slot, uint32_t ask)
{
    mwinContext* context = mwinIOSCurrentContext();
    if (context == nullptr || slot >= context->limits.windows)
    {
        return;
    }
    mwinIOSPlatform* platform = mwinIOSPlatformOf(context);
    mwinIOSWindow* window = &platform->windows[slot];
    if (window->asking && window->ask == ask)
    {
        window->asking = false;
        Created(context, slot, mwin_outcomeFailed);
    }
}

// Asks UIKit for a new scene for a window.
static void Ask(mwinIOSPlatform* platform, uint32_t slot)
{
    mwinIOSWindow* window = &platform->windows[slot];
    window->asking = true;
    window->ask = ++platform->asks;
    window->askDeadlineNs = mwinIOSNow() + SCENE_DEADLINE_NS;
    uint32_t ask = window->ask;
    // The handler may come on another queue.
    [UIApplication.sharedApplication requestSceneSessionActivation:nil
                                                      userActivity:nil
                                                           options:nil
                                                      errorHandler:^(NSError* error) {
                                                        (void)error;
                                                        dispatch_async(dispatch_get_main_queue(), ^{
                                                          Refused(slot, ask);
                                                        });
                                                      }];
}

void mwinIOSCreateWindow(mwinContext* context, uint32_t slot)
{
    mwinIOSPlatform* platform = mwinIOSPlatformOf(context);
    mwinIOSWindow* window = &platform->windows[slot];
    *window = (mwinIOSWindow){.monitor = -1, .position = {NAN, NAN}, .mode = (mwinWindowMode)0xFF};
    UIWindowScene* scene = platform->waitingScene;
    if (scene != nil)
    {
        platform->waitingScene = nil;
        Attach(platform, slot, scene);
    }
    else if (UIApplication.sharedApplication.supportsMultipleScenes)
    {
        @autoreleasepool
        {
            Ask(platform, slot);
        }
    }
    else
    {
        Created(context, slot, mwin_outcomeUnsupported);
    }
}

void mwinIOSSceneCame(mwinIOSPlatform* platform, UIWindowScene* scene)
{
    // The window that asked first; scenes come in the order asked.
    int32_t slot = -1;
    for (uint32_t i = 0; i < platform->context->limits.windows; i++)
    {
        const mwinIOSWindow* window = &platform->windows[i];
        if (window->asking && (slot < 0 || window->ask < platform->windows[slot].ask))
        {
            slot = (int32_t)i;
        }
    }
    if (slot >= 0)
    {
        Attach(platform, (uint32_t)slot, [scene retain]);
    }
    else if (platform->waitingScene == nil)
    {
        platform->waitingScene = [scene retain];
    }
    else
    {
        // One no window asked for, a scene waiting already: let go.
        [UIApplication.sharedApplication requestSceneSessionDestruction:scene.session
                                                                options:nil
                                                           errorHandler:nil];
    }
}

void mwinIOSSceneWent(mwinIOSPlatform* platform, UIScene* scene)
{
    if (platform->waitingScene == scene)
    {
        [platform->waitingScene release];
        platform->waitingScene = nil;
    }
    // The window can no longer show: it is asked to close.
    int32_t slot = mwinIOSSlotOfScene(platform, scene);
    if (slot >= 0 && !platform->windows[slot].gone)
    {
        mwinIOSWindow* window = &platform->windows[slot];
        window->gone = true;
        window->window.hidden = YES;
        mwinEvent event = {.type = mwin_eventCloseRequested, .timeNs = mwinIOSNow()};
        mwinPost(platform->context, (uint32_t)slot, &event);
    }
}

void mwinIOSSceneSeen(mwinIOSPlatform* platform, UIScene* scene, bool seen)
{
    int32_t slot = mwinIOSSlotOfScene(platform, scene);
    if (slot < 0 || platform->windows[slot].gone || platform->windows[slot].occluded != seen)
    {
        return;
    }
    platform->windows[slot].occluded = !seen;
    PostType(platform, (uint32_t)slot, seen ? mwin_eventRevealed : mwin_eventOccluded);
}

void mwinIOSPumpScenes(mwinIOSPlatform* platform)
{
    uint64_t now = mwinIOSNow();
    for (uint32_t i = 0; i < platform->context->limits.windows; i++)
    {
        mwinIOSWindow* window = &platform->windows[i];
        if (window->asking && now >= window->askDeadlineNs)
        {
            window->asking = false;
            Created(platform->context, i, mwin_outcomeFailed);
        }
    }
}

// Whether a window other than the one in a slot shows a scene.
static bool OthersShow(const mwinIOSPlatform* platform, uint32_t slot)
{
    for (uint32_t i = 0; i < platform->context->limits.windows; i++)
    {
        if (i != slot && platform->windows[i].window != nil && !platform->windows[i].gone)
        {
            return true;
        }
    }
    return false;
}

void mwinIOSDestroyWindow(mwinContext* context, uint32_t slot)
{
    mwinIOSPlatform* platform = mwinIOSPlatformOf(context);
    mwinIOSWindow* window = &platform->windows[slot];
    @autoreleasepool
    {
        mwinIOSCloseDialogs(platform, slot);
        // A window whose creation failed or still waits has no view.
        if (window->view != nil)
        {
            mwinIOSForgetView(window->view);
        }
        window->window.hidden = YES;
        window->window.rootViewController = nil;
        [window->window release];
        [window->controller release];
        [window->view release];
        // The last window's scene stays connected, and the next window
        // takes it; another's is let go, unless the system let it go
        // already.
        UIWindowScene* scene = window->scene;
        if (scene != nil && !window->gone && platform->waitingScene == nil &&
            !OthersShow(platform, slot))
        {
            platform->waitingScene = scene;
        }
        else if (scene != nil)
        {
            // The backend stopping leaves the scenes as they are.
            if (!window->gone && !platform->ending)
            {
                [UIApplication.sharedApplication requestSceneSessionDestruction:scene.session
                                                                        options:nil
                                                                   errorHandler:nil];
            }
            [scene release];
        }
    }
    *window = (mwinIOSWindow){.monitor = -1};
}

// Carries out a request now: its outcome, or -1 when UIKit answers
// later.
static int CarryOut(mwinContext* context, uint32_t slot, const mwinRequest* request)
{
    mwinIOSPlatform* platform = mwinIOSPlatformOf(context);
    mwinIOSWindow* window = &platform->windows[slot];
    mwinWindow* core = &context->windows[slot];
    // A window not made, or whose scene the system let go, shows nothing.
    if (window->window == nil || window->gone)
    {
        return mwin_outcomeFailed;
    }
    switch (request->kind)
    {
    case mwin_requestTitle:
        window->scene.title = StringOf(core->pendingTitle, core->pendingTitleLength);
        memmove(core->title, core->pendingTitle, core->pendingTitleLength);
        core->titleLength = core->pendingTitleLength;
        return mwin_outcomeDone;
    case mwin_requestVisible:
        Show(platform, slot, request->value.visible);
        return mwin_outcomeDone;
    case mwin_requestFocus:
        [window->window makeKeyWindow];
        [window->view becomeFirstResponder];
        return mwin_outcomeDone;
    case mwin_requestTextInput:
        return mwinIOSSetTextInput(platform, slot, request->value.textInput.enabled,
                                   request->value.textInput.caret);
    case mwin_requestVirtualKeyboard:
        // The purpose in the low bits, the high bit set to show.
        return mwinIOSSetVirtualKeyboard(platform, slot, (request->value.code & 0x80u) != 0,
                                         (mwinInputPurpose)(request->value.code & 0x7Fu));
    case mwin_requestClipboardWrite:
    case mwin_requestClipboardWriteData:
        return mwinIOSWriteClipboard(platform);
    case mwin_requestClipboardRead:
        return mwinIOSReadClipboard(platform);
    case mwin_requestClipboardReadData:
        return mwinIOSReadClipboardData(platform, request);
    case mwin_requestOpenUrl:
        return mwinIOSOpenUrl(platform, slot, (uint32_t)(request - core->requests));
    case mwin_requestKeepAwake:
        // The pump keeps the display awake from the windows' state.
        return mwin_outcomeDone;
    case mwin_requestAccessibilityRoot:
        return mwinIOSSetAccessibilityRoot(platform, slot, (id)request->value.root);
    case mwin_requestFileDialog:
        return mwinIOSAskDialog(platform, slot, (uint32_t)(request - core->requests));
    default:
        return mwin_outcomeUnsupported;
    }
}

void mwinIOSSubmit(mwinContext* context, uint32_t slot, uint32_t request)
{
    @autoreleasepool
    {
        int outcome = CarryOut(context, slot, &context->windows[slot].requests[request]);
        if (outcome >= 0)
        {
            mwinComplete(context, slot, request, (mwinOutcome)outcome);
        }
    }
}
