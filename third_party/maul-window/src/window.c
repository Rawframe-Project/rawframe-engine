// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Windows and their requests. A request takes a free slot of its
// window, supersedes an active one of the same kind, and goes to the
// backend; mwinComplete answers it with a completion record, and the
// slot is free again once the program drains that record.

#include "maul-window/window.h"

#include "core.h"

#include "maul-unicode/encoding.h"

#include <math.h>
#include <string.h>

#define WINDOW_DEF_COOKIE 0x6D77696Eu
#define ALL_STYLES                                                                                 \
    (mwin_styleResizable | mwin_styleDecorated | mwin_styleAlwaysOnTop | mwin_styleCustomChrome)

mwinWindowDef mwinDefaultWindowDef(void)
{
    mwinWindowDef def = {0};
    def.cookie = WINDOW_DEF_COOKIE;
    def.size = (mwinSize){1280.0f, 720.0f};
    def.mode = mwin_modeWindowed;
    def.visible = true;
    def.style = mwin_styleResizable | mwin_styleDecorated;
    return def;
}

mwinWindowId mwinWindowIdOf(const mwinContext* context, uint32_t slot)
{
    return (mwinWindowId){slot + 1, context->windows[slot].generation};
}

mwinWindow* mwinFindWindow(const mwinContext* context, mwinWindowId window)
{
    if (window.index1 == 0 || window.index1 > context->limits.windows)
    {
        return nullptr;
    }
    mwinWindow* found = &context->windows[window.index1 - 1];
    return found->status == mwin_slotLive && found->generation == window.generation ? found
                                                                                    : nullptr;
}

static bool IsText(const char* text, size_t length, uint16_t limit)
{
    return (text != nullptr || length == 0) && length <= limit &&
           muniValidateUtf8(text, length).status == muni_success;
}

static bool IsPositive(mwinSize size)
{
    return isfinite(size.width) && isfinite(size.height) && size.width > 0.0f && size.height > 0.0f;
}

mwinRequestId mwinRequestIdOf(const mwinContext* context, uint32_t slot, uint32_t request)
{
    const mwinWindow* window = &context->windows[slot];
    return (mwinRequestId){slot * context->limits.requestsPerWindow + request + 1,
                           window->requests[request].generation};
}

// The state a request the platform carried out leaves when no
// notification reports it.
static void Carried(mwinWindow* window, const mwinRequest* request)
{
    switch (request->kind)
    {
    case mwin_requestCreate:
        window->state.style = window->def.style;
        window->state.opacity = 1.0f;
        break;
    case mwin_requestTextInput:
        window->state.textInput = request->value.textInput.enabled;
        break;
    case mwin_requestStyle:
        window->state.style = request->value.code;
        break;
    case mwin_requestOpacity:
        window->state.opacity = request->value.opacity;
        break;
    case mwin_requestAccessibilityRoot:
        window->accessibilityRoot = request->value.root;
        break;
    case mwin_requestKeepAwake:
        window->state.awake = request->value.awake;
        break;
    default:
        break;
    }
}

void mwinReleaseRequestData(const mwinContext* context, mwinRequest* request)
{
    mwinReleaseRequestText(context, request);
    mwinReleaseDialogCopy(context, request);
    mwinReleaseIconCopy(context, request);
}

// The payload kind a read of a kind finds, or mwin_foundKinds for a
// request that is no read.
static uint32_t FoundKindOf(mwinRequestKind kind)
{
    switch (kind)
    {
    case mwin_requestClipboardRead:
        return mwin_foundText;
    case mwin_requestClipboardReadData:
        return mwin_foundData;
    case mwin_requestPrimaryRead:
        return mwin_foundPrimary;
    default:
        return mwin_foundKinds;
    }
}

