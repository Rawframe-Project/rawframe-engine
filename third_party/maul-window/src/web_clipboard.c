// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web clipboard.

#include "web_clipboard.h"

#include "allocator.h"

#include <emscripten/em_js.h>

EM_JS_DEPS(mwin_web_clipboard, "$UTF8ToString");

// clang-format off
// Each starts the promise and says whether the page has the API. Its
// record carries the window's generation, and a rejection as an
// mwinOutcome: denied or failed.
EM_JS(bool, StartWrite, (const mwinContext* context, uint32_t slot, uint32_t generation,
                         const char* text, uint32_t length), {
    const clipboard = navigator.clipboard;
    if (!clipboard || !clipboard.writeText) {
        return false;
    }
    const state = Module.mwinWeb.get(context);
    const refused = error => (error && error.name === 'NotAllowedError' ? 2 : 5);
    const answer = code => state.push(24, slot, code, 0, 0, 0, 0, 0, 0, generation);
    clipboard.writeText(UTF8ToString(text, length, true))
        .then(() => answer(0), error => answer(refused(error)));
    return true;
});

// The text read waits as UTF-8, a lone surrogate encoded as U+FFFD.
EM_JS(bool, StartRead, (const mwinContext* context, uint32_t slot, uint32_t generation), {
    const clipboard = navigator.clipboard;
    if (!clipboard || !clipboard.readText) {
        return false;
    }
    const state = Module.mwinWeb.get(context);
    state.clipboardTexts = state.clipboardTexts || [];
    const refused = error => (error && error.name === 'NotAllowedError' ? 2 : 5);
    const answer = code => state.push(25, slot, code, 0, 0, 0, 0, 0, 0, generation);
    clipboard.readText().then(text => {
        state.clipboardTexts.push(new TextEncoder().encode(text));
        answer(0);
    }, error => answer(refused(error)));
    return true;
});

EM_JS(uint32_t, WaitingLength, (const mwinContext* context), {
    return Module.mwinWeb.get(context).clipboardTexts[0].length;
});

// Takes the waiting text, into out unless it is NULL.
EM_JS(void, TakeWaiting, (const mwinContext* context, char* out), {
    const bytes = Module.mwinWeb.get(context).clipboardTexts.shift();
    if (out) {
        HEAPU8.set(bytes, out);
    }
});
// clang-format on

int mwinWebWriteClipboard(mwinContext* context, uint32_t slot)
{
    return StartWrite(context, slot, context->windows[slot].generation, context->clipboardOffer,
                      context->clipboardOfferLength)
               ? -1
               : mwin_outcomeUnsupported;
}

int mwinWebReadClipboard(mwinContext* context, uint32_t slot)
{
    return StartRead(context, slot, context->windows[slot].generation) ? -1
                                                                       : mwin_outcomeUnsupported;
}

// Takes a done read's text into the context: the outcome.
static mwinOutcome Take(mwinContext* context)
{
    uint32_t length = WaitingLength(context);
    char* bytes = length > 0 && length <= context->limits.clipboardBytes
                      ? mwinAllocate(&context->allocator, length, 1)
                      : nullptr;
    TakeWaiting(context, bytes);
    if (length > context->limits.clipboardBytes)
    {
        return mwin_outcomeTooLarge;
    }
    if (length > 0 && bytes == nullptr)
    {
        return mwin_outcomeFailed;
    }
    mwinOutcome outcome = mwinTakeClipboardText(context, bytes, length);
    if (bytes != nullptr)
    {
        mwinRelease(&context->allocator, bytes, length, 1);
    }
    return outcome;
}

void mwinWebHandleClipboardRecord(mwinWebPlatform* platform, const mwinWebRecord* record)
{
    mwinContext* context = platform->context;
    bool read = record->kind == mwin_webClipboardRead;
    mwinOutcome outcome = (mwinOutcome)record->code;
    // A done read's text is taken even when its window went.
    if (read && outcome == mwin_outcomeDone)
    {
        outcome = Take(context);
    }
    uint32_t slot = (uint32_t)record->slot;
    const mwinWindow* window = &context->windows[slot];
    if (window->status != mwin_slotLive || window->generation != (uint32_t)record->extra)
    {
        return;
    }
    int32_t request =
        mwinFindActiveRequest(window, context->limits.requestsPerWindow,
                              read ? mwin_requestClipboardRead : mwin_requestClipboardWrite);
    if (request >= 0)
    {
        mwinComplete(context, slot, (uint32_t)request, outcome);
    }
}
