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
// subtype on either side of a slash before any parameter.
static bool IsMime(const char* mime, size_t length)
{
    if (mime == nullptr || length == 0 || length > MWIN_CLIPBOARD_MIME)
    {
        return false;
    }
    size_t slash = 0;
    size_t end = length;
    for (size_t i = 0; i < length; i++)
    {
        if (mime[i] < 0x20 || mime[i] > 0x7E)
        {
            return false;
        }
        end = mime[i] == ';' && end == length ? i : end;
        slash = mime[i] == '/' && slash == 0 && i < end ? i : slash;
    }
    return slash > 0 && slash + 1 < end;
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
    if (context->clipboardData != nullptr)
    {
        mwinRelease(&context->allocator, context->clipboardData, context->clipboardData->size,
                    alignof(mwinClipboardCopy));
        context->clipboardData = nullptr;
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
    ReleaseBytes(context, context->clipboardDataFound, context->clipboardDataFoundLength);
    ReleaseBytes(context, context->primaryOffer, context->primaryOfferLength);
    ReleaseBytes(context, context->primaryFound, context->primaryFoundLength);
    context->clipboardDataFound = nullptr;
    context->clipboardDataFoundLength = 0;
    context->primaryOffer = nullptr;
    context->primaryOfferLength = 0;
    context->primaryFound = nullptr;
    context->primaryFoundLength = 0;
}

// Checks a write's items: their types, each once, a text one UTF-8; the
// bytes of the others in all, and the text's, into totalOut and textOut
// (the text item's index, or -1). False for a misuse.
static bool CheckItems(const mwinClipboardItem* items, size_t count, size_t* totalOut, int* textOut)
{
    *totalOut = 0;
    *textOut = -1;
    if (items == nullptr || count == 0 || count > MWIN_CLIPBOARD_ITEMS)
    {
        return false;
    }
    for (size_t i = 0; i < count; i++)
    {
        const mwinClipboardItem* item = &items[i];
        if (!IsMime(item->mime, item->mimeLength) || (item->bytes == nullptr && item->length != 0))
        {
            return false;
        }
        for (size_t j = 0; j < i; j++)
        {
            if (mwinSameMime(items[j].mime, items[j].mimeLength, item->mime, item->mimeLength))
            {
                return false;
            }
        }
        bool text = mwinIsPlainText(item->mime, item->mimeLength);
        if (text &&
            (*textOut >= 0 || muniValidateUtf8(item->bytes, item->length).status != muni_success))
        {
            return false;
        }
        *textOut = text ? (int)i : *textOut;
        *totalOut = item->length > SIZE_MAX - *totalOut ? SIZE_MAX : *totalOut + item->length;
    }
    return true;
}

// Copies a write's items but the text into one block: NULL when the
// allocator has no room. None at all for a write of text alone.
static mwinClipboardCopy* CopyItems(const mwinContext* context, const mwinClipboardItem* items,
                                    size_t count, int text, bool* failedOut)
{
    size_t bytes = sizeof(mwinClipboardCopy);
    for (size_t i = 0; i < count; i++)
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
    for (size_t i = 0; i < count; i++)
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

mwinResult mwinRequestClipboardWriteData(mwinContext* context, mwinWindowId window,
                                         const mwinClipboardItem* items, size_t count,
                                         mwinRequestId* requestOut)
{
    size_t total = 0;
    int text = -1;
    if (context == nullptr || !CheckItems(items, count, &total, &text))
    {
        return mwinMisuse(context);
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
    ReleaseBytes(context, context->clipboardOffer, context->clipboardOfferLength);
    context->clipboardOffer = textCopy;
    context->clipboardOfferLength = (uint32_t)textLength;
    mwinReleaseClipboardData(context);
    context->clipboardData = copy;
    mwinSubmitRequest(context, slot, request, requestOut);
    return mwin_success;
}

mwinResult mwinRequestClipboardReadData(mwinContext* context, mwinWindowId window, const char* mime,
                                        size_t mimeLength, mwinRequestId* requestOut)
{
    if (context == nullptr || !IsMime(mime, mimeLength) || mwinIsPlainText(mime, mimeLength))
    {
        return mwinMisuse(context);
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

// Copies out a found buffer, as the text calls do.
static mwinResult CopyOut(const mwinContext* context, const void* found, size_t length,
                          void* buffer, size_t capacity, size_t* lengthOut)
{
    if (context == nullptr || lengthOut == nullptr || (buffer == nullptr && capacity > 0))
    {
        return mwinMisuse(context);
    }
    if (length > 0 && capacity > 0)
    {
        memcpy(buffer, found, length < capacity ? length : capacity);
    }
    *lengthOut = length;
    return length > capacity ? mwin_errorCapacity : mwin_success;
}

mwinResult mwinGetClipboardData(const mwinContext* context, void* buffer, size_t capacity,
                                size_t* lengthOut)
{
    return CopyOut(context, context != nullptr ? context->clipboardDataFound : nullptr,
                   context != nullptr ? context->clipboardDataFoundLength : 0, buffer, capacity,
                   lengthOut);
}

mwinResult mwinGetPrimaryText(const mwinContext* context, char* buffer, size_t capacity,
                              size_t* lengthOut)
{
    return CopyOut(context, context != nullptr ? context->primaryFound : nullptr,
                   context != nullptr ? context->primaryFoundLength : 0, buffer, capacity,
                   lengthOut);
}

mwinResult mwinRequestPrimaryWrite(mwinContext* context, mwinWindowId window, const char* text,
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

mwinResult mwinRequestPrimaryRead(mwinContext* context, mwinWindowId window,
                                  mwinRequestId* requestOut)
{
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestPrimaryRead, &slot, &request);
    if (status == mwin_success)
    {
        mwinSubmitRequest(context, slot, request, requestOut);
    }
    return status;
}

// Holds bytes found in place of a buffer's, the old one given back: the
// outcome.
static mwinOutcome Hold(mwinContext* context, void** found, uint32_t* foundLength,
                        const void* bytes, size_t length)
{
    void* copy = length > 0 ? mwinAllocate(&context->allocator, length, 1) : nullptr;
    if (length > 0 && copy == nullptr)
    {
        return mwin_outcomeFailed;
    }
    if (length > 0)
    {
        memcpy(copy, bytes, length);
    }
    ReleaseBytes(context, *found, *foundLength);
    *found = copy;
    *foundLength = (uint32_t)length;
    return mwin_outcomeDone;
}

mwinOutcome mwinTakeClipboardData(mwinContext* context, const void* bytes, size_t length)
{
    if (length > context->limits.clipboardBytes)
    {
        return mwin_outcomeTooLarge;
    }
    void* found = context->clipboardDataFound;
    mwinOutcome outcome = Hold(context, &found, &context->clipboardDataFoundLength, bytes, length);
    context->clipboardDataFound = found;
    return outcome;
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
    ReleaseBytes(context, context->primaryFound, context->primaryFoundLength);
    context->primaryFound = text;
    context->primaryFoundLength = (uint32_t)needed;
    return mwin_outcomeDone;
}