void mwinComplete(mwinContext* context, uint32_t slot, uint32_t request, mwinOutcome outcome)
{
    mwinRequest* entry = &context->windows[slot].requests[request];
    if (entry->status != mwin_requestActive)
    {
        return;
    }
    entry->status = mwin_requestAnswered;
    uint32_t found = FoundKindOf(entry->kind);
    if (outcome == mwin_outcomeDone && found < mwin_foundKinds)
    {
        // The read is answered by the payload its bytes just made.
        context->windows[slot].foundReads[found] =
            (mwinFoundRead){mwinRequestIdOf(context, slot, request), context->foundPayloads[found]};
    }
    if (outcome == mwin_outcomeDone)
    {
        Carried(&context->windows[slot], entry);
    }
    mwinReleaseRequestData(context, entry);
    mwinEvent event = {0};
    event.type = mwin_eventRequestCompleted;
    event.timeNs = context->backend->now(context);
    event.data.completion.request = mwinRequestIdOf(context, slot, request);
    event.data.completion.kind = entry->kind;
    event.data.completion.outcome = outcome;
    mwinPost(context, slot, &event);
}

void mwinReleaseRequest(mwinContext* context, mwinRequestId request)
{
    uint32_t perWindow = context->limits.requestsPerWindow;
    uint32_t slot = (request.index1 - 1) / perWindow;
    mwinRequest* entry = &context->windows[slot].requests[(request.index1 - 1) % perWindow];
    if (entry->status == mwin_requestAnswered && entry->generation == request.generation)
    {
        entry->status = mwin_requestFree;
        entry->generation += 1;
    }
}

int32_t mwinFindActiveRequest(const mwinWindow* window, uint16_t count, mwinRequestKind kind)
{
    for (uint16_t i = 0; i < count; i++)
    {
        if (window->requests[i].status == mwin_requestActive && window->requests[i].kind == kind)
        {
            return i;
        }
    }
    return -1;
}

// Takes a request slot of the window for a kind, superseding an active
// request of that kind; -1 when every slot is taken.
static int32_t TakeRequest(mwinContext* context, uint32_t slot, mwinRequestKind kind)
{
    mwinWindow* window = &context->windows[slot];
    uint16_t count = context->limits.requestsPerWindow;
    int32_t free = -1;
    for (uint16_t i = 0; i < count && free < 0; i++)
    {
        free = window->requests[i].status == mwin_requestFree ? i : -1;
    }
    int32_t older = mwinFindActiveRequest(window, count, kind);
    if (free < 0)
    {
        return -1;
    }
    if (older >= 0)
    {
        mwinComplete(context, slot, (uint32_t)older, mwin_outcomeSuperseded);
    }
    window->requests[free].status = mwin_requestActive;
    window->requests[free].kind = kind;
    return free;
}

void mwinSubmitRequest(mwinContext* context, uint32_t slot, int32_t request,
                       mwinRequestId* requestOut)
{
    if (requestOut != nullptr)
    {
        *requestOut = mwinRequestIdOf(context, slot, (uint32_t)request);
    }
    mwinRequestKind kind = context->windows[slot].requests[request].kind;
    if (context->windows[slot].def.kind != mwin_windowNormal &&
        (kind == mwin_requestMode || kind == mwin_requestStyle))
    {
        // A popup is windowed and undecorated on every platform.
        mwinComplete(context, slot, (uint32_t)request, mwin_outcomeUnsupported);
        return;
    }
    context->backend->submit(context, slot, (uint32_t)request);
}

static bool IsDefValid(const mwinContext* context, const mwinWindowDef* def)
{
    bool popup = def->kind != mwin_windowNormal;
    return def->cookie == WINDOW_DEF_COOKIE && IsPositive(def->size) &&
           def->mode <= mwin_modeMaximized && def->style <= ALL_STYLES &&
           IsText(def->title, def->titleLength, context->limits.titleBytes) &&
           IsText(def->canvas, def->canvasLength, MWIN_CANVAS_SELECTOR_BYTES) &&
           def->kind <= mwin_windowTooltip &&
           (!popup || (def->owner.index1 != 0 && def->mode == mwin_modeWindowed &&
                       isfinite(def->position.x) && isfinite(def->position.y)));
}

