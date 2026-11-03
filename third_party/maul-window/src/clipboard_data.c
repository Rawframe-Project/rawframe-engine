// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The clipboard's data by MIME type and the primary selection's text.

#include "clipboard_data.h"

#include "allocator.h"
#include "utf8.h"

#include "maul-unicode/encoding.h"

#include <string.h>

static char LowerOf(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

bool mwinIsPlainText(const char* mime, size_t length)
{
    static const char plain[] = "text/plain";
    size_t size = sizeof(plain) - 1;
    for (size_t i = 0; i < size; i++)
    {
        if (i >= length || LowerOf(mime[i]) != plain[i])
        {
            return false;
        }
    }
    return length == size || mime[size] == ';' || mime[size] == ' ';
}

// A MIME type as the clipboard takes it: printable ASCII, a type and a
// subtype on either side of a slash before any parameter; past
// MWIN_MAX_CLIPBOARD_MIME bytes, mwin_errorLimit.
static mwinResult CheckMime(const char* mime, size_t length)
{
    if (mime == nullptr || length == 0)
    {
        return mwin_errorInvalid;
    }
    if (length > MWIN_MAX_CLIPBOARD_MIME)
    {
        return mwin_errorLimit;
    }
    size_t slash = 0;
    size_t end = length;
    for (size_t i = 0; i < length; i++)
    {
        if (mime[i] < 0x20 || mime[i] > 0x7E)
        {
            return mwin_errorInvalid;
        }
        end = mime[i] == ';' && end == length ? i : end;
        slash = mime[i] == '/' && slash == 0 && i < end ? i : slash;
    }
    return slash > 0 && slash + 1 < end ? mwin_success : mwin_errorInvalid;
}

bool mwinSameMime(const char* a, size_t aLength, const char* b, size_t bLength)
{
    if (aLength != bLength)
    {
        return false;
    }
    for (size_t i = 0; i < aLength; i++)
    {
        if (LowerOf(a[i]) != LowerOf(b[i]))
        {
            return false;
        }
    }
    return true;
}

const mwinClipboardDataItem* mwinFindClipboardItem(const mwinContext* context, const char* mime,
                                                   size_t length)
{
    const mwinClipboardCopy* copy = context->clipboardData;
    for (uint32_t i = 0; copy != nullptr && i < copy->count; i++)
    {
        if (mwinSameMime(copy->items[i].mime, copy->items[i].mimeLength, mime, length))
        {
            return &copy->items[i];
        }
    }
    return nullptr;
}

bool mwinOffersClipboardText(const mwinContext* context)
{
    return context->clipboardData == nullptr || context->clipboardOfferLength > 0;
}

void mwinReleaseClipboardData(mwinContext* context)
{
    mwinClipboardCopy* data = context->clipboardData;
    if (data != nullptr)
    {
        context->clipboardData = nullptr;
        mwinRelease(&context->allocator, data, data->size, alignof(mwinClipboardCopy));
    }
}

static void ReleaseBytes(const mwinContext* context, void* bytes, size_t length)
{
    if (bytes != nullptr)
    {
        mwinRelease(&context->allocator, bytes, length, 1);
    }
}

void mwinReleaseClipboardExtras(mwinContext* context)
{
    mwinReleaseClipboardData(context);
    // No read's payload is found, and no field holds a block, before the
    // free hook gets them.
    void* dataFound = context->clipboardDataFound;
    size_t dataFoundLength = context->clipboardDataFoundLength;
    char* primaryOffer = context->primaryOffer;
    size_t primaryOfferLength = context->primaryOfferLength;
    char* primaryFound = context->primaryFound;
    size_t primaryFoundLength = context->primaryFoundLength;
    context->foundPayloads[mwin_foundData] += 1;
    context->foundPayloads[mwin_foundPrimary] += 1;
    context->clipboardDataFound = nullptr;
    context->clipboardDataFoundLength = 0;
    context->primaryOffer = nullptr;
    context->primaryOfferLength = 0;
    context->primaryFound = nullptr;
    context->primaryFoundLength = 0;
    ReleaseBytes(context, dataFound, dataFoundLength);
    ReleaseBytes(context, primaryOffer, primaryOfferLength);
    ReleaseBytes(context, primaryFound, primaryFoundLength);
}

// Checks a write's items: their types, each once, a text one UTF-8; the
// bytes of the others in all, and the text's, into totalOut and textOut
// (the text item's index, or -1). mwin_errorLimit for more items or a
// longer type than the named bounds, mwin_errorInvalid for a misuse.
static mwinResult CheckItems(const mwinClipboardItem* items, uint32_t count, size_t* totalOut,
                             int* textOut)
{
    *totalOut = 0;
    *textOut = -1;
    if (items == nullptr || count == 0)
    {
        return mwin_errorInvalid;
    }
    if (count > MWIN_MAX_CLIPBOARD_ITEMS)
    {
        return mwin_errorLimit;
    }
    for (uint32_t i = 0; i < count; i++)
    {
        const mwinClipboardItem* item = &items[i];
        mwinResult status = CheckMime(item->mime, item->mimeLength);
        if (status != mwin_success)
        {
            return status;
        }
        if (item->bytes == nullptr && item->length != 0)
        {
            return mwin_errorInvalid;
        }
        for (size_t j = 0; j < i; j++)
        {
            if (mwinSameMime(items[j].mime, items[j].mimeLength, item->mime, item->mimeLength))
            {
                return mwin_errorInvalid;
            }
        }
        bool text = mwinIsPlainText(item->mime, item->mimeLength);
        if (text &&
            (*textOut >= 0 || muniValidateUtf8(item->bytes, item->length).status != muni_success))
        {
            return mwin_errorInvalid;
        }
        *textOut = text ? (int)i : *textOut;
        *totalOut = item->length > SIZE_MAX - *totalOut ? SIZE_MAX : *totalOut + item->length;
    }
    return mwin_success;
}

// Copies a write's items but the text into one block: NULL when the
// allocator has no room. None at all for a write of text alone.
static mwinClipboardCopy* CopyItems(const mwinContext* context, const mwinClipboardItem* items,
                                    uint32_t count, int text, bool* failedOut)
{
    size_t bytes = sizeof(mwinClipboardCopy);
    for (uint32_t i = 0; i < count; i++)
    {
        bytes += (int)i == text ? 0 : items[i].length;
    }
    *failedOut = false;
    if (count == 1 && text == 0)
    {
        return nullptr;
    }
    mwinClipboardCopy* copy = mwinAllocate(&context->allocator, bytes, alignof(mwinClipboardCopy));
    if (copy == nullptr)
    {
        *failedOut = true;
        return nullptr;
    }
    *copy = (mwinClipboardCopy){.size = bytes};
    uint32_t offset = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        if ((int)i == text)
        {
            continue;
        }
        mwinClipboardDataItem* item = &copy->items[copy->count++];
        memcpy(item->mime, items[i].mime, items[i].mimeLength);
        item->mime[items[i].mimeLength] = '\0';
        item->mimeLength = (uint32_t)items[i].mimeLength;
        item->offset = offset;
        item->length = (uint32_t)items[i].length;
        if (items[i].length > 0)
        {
            memcpy((uint8_t*)(copy + 1) + offset, items[i].bytes, items[i].length);
        }
        offset += item->length;
    }
    return copy;
}

