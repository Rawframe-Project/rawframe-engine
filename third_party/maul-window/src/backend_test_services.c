// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The services the test platform plays: its clipboard, drops, and the
// addresses it opens and paths it reveals.

#include "accessibility.h"
#include "allocator.h"
#include "backend_test.h"
#include "clipboard_data.h"
#include "dialog.h"
#include "icon.h"

#include "maul-window/test.h"

#include <stdio.h>
#include <string.h>

// Lets go of the platform's text, or its data.
static void ReleaseText(const mwinContext* context, mwinTestPlatform* platform)
{
    if (platform->clipboard != nullptr)
    {
        mwinRelease(&context->allocator, platform->clipboard, platform->clipboardBytes,
                    alignof(uint16_t));
    }
    platform->clipboard = nullptr;
    platform->clipboardBytes = 0;
}

static void ReleaseData(const mwinContext* context, mwinTestPlatform* platform)
{
    if (platform->data != nullptr)
    {
        mwinRelease(&context->allocator, platform->data, platform->data->size,
                    alignof(mwinClipboardCopy));
    }
    platform->data = nullptr;
}

void mwinTestReleaseClipboard(const mwinContext* context, mwinTestPlatform* platform)
{
    ReleaseText(context, platform);
    ReleaseData(context, platform);
    if (platform->primary != nullptr)
    {
        mwinRelease(&context->allocator, platform->primary, platform->primaryBytes, 1);
    }
    platform->primary = nullptr;
    platform->primaryBytes = 0;
}

// Puts bytes on the platform's clipboard; false when there is no room.
static bool SetClipboard(const mwinContext* context, const void* data, size_t bytes, bool utf16)
{
    mwinTestPlatform* platform = mwinTestPlatformOf(context);
    void* copy = bytes > 0 ? mwinAllocate(&context->allocator, bytes, alignof(uint16_t)) : nullptr;
    if (bytes > 0 && copy == nullptr)
    {
        return false;
    }
    if (bytes > 0)
    {
        memcpy(copy, data, bytes);
    }
    ReleaseText(context, platform);
    platform->clipboard = copy;
    platform->clipboardBytes = bytes;
    platform->utf16 = utf16;
    return true;
}

// Puts a copy of a data block on the platform's clipboard, or none;
// false when there is no room.
static bool SetData(const mwinContext* context, const mwinClipboardCopy* data)
{
    mwinTestPlatform* platform = mwinTestPlatformOf(context);
    mwinClipboardCopy* copy =
        data != nullptr ? mwinAllocate(&context->allocator, data->size, alignof(mwinClipboardCopy))
                        : nullptr;
    if (data != nullptr && copy == nullptr)
    {
        return false;
    }
    if (data != nullptr)
    {
        memcpy(copy, data, data->size);
    }
    ReleaseData(context, platform);
    platform->data = copy;
    return true;
}

static bool SetPrimary(const mwinContext* context, const char* bytes, size_t length)
{
    mwinTestPlatform* platform = mwinTestPlatformOf(context);
    char* copy = length > 0 ? mwinAllocate(&context->allocator, length, 1) : nullptr;
    if (length > 0 && copy == nullptr)
    {
        return false;
    }
    if (length > 0)
    {
        memcpy(copy, bytes, length);
    }
    if (platform->primary != nullptr)
    {
        mwinRelease(&context->allocator, platform->primary, platform->primaryBytes, 1);
    }
    platform->primary = copy;
    platform->primaryBytes = length;
    return true;
}

// The platform's item of a type, or NULL.
static const mwinClipboardDataItem* FindItem(const mwinTestPlatform* platform, const char* mime,
                                             size_t length)
{
    const mwinClipboardCopy* data = platform->data;
    for (uint32_t i = 0; data != nullptr && i < data->count; i++)
    {
        const mwinClipboardDataItem* item = &data->items[i];
        if (item->mimeLength == length && memcmp(item->mime, mime, length) == 0)
        {
            return item;
        }
    }
    return nullptr;
}