mwinResult mwinCreateWindow(mwinContext* context, const mwinWindowDef* def, mwinWindowId* windowOut,
                            mwinRequestId* requestOut)
{
    if (context == nullptr || def == nullptr || windowOut == nullptr || !IsDefValid(context, def))
    {
        return mwinMisuse(context);
    }
    if (def->owner.index1 != 0 && mwinFindWindow(context, def->owner) == nullptr)
    {
        return mwin_errorStale;
    }
    uint32_t slot = 0;
    while (slot < context->limits.windows && context->windows[slot].status != mwin_slotFree)
    {
        slot += 1;
    }
    if (slot == context->limits.windows)
    {
        return mwin_errorCapacity;
    }
    mwinWindow* window = &context->windows[slot];
    window->status = mwin_slotLive;
    window->generation += 1;
    window->state = (mwinWindowState){0};
    window->regionCount = 0;
    window->accessibilityRoot = nullptr;
    window->accessibilityAsked = false;
    window->def = *def;
    window->def.title = nullptr;
    window->titleLength = (uint16_t)def->titleLength;
    if (def->titleLength > 0)
    {
        memcpy(window->title, def->title, def->titleLength);
    }
    int32_t request = TakeRequest(context, slot, mwin_requestCreate);
    *windowOut = mwinWindowIdOf(context, slot);
    if (requestOut != nullptr)
    {
        *requestOut = mwinRequestIdOf(context, slot, (uint32_t)request);
    }
    context->backend->createWindow(context, slot);
    // The program's selector is its own once the call returns.
    window->def.canvas = nullptr;
    window->def.canvasLength = 0;
    return mwin_success;
}

mwinResult mwinDestroyWindow(mwinContext* context, mwinWindowId window)
{
    if (context == nullptr)
    {
        return mwinMisuse(context);
    }
    mwinWindow* found = mwinFindWindow(context, window);
    if (found == nullptr)
    {
        return mwin_errorStale;
    }
    uint32_t slot = window.index1 - 1;
    // What it owns goes first; the owner's slot is live meanwhile, so no
    // new window can take it.
    for (uint32_t i = 0; i < context->limits.windows; i++)
    {
        const mwinWindow* owned = &context->windows[i];
        if (owned->status == mwin_slotLive && owned->def.owner.index1 == window.index1 &&
            owned->def.owner.generation == window.generation)
        {
            (void)mwinDestroyWindow(context, mwinWindowIdOf(context, i));
        }
    }
    for (uint32_t i = 0; i < context->limits.requestsPerWindow; i++)
    {
        mwinComplete(context, slot, i, mwin_outcomeCancelled);
    }
    context->backend->destroyWindow(context, slot);
    mwinPostDestroyed(context, slot, context->backend->now(context));
    found->status = mwin_slotDestroyed;
    return mwin_success;
}

mwinResult mwinGetWindowState(const mwinContext* context, mwinWindowId window,
                              mwinWindowState* stateOut)
{
    if (context == nullptr || stateOut == nullptr)
    {
        return mwinMisuse(context);
    }
    const mwinWindow* found = mwinFindWindow(context, window);
    if (found == nullptr)
    {
        return mwin_errorStale;
    }
    *stateOut = found->state;
    return mwin_success;
}

mwinResult mwinBeginRequest(mwinContext* context, mwinWindowId window, mwinRequestKind kind,
                            uint32_t* slotOut, int32_t* requestOut)
{
    if (context == nullptr)
    {
        return mwinMisuse(context);
    }
    if (mwinFindWindow(context, window) == nullptr)
    {
        return mwin_errorStale;
    }
    *slotOut = window.index1 - 1;
    *requestOut = TakeRequest(context, *slotOut, kind);
    return *requestOut < 0 ? mwin_errorCapacity : mwin_success;
}

mwinResult mwinRequestTitle(mwinContext* context, mwinWindowId window, const char* title,
                            size_t length, mwinRequestId* requestOut)
{
    if (context != nullptr && !IsText(title, length, context->limits.titleBytes))
    {
        return length > context->limits.titleBytes ? mwin_errorCapacity : mwinMisuse(context);
    }
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestTitle, &slot, &request);
    if (status != mwin_success)
    {
        return status;
    }
    mwinWindow* found = &context->windows[slot];
    if (length > 0)
    {
        memcpy(found->pendingTitle, title, length);
    }
    found->pendingTitleLength = (uint16_t)length;
    mwinSubmitRequest(context, slot, request, requestOut);
    return mwin_success;
}

mwinResult mwinRequestSize(mwinContext* context, mwinWindowId window, mwinSize size,
                           mwinRequestId* requestOut)
{
    if (!IsPositive(size))
    {
        return mwinMisuse(context);
    }
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestSize, &slot, &request);
    if (status == mwin_success)
    {
        context->windows[slot].requests[request].value.size = size;
        mwinSubmitRequest(context, slot, request, requestOut);
    }
    return status;
}

