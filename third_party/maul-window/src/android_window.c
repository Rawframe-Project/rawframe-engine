// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Android window (android.h): a program has one, the activity's,
// which the system sizes and shows with the activity. It is created at
// the activity's first native window, which its create request waits
// for; each later native window is a new surface generation, each one
// going a lost surface. Its size in pixels is the native window's, its
// scale the configuration's density over 160. Text input and the
// on-screen keyboard are android_text.c's, the clipboard, addresses and
// keeping awake android_services.c's, file dialogs android_dialog.c's. A second window, sizing,
// placing, restyling, hiding it or setting its mode are unsupported.

#include "android.h"

static void PostType(mwinAndroidPlatform* platform, mwinEventType type)
{
    mwinEvent event = {.type = type, .timeNs = mwinAndroidNow()};
    mwinPost(platform->context, (uint32_t)platform->slot, &event);
}

void mwinAndroidReadSize(mwinAndroidPlatform* platform)
{
    mwinAndroidWindow* window = &platform->window;
    if (platform->slot < 0 || !window->created || platform->nativeWindow == nullptr)
    {
        return;
    }
    int32_t width = ANativeWindow_getWidth(platform->nativeWindow);
    int32_t height = ANativeWindow_getHeight(platform->nativeWindow);
    float scale = mwinAndroidScale(platform);
    if (width <= 0 || height <= 0)
    {
        return;
    }
    mwinSize size = {(float)width / scale, (float)height / scale};
    mwinEvent event = {.timeNs = mwinAndroidNow()};
    uint32_t slot = (uint32_t)platform->slot;
    if (scale != window->scale)
    {
        window->scale = scale;
        event.type = mwin_eventScaleChanged;
        event.data.scale = (mwinScaleChange){scale, size};
        mwinPost(platform->context, slot, &event);
    }
    if ((uint32_t)width != window->width || (uint32_t)height != window->height)
    {
        window->width = (uint32_t)width;
        window->height = (uint32_t)height;
        event.type = mwin_eventResized;
        event.data.size = size;
        mwinPost(platform->context, slot, &event);
        event.type = mwin_eventPixelSizeChanged;
        event.data.pixelSize = (mwinPixelSize){(uint32_t)width, (uint32_t)height};
        mwinPost(platform->context, slot, &event);
        // The keyboard's part depends on the size.
        mwinAndroidPostInsets(platform);
    }
}

void mwinAndroidPostFocus(mwinAndroidPlatform* platform)
{
    mwinAndroidWindow* window = &platform->window;
    if (platform->slot < 0 || !window->created || window->focused == platform->focused)
    {
        return;
    }
    window->focused = platform->focused;
    PostType(platform, window->focused ? mwin_eventFocusGained : mwin_eventFocusLost);
}

void mwinAndroidSurfaceCame(mwinAndroidPlatform* platform)
{
    mwinAndroidWindow* window = &platform->window;
    if (platform->slot < 0)
    {
        return;
    }
    if (window->created)
    {
        if (window->lost)
        {
            window->lost = false;
            PostType(platform, mwin_eventSurfaceRestored);
            mwinAndroidReadSize(platform);
        }
        return;
    }
    // The first surface makes the window, shown with its activity.
    mwinContext* context = platform->context;
    uint32_t slot = (uint32_t)platform->slot;
    window->created = true;
    PostType(platform, mwin_eventWindowCreated);
    mwinAndroidReadSize(platform);
    mwinAndroidPostInsets(platform);
    PostType(platform, mwin_eventShown);
    mwinAndroidPostFocus(platform);
    int32_t request = mwinFindActiveRequest(&context->windows[slot],
                                            context->limits.requestsPerWindow, mwin_requestCreate);
    if (request >= 0)
    {
        mwinComplete(context, slot, (uint32_t)request, mwin_outcomeDone);
    }
}

void mwinAndroidSurfaceWent(mwinAndroidPlatform* platform)
{
    mwinAndroidWindow* window = &platform->window;
    if (platform->slot < 0 || !window->created || window->lost)
    {
        return;
    }
    window->lost = true;
    PostType(platform, mwin_eventSurfaceLost);
}

void mwinAndroidCreateWindow(mwinContext* context, uint32_t slot)
{
    mwinAndroidPlatform* platform = mwinAndroidPlatformOf(context);
    if (platform->slot >= 0)
    {
        int32_t request = mwinFindActiveRequest(
            &context->windows[slot], context->limits.requestsPerWindow, mwin_requestCreate);
        mwinComplete(context, slot, (uint32_t)request, mwin_outcomeUnsupported);
        return;
    }
    platform->slot = (int32_t)slot;
    platform->window = (mwinAndroidWindow){0};
    if (platform->nativeWindow != nullptr)
    {
        mwinAndroidSurfaceCame(platform);
    }
}

void mwinAndroidDestroyWindow(mwinContext* context, uint32_t slot)
{
    mwinAndroidPlatform* platform = mwinAndroidPlatformOf(context);
    if (platform->slot == (int32_t)slot)
    {
        platform->slot = -1;
        platform->window = (mwinAndroidWindow){0};
        mwinAndroidForgetAccessibility(platform);
    }
}

// A request carried out: its outcome, or -1 when it is answered later.
static int CarryOut(mwinAndroidPlatform* platform, uint32_t slot, uint32_t index)
{
    const mwinRequest* request = &platform->context->windows[slot].requests[index];
    switch (request->kind)
    {
    case mwin_requestVisible:
        // The window shows with its activity: asking it to show is done.
        return request->value.visible ? mwin_outcomeDone : mwin_outcomeUnsupported;
    case mwin_requestTextInput:
        return mwinAndroidSetTextInput(platform, request->value.textInput.enabled,
                                       request->value.textInput.caret);
    case mwin_requestFileDialog:
        return mwinAndroidAskDialog(platform, slot, index);
    case mwin_requestClipboardWrite:
        return mwinAndroidWriteClipboard(platform);
    case mwin_requestClipboardRead:
        return mwinAndroidReadClipboard(platform);
    case mwin_requestOpenUrl:
        return mwinAndroidOpenUrl(platform, request);
    case mwin_requestKeepAwake:
        mwinAndroidApplyAwake(platform, request->value.awake);
        return mwin_outcomeDone;
    case mwin_requestVirtualKeyboard:
        // The purpose in the low bits, the high bit set to show.
        return mwinAndroidSetKeyboard(platform, (request->value.code & 0x80u) != 0,
                                      (mwinInputPurpose)(request->value.code & 0x7Fu));
    case mwin_requestAccessibilityRoot:
        return mwinAndroidSetAccessibilityRoot(platform, request->value.root);
    default:
        return mwin_outcomeUnsupported;
    }
}

void mwinAndroidSubmit(mwinContext* context, uint32_t slot, uint32_t request)
{
    int outcome = CarryOut(mwinAndroidPlatformOf(context), slot, request);
    if (outcome >= 0)
    {
        mwinComplete(context, slot, request, (mwinOutcome)outcome);
    }
}
