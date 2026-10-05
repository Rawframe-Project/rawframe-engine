// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// iOS windows (ios.h): a window is a scene's UIWindow, whose root view
// controller shows the library's view. A new window takes the scene
// that waits for one (the one the system connected at launch, or one a
// destroyed window left); without one it fails. The system sizes and
// places scenes, so a window's size is its scene's, in points, and its
// place is on its screen; the mode is borderless full screen while the
// window covers its screen, windowed otherwise (an iPad's split view or
// Stage Manager). Requests to size, place, restyle or set the mode of a
// window are unsupported; the title is the scene's, which the app
// switcher shows.

#include "ios.h"

#include <math.h>
#include <string.h>

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

void mwinIOSCreateWindow(mwinContext* context, uint32_t slot)
{
    mwinIOSPlatform* platform = mwinIOSPlatformOf(context);
    const mwinWindow* core = &context->windows[slot];
    mwinIOSWindow* window = &platform->windows[slot];
    *window = (mwinIOSWindow){.monitor = -1, .position = {NAN, NAN}, .mode = (mwinWindowMode)0xFF};
    int32_t request =
        mwinFindActiveRequest(core, context->limits.requestsPerWindow, mwin_requestCreate);
    UIWindowScene* scene = platform->waitingScene;
    if (scene == nil)
    {
        mwinComplete(context, slot, (uint32_t)request, mwin_outcomeFailed);
        return;
    }
    platform->waitingScene = nil;
    @autoreleasepool
    {
        UIWindow* made = [[UIWindow alloc] initWithWindowScene:scene];
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
    }
    mwinComplete(context, slot, (uint32_t)request, mwin_outcomeDone);
}

void mwinIOSDestroyWindow(mwinContext* context, uint32_t slot)
{
    mwinIOSPlatform* platform = mwinIOSPlatformOf(context);
    mwinIOSWindow* window = &platform->windows[slot];
    @autoreleasepool
    {
        mwinIOSCloseDialogs(platform, slot);
        mwinIOSForgetView(window->view);
        window->window.hidden = YES;
        window->window.rootViewController = nil;
        [window->window release];
        [window->controller release];
        [window->view release];
        // The scene stays connected: the next window takes it.
        if (platform->waitingScene == nil)
        {
            platform->waitingScene = window->scene;
        }
        else
        {
            [window->scene release];
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
        return mwinIOSWriteClipboard(platform);
    case mwin_requestClipboardRead:
        return mwinIOSReadClipboard(platform);
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