mwinOutcome mwinTestUseClipboard(mwinContext* context, const mwinRequest* request)
{
    const mwinTestPlatform* platform = mwinTestPlatformOf(context);
    bool done = true;
    switch (request->kind)
    {
    case mwin_requestClipboardWrite:
        // Text alone: the data goes.
        done =
            SetClipboard(context, context->clipboardOffer, context->clipboardOfferLength, false) &&
            SetData(context, nullptr);
        break;
    case mwin_requestClipboardWriteData:
        done =
            SetClipboard(context, context->clipboardOffer, context->clipboardOfferLength, false) &&
            SetData(context, context->clipboardData);
        break;
    case mwin_requestClipboardReadData:
    {
        const mwinClipboardDataItem* item =
            FindItem(platform, request->value.text.bytes, request->value.text.length);
        return item != nullptr
                   ? mwinTakeClipboardData(context, mwinClipboardBytesOf(platform->data, item),
                                           item->length)
                   : mwin_outcomeFailed;
    }
    case mwin_requestPrimaryWrite:
        done = SetPrimary(context, context->primaryOffer, context->primaryOfferLength);
        break;
    case mwin_requestPrimaryRead:
        return mwinTakePrimaryText(context, platform->primary, platform->primaryBytes);
    default:
        return platform->utf16
                   ? mwinTakeClipboardUtf16(context, platform->clipboard,
                                            platform->clipboardBytes / 2)
                   : mwinTakeClipboardText(context, platform->clipboard, platform->clipboardBytes);
    }
    return done ? mwin_outcomeDone : mwin_outcomeFailed;
}

mwinResult mwinTestSetClipboard(mwinContext* context, const char* bytes, size_t length)
{
    if (context == nullptr || (bytes == nullptr && length != 0))
    {
        return mwinMisuse(context);
    }
    if (mwinTestPlatformOf(context) == nullptr)
    {
        return mwin_errorUnsupported;
    }
    return SetClipboard(context, bytes, length, false) ? mwin_success : mwin_errorCapacity;
}

mwinResult mwinTestSetClipboardUtf16(mwinContext* context, const uint16_t* units, size_t length)
{
    if (context == nullptr || (units == nullptr && length != 0))
    {
        return mwinMisuse(context);
    }
    if (mwinTestPlatformOf(context) == nullptr)
    {
        return mwin_errorUnsupported;
    }
    return SetClipboard(context, units, length * 2, true) ? mwin_success : mwin_errorCapacity;
}

mwinResult mwinTestGetClipboard(const mwinContext* context, char* buffer, size_t capacity,
                                size_t* lengthOut)
{
    if (context == nullptr || lengthOut == nullptr || (buffer == nullptr && capacity > 0))
    {
        return mwinMisuse(context);
    }
    const mwinTestPlatform* platform = mwinTestPlatformOf(context);
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    size_t length = platform->clipboardBytes;
    if (length > 0 && capacity > 0)
    {
        memcpy(buffer, platform->clipboard, length < capacity ? length : capacity);
    }
    *lengthOut = length;
    return length > capacity ? mwin_errorCapacity : mwin_success;
}

mwinResult mwinTestSetClipboardData(mwinContext* context, const char* mime, size_t mimeLength,
                                    const void* bytes, size_t length)
{
    if (context == nullptr || mime == nullptr || mimeLength == 0 ||
        mimeLength > MWIN_CLIPBOARD_MIME || (bytes == nullptr && length != 0))
    {
        return mwinMisuse(context);
    }
    if (mwinTestPlatformOf(context) == nullptr)
    {
        return mwin_errorUnsupported;
    }
    size_t size = sizeof(mwinClipboardCopy) + length;
    mwinClipboardCopy* data = mwinAllocate(&context->allocator, size, alignof(mwinClipboardCopy));
    if (data == nullptr)
    {
        return mwin_errorCapacity;
    }
    *data = (mwinClipboardCopy){.size = size, .count = 1};
    memcpy(data->items[0].mime, mime, mimeLength);
    data->items[0].mimeLength = (uint32_t)mimeLength;
    data->items[0].length = (uint32_t)length;
    if (length > 0)
    {
        memcpy(data + 1, bytes, length);
    }
    // Another program's copy: the text goes with what was there.
    bool done = SetClipboard(context, nullptr, 0, false) && SetData(context, data);
    mwinRelease(&context->allocator, data, size, alignof(mwinClipboardCopy));
    return done ? mwin_success : mwin_errorCapacity;
}

