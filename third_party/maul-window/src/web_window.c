// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Web windows.

#include "web_window.h"

#include "web_clipboard.h"
#include "web_cursor.h"
#include "web_drop.h"
#include "web_input.h"
#include "web_js.h"
#include "web_page.h"
#include "web_services.h"
#include "web_text.h"

#include <string.h>

static mwinWebPlatform* PlatformOf(const mwinContext* context)
{
    return (mwinWebPlatform*)context->backendData;
}

static void Post(mwinWebPlatform* platform, uint32_t slot, mwinEvent* event, uint64_t timeNs)
{
    event->timeNs = timeNs;
    mwinPost(platform->context, slot, event);
}

static void PostType(mwinWebPlatform* platform, uint32_t slot, mwinEventType type, uint64_t timeNs)
{
    mwinEvent event = {.type = type};
    Post(platform, slot, &event, timeNs);
}

// Posts what changed of a canvas's box and scale.
static void Resize(mwinWebPlatform* platform, uint32_t slot, mwinSize size, mwinPixelSize pixels,
                   uint64_t timeNs)
{
    mwinWebWindow* window = &platform->windows[slot];
    mwinEvent event = {0};
    if (window->scale != platform->scale)
    {
        window->scale = platform->scale;
        event.type = mwin_eventScaleChanged;
        event.data.scale = (mwinScaleChange){platform->scale, size};
        Post(platform, slot, &event, timeNs);
    }
    if (window->size.width != size.width || window->size.height != size.height)
    {
        window->size = size;
        event.type = mwin_eventResized;
        event.data.size = size;
        Post(platform, slot, &event, timeNs);
    }
    if (window->pixels.width != pixels.width || window->pixels.height != pixels.height)
    {
        window->pixels = pixels;
        event.type = mwin_eventPixelSizeChanged;
        event.data.pixelSize = pixels;
        Post(platform, slot, &event, timeNs);
    }
}

static void PostMode(mwinWebPlatform* platform, uint32_t slot, mwinWindowMode mode, uint64_t timeNs)
{
    mwinEvent event = {.type = mwin_eventModeChanged};
    event.data.mode = mode;
    Post(platform, slot, &event, timeNs);
}

void mwinWebCreateWindow(mwinContext* context, uint32_t slot)
{
    mwinWebPlatform* platform = PlatformOf(context);
    const mwinWindow* core = &context->windows[slot];
    mwinWebWindow* window = &platform->windows[slot];
    *window = (mwinWebWindow){0};
    int32_t request =
        mwinFindActiveRequest(core, context->limits.requestsPerWindow, mwin_requestCreate);
    if (core->def.kind != mwin_windowNormal)
    {
        // A page has no windows over its canvases.
        mwinComplete(context, slot, (uint32_t)request, mwin_outcomeUnsupported);
        return;
    }
    float box[4];
    int length = mwinWebOpenCanvas(context, slot, core->def.canvas, core->def.canvasLength,
                                   core->def.size.width, core->def.size.height, core->def.visible,
                                   window->selector, MWIN_WEB_SELECTOR_BYTES, box);
    if (length < 0)
    {
        mwinComplete(context, slot, (uint32_t)request, mwin_outcomeFailed);
        return;
    }
    window->selectorLength = (uint32_t)length;
    static const char suffix[] = "-accessibility";
    memcpy(window->host, window->selector, (size_t)length);
    memcpy(window->host + length, suffix, sizeof(suffix) - 1);
    window->hostLength = (uint32_t)length + (uint32_t)sizeof(suffix) - 1;
    window->open = true;
    mwinWebWatchCanvas(context, slot);
    mwinWebWatchDrops(context, slot);
    uint64_t now = mwinWebNanoseconds(mwinWebNow());
    PostType(platform, slot, mwin_eventWindowCreated, now);
    if (platform->monitor >= 0)
    {
        mwinEvent display = {.type = mwin_eventDisplayChanged};
        display.data.monitor = mwinMonitorIdOf(context, (uint32_t)platform->monitor);
        Post(platform, slot, &display, now);
    }
    Resize(platform, slot, (mwinSize){box[0], box[1]},
           (mwinPixelSize){(uint32_t)box[2], (uint32_t)box[3]}, now);
    PostMode(platform, slot, mwin_modeWindowed, now);
    if (core->def.visible)
    {
        PostType(platform, slot, mwin_eventShown, now);
    }
    mwinComplete(context, slot, (uint32_t)request, mwin_outcomeDone);
    if (core->def.mode == mwin_modeBorderlessFullscreen)
    {
        // Without a user's gesture this is refused; the window stays.
        (void)mwinWebSetFullscreen(context, slot, true);
    }
}

void mwinWebDestroyWindow(mwinContext* context, uint32_t slot)
{
    mwinWebWindow* window = &PlatformOf(context)->windows[slot];
    if (window->open)
    {
        mwinWebCloseCanvas(context, slot);
    }
    *window = (mwinWebWindow){0};
}

// A mode request's outcome, or -1 when the page answers later.
static int SetMode(mwinContext* context, uint32_t slot, mwinWindowMode mode)
{
    if (mode == mwin_modeMaximized || mode == mwin_modeMinimized)
    {
        return mwin_outcomeUnsupported;
    }
    int answer = mwinWebSetFullscreen(context, slot, mode == mwin_modeBorderlessFullscreen);
    return answer > 0 ? mwin_outcomeDone : (answer < 0 ? mwin_outcomeUnsupported : -1);
}

