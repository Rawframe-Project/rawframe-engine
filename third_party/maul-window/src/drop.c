// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Drops: their files and text as backends gather them, and the copies
// the program takes.

#include "maul-window/drop.h"

#include "allocator.h"
#include "core.h"
#include "utf8.h"

#include "maul-unicode/encoding.h"

#include <string.h>

static void ReleasePayload(const mwinContext* context, mwinDropPayload* payload)
{
    mwinListRelease(&payload->files, &context->allocator);
    if (payload->text != nullptr)
    {
        mwinRelease(&context->allocator, payload->text, payload->textLength, 1);
    }
    *payload = (mwinDropPayload){0};
}

void mwinBeginDrop(mwinContext* context)
{
    ReleasePayload(context, &context->dropping);
}

static mwinListBounds Bounds(const mwinContext* context)
{
    return (mwinListBounds){&context->allocator, context->limits.droppedFiles,
                            context->limits.dropBytes};
}

// A path that names no file the program could open, or past the limits,
// is left out and the drop marked truncated.
void mwinAddDroppedFile(mwinContext* context, const char* path, size_t length)
{
    if (mwinListAdd(&context->dropping.files, Bounds(context), path, length) != mwin_listAdded)
    {
        context->dropping.truncated = true;
    }
}

void mwinAddDroppedFileUtf16(mwinContext* context, const uint16_t* path, size_t length)
{
    if (mwinListAddUtf16(&context->dropping.files, Bounds(context), path, length) != mwin_listAdded)
    {
        context->dropping.truncated = true;
    }
}

// Room for the text of a length, the drop marked truncated when it has
// none.
static char* TextRoom(mwinContext* context, size_t length)
{
    mwinDropPayload* drop = &context->dropping;
    if (length > context->limits.dropBytes)
    {
        drop->truncated = true;
        return nullptr;
    }
    char* text = length > 0 ? mwinAllocate(&context->allocator, length, 1) : nullptr;
    drop->truncated = drop->truncated || (length > 0 && text == nullptr);
    if (text != nullptr)
    {
        if (drop->text != nullptr)
        {
            mwinRelease(&context->allocator, drop->text, drop->textLength, 1);
        }
        drop->text = text;
        drop->textLength = (uint32_t)length;
    }
    return text;
}

void mwinSetDroppedText(mwinContext* context, const char* bytes, size_t length)
{
    // Repairing never shortens the text.
    if (length > context->limits.dropBytes)
    {
        context->dropping.truncated = true;
        return;
    }
    char* text = TextRoom(context, mwinRepairUtf8(bytes, length, nullptr));
    if (text != nullptr)
    {
        (void)mwinRepairUtf8(bytes, length, text);
    }
}

void mwinSetDroppedTextUtf16(mwinContext* context, const uint16_t* units, size_t length)
{
    size_t needed = 0;
    (void)muniConvertUtf16ToUtf8(units, length, muni_convertReplace, nullptr, 0, &needed);
    char* text = TextRoom(context, needed);
    if (text != nullptr)
    {
        (void)muniConvertUtf16ToUtf8(units, length, muni_convertReplace, text, needed, &needed);
    }
}

void mwinFinishDrop(mwinContext* context, uint32_t slot, mwinPosition position, uint64_t timeNs)
{
    ReleasePayload(context, &context->dropped);
    context->dropped = context->dropping;
    context->dropping = (mwinDropPayload){0};
    context->dropNumber += 1;
    const mwinDropPayload* drop = &context->dropped;
    mwinEvent event = {.type = mwin_eventDropped, .timeNs = timeNs};
    event.data.drop = (mwinDropEvent){position, context->dropNumber, drop->files.count,
                                      drop->textLength, drop->truncated};
    mwinPost(context, slot, &event);
}

void mwinReleaseDrops(mwinContext* context)
{
    ReleasePayload(context, &context->dropping);
    ReleasePayload(context, &context->dropped);
}

// Copies bytes of the delivered drop out.
static mwinResult CopyOut(const mwinContext* context, uint32_t drop, const char* bytes,
                          size_t length, char* buffer, size_t capacity, size_t* lengthOut)
{
    if (context == nullptr || lengthOut == nullptr || (buffer == nullptr && capacity > 0))
    {
        return mwinMisuse(context);
    }
    if (drop == 0 || drop != context->dropNumber)
    {
        return mwin_errorStale;
    }
    if (length > 0 && capacity > 0)
    {
        memcpy(buffer, bytes, length < capacity ? length : capacity);
    }
    *lengthOut = length;
    return length > capacity ? mwin_errorCapacity : mwin_success;
}

mwinResult mwinGetDroppedFiles(const mwinContext* context, uint32_t drop, char* buffer,
                               size_t capacity, size_t* lengthOut)
{
    return CopyOut(context, drop, context != nullptr ? context->dropped.files.bytes : nullptr,
                   context != nullptr ? context->dropped.files.length : 0, buffer, capacity,
                   lengthOut);
}

mwinResult mwinGetDroppedText(const mwinContext* context, uint32_t drop, char* buffer,
                              size_t capacity, size_t* lengthOut)
{
    return CopyOut(context, drop, context != nullptr ? context->dropped.text : nullptr,
                   context != nullptr ? context->dropped.textLength : 0, buffer, capacity,
                   lengthOut);
}