mwinResult mwinRequestPosition(mwinContext* context, mwinWindowId window, mwinPosition position,
                               mwinRequestId* requestOut)
{
    if (!isfinite(position.x) || !isfinite(position.y))
    {
        return mwinMisuse(context);
    }
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestPosition, &slot, &request);
    if (status == mwin_success)
    {
        context->windows[slot].requests[request].value.position = position;
        mwinSubmitRequest(context, slot, request, requestOut);
    }
    return status;
}

mwinResult mwinRequestMode(mwinContext* context, mwinWindowId window, mwinWindowMode mode,
                           mwinRequestId* requestOut)
{
    if (mode > mwin_modeMaximized)
    {
        return mwinMisuse(context);
    }
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestMode, &slot, &request);
    if (status == mwin_success)
    {
        context->windows[slot].requests[request].value.mode = mode;
        mwinSubmitRequest(context, slot, request, requestOut);
    }
    return status;
}

mwinResult mwinRequestVisible(mwinContext* context, mwinWindowId window, bool visible,
                              mwinRequestId* requestOut)
{
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestVisible, &slot, &request);
    if (status == mwin_success)
    {
        context->windows[slot].requests[request].value.visible = visible;
        mwinSubmitRequest(context, slot, request, requestOut);
    }
    return status;
}

mwinResult mwinRequestFocus(mwinContext* context, mwinWindowId window, mwinRequestId* requestOut)
{
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestFocus, &slot, &request);
    if (status == mwin_success)
    {
        mwinSubmitRequest(context, slot, request, requestOut);
    }
    return status;
}

mwinResult mwinRequestSizeLimits(mwinContext* context, mwinWindowId window, mwinSize minimum,
                                 mwinSize maximum, mwinRequestId* requestOut)
{
    bool finite = isfinite(minimum.width) && isfinite(minimum.height) && isfinite(maximum.width) &&
                  isfinite(maximum.height);
    if (!finite || minimum.width < 0.0f || minimum.height < 0.0f || maximum.width < 0.0f ||
        maximum.height < 0.0f || (maximum.width > 0.0f && maximum.width < minimum.width) ||
        (maximum.height > 0.0f && maximum.height < minimum.height))
    {
        return mwinMisuse(context);
    }
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestSizeLimits, &slot, &request);
    if (status == mwin_success)
    {
        context->windows[slot].requests[request].value.limits.minimum = minimum;
        context->windows[slot].requests[request].value.limits.maximum = maximum;
        mwinSubmitRequest(context, slot, request, requestOut);
    }
    return status;
}

mwinResult mwinRequestAspectRatio(mwinContext* context, mwinWindowId window, uint32_t width,
                                  uint32_t height, mwinRequestId* requestOut)
{
    if ((width == 0) != (height == 0))
    {
        return mwinMisuse(context);
    }
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestAspectRatio, &slot, &request);
    if (status == mwin_success)
    {
        context->windows[slot].requests[request].value.aspect.width = width;
        context->windows[slot].requests[request].value.aspect.height = height;
        mwinSubmitRequest(context, slot, request, requestOut);
    }
    return status;
}

mwinResult mwinRequestStyle(mwinContext* context, mwinWindowId window, mwinWindowStyle style,
                            mwinRequestId* requestOut)
{
    if (style > ALL_STYLES)
    {
        return mwinMisuse(context);
    }
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestStyle, &slot, &request);
    if (status == mwin_success)
    {
        context->windows[slot].requests[request].value.code = style;
        mwinSubmitRequest(context, slot, request, requestOut);
    }
    return status;
}

mwinResult mwinRequestOpacity(mwinContext* context, mwinWindowId window, float opacity,
                              mwinRequestId* requestOut)
{
    if (!(opacity >= 0.0f && opacity <= 1.0f))
    {
        return mwinMisuse(context);
    }
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestOpacity, &slot, &request);
    if (status == mwin_success)
    {
        context->windows[slot].requests[request].value.opacity = opacity;
        mwinSubmitRequest(context, slot, request, requestOut);
    }
    return status;
}
