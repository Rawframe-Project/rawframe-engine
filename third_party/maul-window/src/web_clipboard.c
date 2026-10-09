// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web clipboard.

#include "web_clipboard.h"

#include "allocator.h"
#include "clipboard_data.h"
#include "web_js.h"

EM_JS_DEPS(mwin_web_clipboard, "$UTF8ToString");

// clang-format off
// Each starts the promise and says whether the page has the API. Its
// record carries the window's generation, and a rejection as an
// mwinOutcome: denied or failed.
EM_JS(bool, mwinPageClipboardWrite, (const mwinContext* context, uint32_t slot, uint32_t generation,
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

// The text read waits as UTF-8, a lone surrogate encoded as U+FFFD,
// with the bytes of data reads in the order their records come.
EM_JS(bool, mwinPageClipboardRead, (const mwinContext* context, uint32_t slot, uint32_t generation), {
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

// Data goes as a ClipboardItem of Blobs: the three types every browser
// takes as they are, any other as a web custom format, its type after
// "web ". Each item is staged, then the staged ones written together.
// Records of data carry x 1.
EM_JS(void, mwinPageClipboardStage, (const mwinContext* context, const char* mime,
                         uint32_t mimeLength, const uint8_t* bytes, uint32_t length), {
    const state = Module.mwinWeb.get(context);
    const type = UTF8ToString(mime, mimeLength);
    const lower = type.toLowerCase();
    const name = ['text/plain', 'text/html', 'image/png'].includes(lower) ? lower : 'web ' + type;
    state.clipboardStaged = state.clipboardStaged || {};
    state.clipboardStaged[name] = new Blob([HEAPU8.slice(bytes, bytes + length)], {type: name});
});

EM_JS(bool, mwinPageClipboardWriteStaged, (const mwinContext* context, uint32_t slot,
                               uint32_t generation), {
    const state = Module.mwinWeb.get(context);
    const staged = state.clipboardStaged || {};
    state.clipboardStaged = {};
    const clipboard = navigator.clipboard;
    if (!clipboard || !clipboard.write || typeof ClipboardItem === 'undefined') {
        return false;
    }
    const refused = error => (error && error.name === 'NotAllowedError' ? 2 : 5);
    const answer = code => state.push(24, slot, code, 1, 0, 0, 0, 0, 0, generation);
    Promise.resolve().then(() => clipboard.write([new ClipboardItem(staged)]))
        .then(() => answer(0), error => answer(refused(error)));
    return true;
});

// The data of a type waits as bytes; a clipboard without it fails.
EM_JS(bool, mwinPageClipboardReadData, (const mwinContext* context, uint32_t slot,
                            uint32_t generation, const char* mime, uint32_t mimeLength), {
    const clipboard = navigator.clipboard;
    if (!clipboard || !clipboard.read) {
        return false;
    }
    const state = Module.mwinWeb.get(context);
    state.clipboardTexts = state.clipboardTexts || [];
    const type = UTF8ToString(mime, mimeLength);
    const lower = type.toLowerCase();
    const name = ['text/plain', 'text/html', 'image/png'].includes(lower) ? lower : 'web ' + type;
    const refused = error => (error && error.name === 'NotAllowedError' ? 2 : 5);
    const answer = code => state.push(25, slot, code, 1, 0, 0, 0, 0, 0, generation);
    clipboard.read().then(items => {
        const item = items.find(each => each.types.includes(name));
        if (!item) {
            answer(5);
            return;
        }
        return item.getType(name).then(blob => blob.arrayBuffer()).then(buffer => {
            state.clipboardTexts.push(new Uint8Array(buffer));
            answer(0);
        });
    }).catch(error => answer(refused(error)));
    return true;
});

EM_JS(uint32_t, mwinPageClipboardWaiting, (const mwinContext* context), {
    return Module.mwinWeb.get(context).clipboardTexts[0].length;
});

// Takes the waiting text, into out unless it is NULL.
EM_JS(void, mwinPageClipboardTake, (const mwinContext* context, char* out), {
    const bytes = Module.mwinWeb.get(context).clipboardTexts.shift();
    if (out) {
        HEAPU8.set(bytes, out);
    }
});
// clang-format on

int mwinWebWriteClipboard(mwinContext* context, uint32_t slot)
{
    return mwinPageClipboardWrite(context, slot, context->windows[slot].generation,
                                  context->clipboardOffer, context->clipboardOfferLength)
               ? -1
               : mwin_outcomeUnsupported;
}

int mwinWebReadClipboard(mwinContext* context, uint32_t slot)
{
    return mwinPageClipboardRead(context, slot, context->windows[slot].generation)
               ? -1
               : mwin_outcomeUnsupported;
}

int mwinWebWriteClipboardData(mwinContext* context, uint32_t slot)
{
    const mwinClipboardCopy* copy = context->clipboardData;
    if (mwinOffersClipboardText(context))
    {
        mwinPageClipboardStage(context, "text/plain", 10, (const uint8_t*)context->clipboardOffer,
                               context->clipboardOfferLength);
    }
    for (uint32_t i = 0; copy != nullptr && i < copy->count; i++)
    {
        const mwinClipboardDataItem* item = &copy->items[i];
        mwinPageClipboardStage(context, item->mime, item->mimeLength,
                               mwinClipboardBytesOf(copy, item), item->length);
    }
    return mwinPageClipboardWriteStaged(context, slot, context->windows[slot].generation)
               ? -1
               : mwin_outcomeUnsupported;
}

int mwinWebReadClipboardData(mwinContext* context, uint32_t slot, const mwinRequest* request)
{
    return mwinPageClipboardReadData(context, slot, context->windows[slot].generation,
                                     request->value.text.bytes, request->value.text.length)
               ? -1
               : mwin_outcomeUnsupported;
}

// Takes a done read's text or data into the context: the outcome.
static mwinOutcome Take(mwinContext* context, bool data)
{
    uint32_t length = mwinPageClipboardWaiting(context);
    char* bytes = length > 0 && length <= context->limits.clipboardBytes
                      ? mwinAllocate(&context->allocator, length, 1)
                      : nullptr;
    mwinPageClipboardTake(context, bytes);
    if (length > context->limits.clipboardBytes)
    {
        return mwin_outcomeTooLarge;
    }
    if (length > 0 && bytes == nullptr)
    {
        return mwin_outcomeFailed;
    }
    mwinOutcome outcome = data ? mwinTakeClipboardData(context, bytes, length)
                               : mwinTakeClipboardText(context, bytes, length);
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
    bool data = record->x != 0.0f;
    mwinOutcome outcome = (mwinOutcome)record->code;
    // A done read's bytes are taken even when its window went.
    if (read && outcome == mwin_outcomeDone)
    {
        outcome = Take(context, data);
    }
    uint32_t slot = (uint32_t)record->slot;
    const mwinWindow* window = &context->windows[slot];
    if (window->status != mwin_slotLive || window->generation != (uint32_t)record->extra)
    {
        return;
    }
    static const mwinRequestKind kinds[2][2] = {
        {mwin_requestClipboardWrite, mwin_requestClipboardWriteData},
        {mwin_requestClipboardRead, mwin_requestClipboardReadData},
    };
    int32_t request =
        mwinFindActiveRequest(window, context->limits.requestsPerWindow, kinds[read][data]);
    if (request >= 0)
    {
        mwinComplete(context, slot, (uint32_t)request, outcome);
    }
}