mwinResult mwinWindow_RequestClipboardWriteData(mwinContext* context, mwinWindowId window,
                                                const mwinClipboardItem* items, uint32_t count,
                                                mwinRequestId* requestOut)
{
    mwinClearOut(requestOut, sizeof *requestOut);
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    size_t total = 0;
    int text = -1;
    if (context == nullptr)
    {
        return mwinMisuse(context);
    }
    mwinResult checked = CheckItems(items, count, &total, &text);
    if (checked != mwin_success)
    {
        return mwinRefuse(context, checked);
    }
    if (total > context->limits.clipboardBytes)
    {
        return mwin_errorCapacity;
    }
    size_t textLength = text >= 0 ? items[text].length : 0;
    char* textCopy = textLength > 0 ? mwinAllocate(&context->allocator, textLength, 1) : nullptr;
    bool failed = textLength > 0 && textCopy == nullptr;
    mwinClipboardCopy* copy = failed ? nullptr : CopyItems(context, items, count, text, &failed);
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status =
        failed ? mwin_errorCapacity
               : mwinBeginRequest(context, window, mwin_requestClipboardWriteData, &slot, &request);
    if (status != mwin_success)
    {
        ReleaseBytes(context, textCopy, textLength);
        if (copy != nullptr)
        {
            mwinRelease(&context->allocator, copy, copy->size, alignof(mwinClipboardCopy));
        }
        return status;
    }
    if (textLength > 0)
    {
        memcpy(textCopy, items[text].bytes, textLength);
    }
    // The write replaces the clipboard's text and data alike.
    char* last = context->clipboardOffer;
    size_t lastLength = context->clipboardOfferLength;
    context->clipboardOffer = textCopy;
    context->clipboardOfferLength = (uint32_t)textLength;
    ReleaseBytes(context, last, lastLength);
    mwinReleaseClipboardData(context);
    context->clipboardData = copy;
    mwinSubmitRequest(context, slot, request, requestOut);
    return mwin_success;
}

