// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The clipboard's requests and the text they carry.

#include "maul-window/clipboard.h"

#include "allocator.h"
#include "clipboard_data.h"
#include "core.h"
#include "utf8.h"

#include "maul-unicode/encoding.h"

#include <string.h>

// Text in a block of its own length; none for empty text.
static char* Allocate(const mwinContext* context, size_t length)
{
    return length > 0 ? mwinAllocate(&context->allocator, length, 1) : nullptr;
}

static void Release(const mwinContext* context, char* text, size_t length)
{
    if (text != nullptr)
    {
        mwinRelease(&context->allocator, text, length, 1);
    }
}

// Makes room for a read's text of a length, the last one's given back;
// NULL, with the last one kept, when the allocator has none.
static char* Found(mwinContext* context, size_t length, bool* failedOut)
{
    char* text = Allocate(context, length);
    *failedOut = length > 0 && text == nullptr;
    if (!*failedOut)
    {
        Release(context, context->clipboardFound, context->clipboardFoundLength);
        context->clipboardFound = text;
        context->clipboardFoundLength = (uint32_t)length;
        context->foundPayloads[mwin_foundText] += 1;
    }
    return text;
}

mwinOutcome mwinTakeClipboardText(mwinContext* context, const char* bytes, size_t length)
{
    // Repairing never shortens the text.
    if (length > context->limits.clipboardBytes)
    {
        return mwin_outcomeTooLarge;
    }
    size_t needed = mwinRepairUtf8(bytes, length, nullptr);
    if (needed > context->limits.clipboardBytes)
    {
        return mwin_outcomeTooLarge;
    }
    bool failed = false;
    char* text = Found(context, needed, &failed);
    if (failed)
    {
        return mwin_outcomeFailed;
    }
    (void)mwinRepairUtf8(bytes, length, text);
    return mwin_outcomeDone;
}

mwinOutcome mwinTakeClipboardUtf16(mwinContext* context, const uint16_t* units, size_t length)
{
    // Each unit is at least a byte of UTF-8.
    if (length > context->limits.clipboardBytes)
    {
        return mwin_outcomeTooLarge;
    }
    size_t needed = 0;
    (void)muniConvertUtf16ToUtf8(units, length, muni_convertReplace, nullptr, 0, &needed);
    if (needed > context->limits.clipboardBytes)
    {
        return mwin_outcomeTooLarge;
    }
    bool failed = false;
    char* text = Found(context, needed, &failed);
    if (failed)
    {
        return mwin_outcomeFailed;
    }
    (void)muniConvertUtf16ToUtf8(units, length, muni_convertReplace, text, needed, &needed);
    return mwin_outcomeDone;
}

void mwinReleaseClipboard(mwinContext* context)
{
    Release(context, context->clipboardOffer, context->clipboardOfferLength);
    Release(context, context->clipboardFound, context->clipboardFoundLength);
    context->clipboardOffer = nullptr;
    context->clipboardOfferLength = 0;
    context->clipboardFound = nullptr;
    context->clipboardFoundLength = 0;
    mwinReleaseClipboardExtras(context);
}

mwinResult mwinRequestClipboardWrite(mwinContext* context, mwinWindowId window, const char* text,
                                     size_t length, mwinRequestId* requestOut)
{
    if (context == nullptr || (text == nullptr && length != 0) ||
        muniValidateUtf8(text, length).status != muni_success)
    {
        return mwinMisuse(context);
    }
    if (length > context->limits.clipboardBytes)
    {
        return mwin_errorCapacity;
    }
    char* copy = Allocate(context, length);
    if (length > 0 && copy == nullptr)
    {
        return mwin_errorCapacity;
    }
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status =
        mwinBeginRequest(context, window, mwin_requestClipboardWrite, &slot, &request);
    if (status != mwin_success)
    {
        Release(context, copy, length);
        return status;
    }
    if (length > 0)
    {
        memcpy(copy, text, length);
    }
    // Text alone: the data written before goes.
    Release(context, context->clipboardOffer, context->clipboardOfferLength);
    context->clipboardOffer = copy;
    context->clipboardOfferLength = (uint32_t)length;
    mwinReleaseClipboardData(context);
    mwinSubmitRequest(context, slot, request, requestOut);
    return mwin_success;
}

mwinResult mwinRequestClipboardRead(mwinContext* context, mwinWindowId window,
                                    mwinRequestId* requestOut)
{
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status =
        mwinBeginRequest(context, window, mwin_requestClipboardRead, &slot, &request);
    if (status == mwin_success)
    {
        mwinSubmitRequest(context, slot, request, requestOut);
    }
    return status;
}

mwinResult mwinGetClipboardText(const mwinContext* context, mwinRequestId request, char* buffer,
                                size_t capacity, size_t* lengthOut)
{
    return mwinCopyFound(
        context, request, mwin_foundText, context != nullptr ? context->clipboardFound : nullptr,
        context != nullptr ? context->clipboardFoundLength : 0, buffer, capacity, lengthOut);
}