// Carries out a request now: its outcome, or -1 when the page answers
// later.
static int CarryOut(mwinContext* context, uint32_t slot, const mwinRequest* request)
{
    mwinWindow* core = &context->windows[slot];
    switch (request->kind)
    {
    case mwin_requestTitle:
        mwinWebSetTitle(context, slot, core->pendingTitle, core->pendingTitleLength);
        memmove(core->title, core->pendingTitle, core->pendingTitleLength);
        core->titleLength = core->pendingTitleLength;
        return mwin_outcomeDone;
    case mwin_requestSize:
        mwinWebSetSize(context, slot, request->value.size.width, request->value.size.height);
        return mwin_outcomeDone;
    case mwin_requestMode:
        return SetMode(context, slot, request->value.mode);
    case mwin_requestVisible:
        mwinWebSetVisible(context, slot, request->value.visible);
        PostType(PlatformOf(context), slot,
                 request->value.visible ? mwin_eventShown : mwin_eventHidden,
                 mwinWebNanoseconds(mwinWebNow()));
        return mwin_outcomeDone;
    case mwin_requestFocus:
        return mwinWebFocus(context, slot) ? mwin_outcomeDone : mwin_outcomeDenied;
    case mwin_requestOpacity:
        mwinWebSetOpacity(context, slot, request->value.opacity);
        return mwin_outcomeDone;
    case mwin_requestCursorMode:
        return mwinWebSetCursorMode(PlatformOf(context), slot, request->value.code);
    case mwin_requestCursorShape:
        return mwinWebSetCursorShape(PlatformOf(context), slot, request->value.code);
    case mwin_requestCursorImage:
        return mwinWebSetCursorImage(PlatformOf(context), slot, request->value.cursor);
    case mwin_requestTextInput:
        return mwinWebSetTextInput(PlatformOf(context), slot, request->value.textInput.enabled,
                                   request->value.textInput.caret);
    case mwin_requestClipboardWrite:
        return mwinWebWriteClipboard(context, slot);
    case mwin_requestClipboardRead:
        return mwinWebReadClipboard(context, slot);
    case mwin_requestClipboardWriteData:
        return mwinWebWriteClipboardData(context, slot);
    case mwin_requestClipboardReadData:
        return mwinWebReadClipboardData(context, slot, request);
    case mwin_requestOpenUrl:
        return mwinWebOpenUrl(request);
    case mwin_requestKeepAwake:
        // The pump keeps the screen awake from the windows' state.
        return mwinWebCanKeepAwake();
    case mwin_requestVirtualKeyboard:
        // The purpose in the low bits, the high bit set to show.
        return mwinWebSetVirtualKeyboard(PlatformOf(context), slot,
                                         (request->value.code & 0x80u) != 0,
                                         (mwinInputPurpose)(request->value.code & 0x7Fu));
    default:
        return mwin_outcomeUnsupported;
    }
}

void mwinWebSubmit(mwinContext* context, uint32_t slot, uint32_t request)
{
    int outcome = CarryOut(context, slot, &context->windows[slot].requests[request]);
    if (outcome >= 0)
    {
        mwinComplete(context, slot, request, (mwinOutcome)outcome);
    }
}

// The page's answer to a mode request, if one waits.
static void AnswerMode(mwinWebPlatform* platform, uint32_t slot, mwinOutcome outcome)
{
    mwinContext* context = platform->context;
    const mwinWindow* core = &context->windows[slot];
    int32_t request =
        mwinFindActiveRequest(core, context->limits.requestsPerWindow, mwin_requestMode);
    if (request >= 0)
    {
        mwinComplete(context, slot, (uint32_t)request, outcome);
    }
}

void mwinWebHandleWindowRecord(mwinWebPlatform* platform, const mwinWebRecord* record)
{
    uint32_t slot = (uint32_t)record->slot;
    uint64_t timeNs = mwinWebNanoseconds(record->timeMs);
    switch (record->kind)
    {
    case mwin_webResized:
        Resize(platform, slot, (mwinSize){record->x, record->y},
               (mwinPixelSize){(uint32_t)record->z, (uint32_t)record->w}, timeNs);
        break;
    case mwin_webFocus:
        PostType(platform, slot, record->code != 0 ? mwin_eventFocusGained : mwin_eventFocusLost,
                 timeNs);
        break;
    case mwin_webFullscreen:
        PostMode(platform, slot,
                 record->code != 0 ? mwin_modeBorderlessFullscreen : mwin_modeWindowed, timeNs);
        AnswerMode(platform, slot, mwin_outcomeDone);
        break;
    case mwin_webFullscreenFailed:
        AnswerMode(platform, slot, mwin_outcomeDenied);
        break;
    case mwin_webSurfaceLost:
    case mwin_webSurfaceRestored:
        PostType(platform, slot,
                 record->kind == mwin_webSurfaceLost ? mwin_eventSurfaceLost
                                                     : mwin_eventSurfaceRestored,
                 timeNs);
        break;
    default:
        break;
    }
}