mwinResult mwinWindow_RequestClipboardReadData(mwinContext* context, mwinWindowId window,
                                               const char* mime, size_t mimeLength,
                                               mwinRequestId* requestOut)
{
    mwinClearOut(requestOut, sizeof *requestOut);
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    if (context == nullptr)
    {
        return mwinMisuse(context);
    }
    mwinResult checked = CheckMime(mime, mimeLength);
    if (checked == mwin_success && mwinIsPlainText(mime, mimeLength))
    {
        checked = mwin_errorInvalid;
    }
    if (checked != mwin_success)
    {
        return mwinRefuse(context, checked);
    }
    char* copy = mwinAllocate(&context->allocator, mimeLength + 1, 1);
    if (copy == nullptr)
    {
        return mwin_errorCapacity;
    }
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status =
        mwinBeginRequest(context, window, mwin_requestClipboardReadData, &slot, &request);
    if (status != mwin_success)
    {
        mwinRelease(&context->allocator, copy, mimeLength + 1, 1);
        return status;
    }
    memcpy(copy, mime, mimeLength);
    copy[mimeLength] = '\0';
    mwinRequest* entry = &context->windows[slot].requests[request];
    entry->value.text.bytes = copy;
    entry->value.text.length = (uint32_t)mimeLength;
    mwinSubmitRequest(context, slot, request, requestOut);
    return mwin_success;
}

bool mwinIsFoundBy(const mwinContext* context, mwinRequestId request, uint32_t kind)
{
    uint32_t perWindow = context->limits.requestsPerWindow;
    if (request.index1 == 0 || (request.index1 - 1) / perWindow >= context->limits.windows ||
        context->finishing)
    {
        return false;
    }
    const mwinFoundRead* read =
        &context->windows[(request.index1 - 1) / perWindow].foundReads[kind];
    return read->request.index1 == request.index1 &&
           read->request.generation == request.generation &&
           read->payload == context->foundPayloads[kind];
}

mwinResult mwinCopyFound(const mwinContext* context, mwinRequestId request, uint32_t kind,
                         const void* found, size_t length, void* buffer, size_t capacity,
                         size_t* lengthOut)
{
    if (context == nullptr || lengthOut == nullptr || (buffer == nullptr && capacity > 0))
    {
        return mwinMisuse(context);
    }
    if (!mwinIsFoundBy(context, request, kind))
    {
        return mwin_errorStale;
    }
    return mwinGiveBytes(found, length, buffer, capacity, lengthOut);
}

mwinResult mwinRequest_GetClipboardData(const mwinContext* context, mwinRequestId request,
                                        void* bufferOut, size_t capacity, size_t* lengthOut)
{
    mwinClearOut(lengthOut, sizeof *lengthOut);
    return mwinClearOnFailure(
        mwinCopyFound(context, request, mwin_foundData,
                      context != nullptr ? context->clipboardDataFound : nullptr,
                      context != nullptr ? context->clipboardDataFoundLength : 0, bufferOut,
                      capacity, lengthOut),
        bufferOut, capacity);
}