mwinResult mwinTestGetClipboardData(const mwinContext* context, const char* mime, size_t mimeLength,
                                    void* buffer, size_t capacity, size_t* lengthOut)
{
    if (context == nullptr || mime == nullptr || lengthOut == nullptr ||
        (buffer == nullptr && capacity > 0))
    {
        return mwinMisuse(context);
    }
    const mwinTestPlatform* platform = mwinTestPlatformOf(context);
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    const mwinClipboardDataItem* item = FindItem(platform, mime, mimeLength);
    if (item == nullptr)
    {
        return mwinMisuse(context);
    }
    if (item->length > 0 && capacity > 0)
    {
        memcpy(buffer, mwinClipboardBytesOf(platform->data, item),
               item->length < capacity ? item->length : capacity);
    }
    *lengthOut = item->length;
    return item->length > capacity ? mwin_errorCapacity : mwin_success;
}

mwinResult mwinTestSetPrimary(mwinContext* context, const char* bytes, size_t length)
{
    if (context == nullptr || (bytes == nullptr && length != 0))
    {
        return mwinMisuse(context);
    }
    if (mwinTestPlatformOf(context) == nullptr)
    {
        return mwin_errorUnsupported;
    }
    return SetPrimary(context, bytes, length) ? mwin_success : mwin_errorCapacity;
}

mwinResult mwinTestGetPrimary(const mwinContext* context, char* buffer, size_t capacity,
                              size_t* lengthOut)
{
    if (context == nullptr || lengthOut == nullptr || (buffer == nullptr && capacity > 0))
    {
        return mwinMisuse(context);
    }
    const mwinTestPlatform* platform = mwinTestPlatformOf(context);
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    size_t length = platform->primaryBytes;
    if (length > 0 && capacity > 0)
    {
        memcpy(buffer, platform->primary, length < capacity ? length : capacity);
    }
    *lengthOut = length;
    return length > capacity ? mwin_errorCapacity : mwin_success;
}

mwinResult mwinTestDrop(mwinContext* context, mwinWindowId window, mwinPosition position,
                        const char* files, size_t filesLength, const char* text, size_t textLength)
{
    if (context == nullptr || (files == nullptr && filesLength != 0) ||
        (filesLength > 0 && files[filesLength - 1] != '\0'))
    {
        return mwinMisuse(context);
    }
    mwinTestPlatform* platform = mwinTestPlatformOf(context);
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    if (mwinFindWindow(context, window) == nullptr)
    {
        return mwin_errorStale;
    }
    if (platform->dropWaiting)
    {
        return mwin_errorState;
    }
    mwinEvent report = {.type = mwin_eventDropped, .window = window, .timeNs = platform->timeNs};
    report.data.drop.position = position;
    mwinResult status = mwinTestQueueReport(platform, &report);
    if (status != mwin_success)
    {
        return status;
    }
    mwinBeginDrop(context);
    for (size_t at = 0; at < filesLength; at += strlen(files + at) + 1)
    {
        mwinAddDroppedFile(context, files + at, strlen(files + at));
    }
    if (text != nullptr)
    {
        mwinSetDroppedText(context, text, textLength);
    }
    platform->dropWaiting = true;
    return mwin_success;
}

void mwinTestOpen(mwinContext* context, const mwinRequest* request)
{
    mwinTestPlatform* platform = mwinTestPlatformOf(context);
    if (platform == nullptr)
    {
        return;
    }
    int which = request->kind == mwin_requestOpenUrl ? 0 : 1;
    memcpy(platform->opened[which], request->value.text.bytes, request->value.text.length);
    platform->openedLength[which] = request->value.text.length;
}

