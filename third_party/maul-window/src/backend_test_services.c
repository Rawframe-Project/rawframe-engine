// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The services the test platform plays: its clipboard, drops, and the
// addresses it opens and paths it reveals.

#include "accessibility.h"
#include "allocator.h"
#include "backend_test.h"
#include "dialog.h"
#include "icon.h"

#include "maul-window/test.h"

#include <stdio.h>
#include <string.h>

void mwinTestReleaseClipboard(const mwinContext* context, mwinTestPlatform* platform)
{
    if (platform->clipboard != nullptr)
    {
        mwinRelease(&context->allocator, platform->clipboard, platform->clipboardBytes,
                    alignof(uint16_t));
    }
    platform->clipboard = nullptr;
    platform->clipboardBytes = 0;
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
    mwinTestReleaseClipboard(context, platform);
    platform->clipboard = copy;
    platform->clipboardBytes = bytes;
    platform->utf16 = utf16;
    return true;
}

mwinOutcome mwinTestUseClipboard(mwinContext* context, mwinRequestKind kind)
{
    const mwinTestPlatform* platform = mwinTestPlatformOf(context);
    if (kind == mwin_requestClipboardWrite)
    {
        return SetClipboard(context, context->clipboardOffer, context->clipboardOfferLength, false)
                   ? mwin_outcomeDone
                   : mwin_outcomeFailed;
    }
    return platform->utf16
               ? mwinTakeClipboardUtf16(context, platform->clipboard, platform->clipboardBytes / 2)
               : mwinTakeClipboardText(context, platform->clipboard, platform->clipboardBytes);
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