mwinResult mwinRequest_GetPrimaryText(const mwinContext* context, mwinRequestId request,
                                      char* bufferOut, size_t capacity, size_t* lengthOut)
{
    mwinClearOut(lengthOut, sizeof *lengthOut);
    return mwinClearOnFailure(mwinCopyFound(context, request, mwin_foundPrimary,
                                            context != nullptr ? context->primaryFound : nullptr,
                                            context != nullptr ? context->primaryFoundLength : 0,
                                            bufferOut, capacity, lengthOut),
                              bufferOut, capacity);
}

mwinResult mwinWindow_RequestPrimaryWrite(mwinContext* context, mwinWindowId window,
                                          const char* text, size_t length,
                                          mwinRequestId* requestOut)
{
    mwinClearOut(requestOut, sizeof *requestOut);
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    if (context == nullptr || (text == nullptr && length != 0) ||
        muniValidateUtf8(text, length).status != muni_success)
    {
        return mwinMisuse(context);
    }
    if (length > context->limits.clipboardBytes)
    {
        return mwin_errorCapacity;
    }
    char* copy = length > 0 ? mwinAllocate(&context->allocator, length, 1) : nullptr;
    if (length > 0 && copy == nullptr)
    {
        return mwin_errorCapacity;
    }
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status =
        mwinBeginRequest(context, window, mwin_requestPrimaryWrite, &slot, &request);
    if (status != mwin_success)
    {
        ReleaseBytes(context, copy, length);
        return status;
    }
    if (length > 0)
    {
        memcpy(copy, text, length);
    }
    ReleaseBytes(context, context->primaryOffer, context->primaryOfferLength);
    context->primaryOffer = copy;
    context->primaryOfferLength = (uint32_t)length;
    mwinSubmitRequest(context, slot, request, requestOut);
    return mwin_success;
}

mwinResult mwinWindow_RequestPrimaryRead(mwinContext* context, mwinWindowId window,
                                         mwinRequestId* requestOut)
{
    mwinClearOut(requestOut, sizeof *requestOut);
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestPrimaryRead, &slot, &request);
    if (status == mwin_success)
    {
        mwinSubmitRequest(context, slot, request, requestOut);
    }
    return status;
}

mwinOutcome mwinTakeClipboardData(mwinContext* context, const void* bytes, size_t length)
{
    if (length > context->limits.clipboardBytes)
    {
        return mwin_outcomeTooLarge;
    }
    void* copy = length > 0 ? mwinAllocate(&context->allocator, length, 1) : nullptr;
    if (length > 0 && copy == nullptr)
    {
        return mwin_outcomeFailed;
    }
    if (length > 0)
    {
        memcpy(copy, bytes, length);
    }
    // In place, with its count, before the free hook gets the last bytes,
    // as for a read's text.
    void* last = context->clipboardDataFound;
    size_t lastLength = context->clipboardDataFoundLength;
    context->clipboardDataFound = copy;
    context->clipboardDataFoundLength = (uint32_t)length;
    context->foundPayloads[mwin_foundData] += 1;
    ReleaseBytes(context, last, lastLength);
    return mwin_outcomeDone;
}

mwinOutcome mwinTakePrimaryText(mwinContext* context, const char* bytes, size_t length)
{
    // Repairing never shortens the text.
    size_t needed =
        length <= context->limits.clipboardBytes ? mwinRepairUtf8(bytes, length, nullptr) : length;
    if (needed > context->limits.clipboardBytes)
    {
        return mwin_outcomeTooLarge;
    }
    char* text = needed > 0 ? mwinAllocate(&context->allocator, needed, 1) : nullptr;
    if (needed > 0 && text == nullptr)
    {
        return mwin_outcomeFailed;
    }
    (void)mwinRepairUtf8(bytes, length, text);
    char* last = context->primaryFound;
    size_t lastLength = context->primaryFoundLength;
    context->primaryFound = text;
    context->primaryFoundLength = (uint32_t)needed;
    context->foundPayloads[mwin_foundPrimary] += 1;
    ReleaseBytes(context, last, lastLength);
    return mwin_outcomeDone;
}
