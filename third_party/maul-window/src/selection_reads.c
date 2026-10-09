// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The reads of the clipboard and the primary selection.

#include "selection_reads.h"

#include "clipboard_data.h"

#include <string.h>

// The kinds of request a read answers, in the order waiting ones start.
static const mwinRequestKind s_readKinds[] = {
    mwin_requestClipboardRead,
    mwin_requestClipboardReadData,
    mwin_requestPrimaryRead,
};

bool mwinReadsPrimary(const mwinRequest* request)
{
    return request->kind == mwin_requestPrimaryRead;
}

void mwinBeginSelectionRead(mwinSelectionRead* read, const mwinRequest* request)
{
    read->kind = request->kind;
    read->mimeLength = 0;
    read->mime[0] = '\0';
    if (request->kind == mwin_requestClipboardReadData)
    {
        memcpy(read->mime, request->value.text.bytes, request->value.text.length + 1);
        read->mimeLength = request->value.text.length;
    }
}

mwinOutcome mwinAnswerOwnRead(mwinContext* context, const mwinRequest* request)
{
    switch (request->kind)
    {
    case mwin_requestClipboardReadData:
    {
        const mwinClipboardDataItem* item =
            mwinFindClipboardItem(context, request->value.text.bytes, request->value.text.length);
        return item != nullptr
                   ? mwinTakeClipboardData(
                         context, mwinClipboardBytesOf(context->clipboardData, item), item->length)
                   : mwin_outcomeFailed;
    }
    case mwin_requestPrimaryRead:
        return mwinTakePrimaryText(context, context->primaryOffer, context->primaryOfferLength);
    default:
        return mwinTakeClipboardText(context, context->clipboardOffer,
                                     context->clipboardOfferLength);
    }
}

mwinOutcome mwinTakeSelectionRead(mwinContext* context, const mwinSelectionRead* read,
                                  const void* bytes, size_t length)
{
    switch (read->kind)
    {
    case mwin_requestClipboardReadData:
        return mwinTakeClipboardData(context, bytes, length);
    case mwin_requestPrimaryRead:
        return mwinTakePrimaryText(context, bytes, length);
    default:
        return mwinTakeClipboardText(context, bytes, length);
    }
}

mwinOutcome mwinMissedSelectionRead(mwinContext* context, const mwinSelectionRead* read)
{
    return read->kind == mwin_requestClipboardReadData
               ? mwin_outcomeFailed
               : mwinTakeSelectionRead(context, read, nullptr, 0);
}

// Whether a read answers a request: one of its kind, and for data of
// its type.
static bool Answers(const mwinSelectionRead* read, const mwinRequest* request)
{
    return request->kind == read->kind &&
           (request->kind != mwin_requestClipboardReadData ||
            mwinSameMime(request->value.text.bytes, request->value.text.length, read->mime,
                         read->mimeLength));
}

void mwinFinishSelectionRead(mwinContext* context, const mwinSelectionRead* read,
                             mwinOutcome outcome)
{
    for (uint32_t slot = 0; slot < context->limits.windows; slot++)
    {
        const mwinWindow* window = &context->windows[slot];
        int32_t request =
            window->status == mwin_slotLive
                ? mwinFindActiveRequest(window, context->limits.requestsPerWindow, read->kind)
                : -1;
        if (request >= 0 && Answers(read, &window->requests[request]))
        {
            mwinComplete(context, slot, (uint32_t)request, outcome);
        }
    }
}

void mwinStartWaitingReads(mwinContext* context, mwinStartSelectionRead start, void* backend)
{
    size_t kinds = sizeof(s_readKinds) / sizeof(s_readKinds[0]);
    for (uint32_t slot = 0; slot < context->limits.windows; slot++)
    {
        const mwinWindow* window = &context->windows[slot];
        for (size_t i = 0; window->status == mwin_slotLive && i < kinds; i++)
        {
            int32_t request =
                mwinFindActiveRequest(window, context->limits.requestsPerWindow, s_readKinds[i]);
            int outcome = request >= 0 ? start(backend, &window->requests[request]) : -1;
            if (outcome >= 0)
            {
                mwinComplete(context, slot, (uint32_t)request, (mwinOutcome)outcome);
            }
        }
    }
}