mwinResult mwinTestGetOpened(const mwinContext* context, mwinRequestKind kind, char* buffer,
                             size_t capacity, size_t* lengthOut)
{
    bool known = kind == mwin_requestOpenUrl || kind == mwin_requestRevealFile;
    if (context == nullptr || lengthOut == nullptr || !known || (buffer == nullptr && capacity > 0))
    {
        return mwinMisuse(context);
    }
    const mwinTestPlatform* platform = mwinTestPlatformOf(context);
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    int which = kind == mwin_requestOpenUrl ? 0 : 1;
    size_t length = platform->openedLength[which];
    if (length > 0 && capacity > 0)
    {
        memcpy(buffer, platform->opened[which], length < capacity ? length : capacity);
    }
    *lengthOut = length;
    return length > capacity ? mwin_errorCapacity : mwin_success;
}

mwinResult mwinTestSetDialogFiles(mwinContext* context, const char* files, size_t length)
{
    if (context == nullptr || (files == nullptr && length > 0) || length > MWIN_TEST_DIALOG_BYTES ||
        (length > 0 && files[length - 1] != '\0'))
    {
        return mwinMisuse(context);
    }
    mwinTestPlatform* platform = mwinTestPlatformOf(context);
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    if (length > 0)
    {
        memcpy(platform->dialogFiles, files, length);
    }
    platform->dialogFilesLength = (uint32_t)length;
    return mwin_success;
}

// Adds a line to the dialog's description, cut at its end.
static void Line(mwinTestPlatform* platform, const char* first, const char* second)
{
    int written = snprintf(platform->dialog + platform->dialogLength,
                           MWIN_TEST_DIALOG_BYTES - platform->dialogLength, "%s%s%s\n", first,
                           second != nullptr ? ":" : "", second != nullptr ? second : "");
    size_t room = MWIN_TEST_DIALOG_BYTES - platform->dialogLength - 1;
    size_t added = written < 0 ? 0 : (size_t)written;
    platform->dialogLength += (uint32_t)(added < room ? added : room);
}

mwinOutcome mwinTestAnswerDialog(mwinContext* context, uint32_t slot, uint32_t request,
                                 mwinOutcome outcome)
{
    mwinTestPlatform* platform = mwinTestPlatformOf(context);
    const mwinDialogCopy* copy = context->windows[slot].requests[request].value.dialog;
    char kind[2] = {(char)('0' + copy->kind), '\0'};
    platform->dialogLength = 0;
    Line(platform, kind, nullptr);
    Line(platform, copy->title, nullptr);
    Line(platform, copy->folder, nullptr);
    Line(platform, copy->name, nullptr);
    for (uint32_t i = 0; i < copy->filterCount; i++)
    {
        Line(platform, copy->filters[i].name, copy->filters[i].extensions);
    }
    mwinBeginDialog(context);
    for (size_t at = 0; outcome == mwin_outcomeDone && at < platform->dialogFilesLength;
         at += strlen(platform->dialogFiles + at) + 1)
    {
        mwinAddDialogFile(context, platform->dialogFiles + at, strlen(platform->dialogFiles + at));
    }
    return mwinSettleDialog(context, slot, request, outcome);
}

mwinResult mwinTestGetDialog(const mwinContext* context, char* buffer, size_t capacity,
                             size_t* lengthOut)
{
    if (context == nullptr || lengthOut == nullptr || (buffer == nullptr && capacity > 0))
    {
        return mwinMisuse(context);
    }
    const mwinTestPlatform* platform = mwinTestPlatformOf(context);
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    size_t length = platform->dialogLength;
    if (length > 0 && capacity > 0)
    {
        memcpy(buffer, platform->dialog, length < capacity ? length : capacity);
    }
    *lengthOut = length;
    return length > capacity ? mwin_errorCapacity : mwin_success;
}

static uint64_t Hash(uint64_t hash, const uint8_t* bytes, size_t length)
{
    for (size_t i = 0; i < length; i++)
    {
        hash = (hash ^ bytes[i]) * 0x100000001B3u;
    }
    return hash;
}

void mwinTestSetIcon(mwinContext* context, const mwinRequest* request)
{
    mwinTestPlatform* platform = mwinTestPlatformOf(context);
    const mwinIconCopy* icon = request->value.icon;
    uint64_t hash = 0xCBF29CE484222325u;
    for (uint32_t i = 0; i < icon->count; i++)
    {
        const mwinIconCopyImage* image = &icon->images[i];
        uint8_t size[8];
        for (int b = 0; b < 4; b++)
        {
            size[b] = (uint8_t)(image->width >> (8 * b));
            size[4 + b] = (uint8_t)(image->height >> (8 * b));
        }
        hash = Hash(hash, size, sizeof(size));
        hash = Hash(hash, image->pixels, (size_t)image->width * image->height * 4);
    }
    platform->iconCount = icon->count;
    platform->iconChecksum = hash;
}

mwinResult mwinTestGetIcon(const mwinContext* context, uint32_t* countOut, uint64_t* checksumOut)
{
    if (context == nullptr || countOut == nullptr || checksumOut == nullptr)
    {
        return mwinMisuse(context);
    }
    const mwinTestPlatform* platform = mwinTestPlatformOf(context);
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    *countOut = platform->iconCount;
    *checksumOut = platform->iconChecksum;
    return mwin_success;
}

mwinResult mwinTestAskAccessibility(mwinContext* context, mwinWindowId window)
{
    if (context == nullptr)
    {
        return mwinMisuse(context);
    }
    if (mwinTestPlatformOf(context) == nullptr)
    {
        return mwin_errorUnsupported;
    }
    if (mwinFindWindow(context, window) == nullptr)
    {
        return mwin_errorStale;
    }
    mwinNoteAccessibilityAsked(context, window.index1 - 1);
    return mwin_success;
}

// The modifiers a chord is set and asked with: Shift, Control, Alt, Meta.
#define CHORD_MODIFIERS (mwin_modShift | mwin_modControl | mwin_modAlt | mwin_modMeta)

mwinKeyReach mwinTestKeyReachOf(const mwinContext* context, mwinKeyCode code,
                                mwinModifiers modifiers)
{
    const mwinTestPlatform* platform = mwinTestPlatformOf(context);
    for (uint32_t i = 0; i < platform->keyReachCount; i++)
    {
        const mwinTestKeyReach* set = &platform->keyReaches[i];
        if (set->code == code && set->modifiers == (modifiers & CHORD_MODIFIERS))
        {
            return set->reach;
        }
    }
    return mwin_keyReachDelivered;
}

mwinResult mwinTestSetKeyReach(mwinContext* context, mwinKeyCode code, mwinModifiers modifiers,
                               mwinKeyReach reach)
{
    if (context == nullptr || code == mwin_codeUnknown || code > mwin_codeMetaRight ||
        reach > mwin_keyReachNever)
    {
        return mwinMisuse(context);
    }
    mwinTestPlatform* platform = mwinTestPlatformOf(context);
    if (platform == nullptr)
    {
        return mwin_errorUnsupported;
    }
    uint8_t chord = (uint8_t)(modifiers & CHORD_MODIFIERS);
    uint32_t found = 0;
    while (found < platform->keyReachCount && (platform->keyReaches[found].code != code ||
                                               platform->keyReaches[found].modifiers != chord))
    {
        found++;
    }
    if (reach == mwin_keyReachDelivered)
    {
        if (found < platform->keyReachCount)
        {
            platform->keyReaches[found] = platform->keyReaches[--platform->keyReachCount];
        }
        return mwin_success;
    }
    if (found == MWIN_TEST_KEY_REACHES)
    {
        return mwin_errorCapacity;
    }
    platform->keyReaches[found] = (mwinTestKeyReach){code, chord, reach};
    platform->keyReachCount += found == platform->keyReachCount;
    return mwin_success;
}
