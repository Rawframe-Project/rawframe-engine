// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// File dialogs: the def's copy and the paths chosen.

#include "dialog.h"

#include "allocator.h"

#include "maul-unicode/encoding.h"
#include "maul-window/services.h"

#include <stdalign.h>
#include <stdckdint.h>
#include <string.h>

#define DIALOG_COOKIE 0x6D776664u // "mwfd"

mwinFileDialogDef mwinDefaultFileDialogDef(void)
{
    return (mwinFileDialogDef){.cookie = DIALOG_COOKIE, .kind = mwin_dialogOpen};
}

static bool IsText(const char* text, size_t length, size_t limit)
{
    return (text != nullptr || length == 0) && length <= limit &&
           (length == 0 || (muniValidateUtf8(text, length).status == muni_success &&
                            memchr(text, '\0', length) == nullptr));
}

// Extensions without dots or wildcards, separated by ';', none empty.
static bool IsExtensions(const char* text, size_t length)
{
    if (length == 0 || !IsText(text, length, MWIN_DIALOG_FILTER_BYTES) || text[0] == ';' ||
        text[length - 1] == ';')
    {
        return false;
    }
    for (size_t i = 0; i < length; i++)
    {
        bool wild = text[i] == '.' || text[i] == '*' || text[i] == '?' || text[i] == '/' ||
                    text[i] == '\\' || (unsigned char)text[i] <= ' ';
        if (wild || (text[i] == ';' && text[i + 1] == ';'))
        {
            return false;
        }
    }
    return true;
}

static bool IsAbsolute(const char* path, size_t length)
{
#ifdef _WIN32
    bool drive = length >= 3 && path[1] == ':' && (path[2] == '\\' || path[2] == '/');
    return drive || (length >= 3 && path[0] == '\\' && path[1] == '\\');
#else
    return length >= 1 && path[0] == '/';
#endif
}

static bool IsDef(const mwinFileDialogDef* def)
{
    bool valid = def->cookie == DIALOG_COOKIE && def->kind <= mwin_dialogFolder &&
                 IsText(def->title, def->titleLength, MWIN_DIALOG_TITLE_BYTES) &&
                 IsText(def->folder, def->folderLength, MWIN_ADDRESS_BYTES) &&
                 (def->folderLength == 0 || IsAbsolute(def->folder, def->folderLength)) &&
                 IsText(def->name, def->nameLength, MWIN_ADDRESS_BYTES) &&
                 def->filterCount <= MWIN_DIALOG_FILTERS &&
                 (def->filters != nullptr || def->filterCount == 0);
    for (uint32_t i = 0; valid && i < def->filterCount; i++)
    {
        const mwinFileFilter* filter = &def->filters[i];
        valid = filter->nameLength > 0 &&
                IsText(filter->name, filter->nameLength, MWIN_DIALOG_FILTER_BYTES) &&
                IsExtensions(filter->extensions, filter->extensionsLength);
    }
    return valid;
}

// Copies text and its NUL to where at points, and moves it on.
static const char* Put(char** at, const char* text, size_t length)
{
    char* start = *at;
    if (length > 0)
    {
        memcpy(start, text, length);
    }
    start[length] = '\0';
    *at += length + 1;
    return start;
}

static mwinDialogCopy* Copy(const mwinContext* context, const mwinFileDialogDef* def)
{
    // The copy, its filters, and each text with its NUL, in checked
    // arithmetic: a size past size_t is refused as no memory.
    size_t size = 0;
    bool overflow = ckd_mul(&size, (size_t)def->filterCount, sizeof(mwinDialogFilter)) ||
                    ckd_add(&size, size, sizeof(mwinDialogCopy) + 3) ||
                    ckd_add(&size, size, def->titleLength) ||
                    ckd_add(&size, size, def->folderLength) ||
                    ckd_add(&size, size, def->nameLength);
    for (uint32_t i = 0; i < def->filterCount && !overflow; i++)
    {
        overflow = ckd_add(&size, size, def->filters[i].nameLength) ||
                   ckd_add(&size, size, def->filters[i].extensionsLength) ||
                   ckd_add(&size, size, 2);
    }
    mwinDialogCopy* copy =
        overflow ? nullptr : mwinAllocate(&context->allocator, size, alignof(mwinDialogCopy));
    if (copy == nullptr)
    {
        return nullptr;
    }
    char* at = (char*)(copy->filters + def->filterCount);
    copy->size = size;
    copy->kind = def->kind;
    copy->title = Put(&at, def->title, def->titleLength);
    copy->folder = Put(&at, def->folder, def->folderLength);
    copy->name = Put(&at, def->name, def->nameLength);
    copy->titleLength = (uint32_t)def->titleLength;
    copy->folderLength = (uint32_t)def->folderLength;
    copy->nameLength = (uint32_t)def->nameLength;
    copy->filterCount = def->kind == mwin_dialogFolder ? 0 : def->filterCount;
    for (uint32_t i = 0; i < copy->filterCount; i++)
    {
        const mwinFileFilter* filter = &def->filters[i];
        copy->filters[i].name = Put(&at, filter->name, filter->nameLength);
        copy->filters[i].extensions = Put(&at, filter->extensions, filter->extensionsLength);
    }
    return copy;
}

mwinResult mwinRequestFileDialog(mwinContext* context, mwinWindowId window,
                                 const mwinFileDialogDef* def, mwinRequestId* requestOut)
{
    if (context == nullptr || def == nullptr || !IsDef(def))
    {
        return mwinMisuse(context);
    }
    mwinDialogCopy* copy = Copy(context, def);
    if (copy == nullptr)
    {
        return mwin_errorCapacity;
    }
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestFileDialog, &slot, &request);
    if (status != mwin_success)
    {
        mwinRelease(&context->allocator, copy, copy->size, alignof(mwinDialogCopy));
        return status;
    }
    context->windows[slot].requests[request].value.dialog = copy;
    mwinSubmitRequest(context, slot, request, requestOut);
    return mwin_success;
}

void mwinReleaseDialogCopy(const mwinContext* context, mwinRequest* request)
{
    if (request->kind == mwin_requestFileDialog && request->value.dialog != nullptr)
    {
        mwinDialogCopy* copy = request->value.dialog;
        mwinRelease(&context->allocator, copy, copy->size, alignof(mwinDialogCopy));
        request->value.dialog = nullptr;
    }
}

static mwinListBounds Bounds(const mwinContext* context)
{
    return (mwinListBounds){&context->allocator, context->limits.dialogFiles,
                            context->limits.dialogBytes};
}

void mwinBeginDialog(mwinContext* context)
{
    mwinListRelease(&context->dialogGathering, &context->allocator);
    context->dialogAnswer = mwin_outcomeDone;
}

// Keeps the worst of the ways a path did not go in.
static void Note(mwinContext* context, mwinListResult result)
{
    if (result == mwin_listNotPath)
    {
        context->dialogAnswer = mwin_outcomeFailed;
    }
    else if (result == mwin_listFull && context->dialogAnswer == mwin_outcomeDone)
    {
        context->dialogAnswer = mwin_outcomeTooLarge;
    }
}

void mwinAddDialogFile(mwinContext* context, const char* path, size_t length)
{
    Note(context, mwinListAdd(&context->dialogGathering, Bounds(context), path, length));
}

void mwinAddDialogFileUtf16(mwinContext* context, const uint16_t* path, size_t length)
{
    Note(context, mwinListAddUtf16(&context->dialogGathering, Bounds(context), path, length));
}

mwinOutcome mwinSettleDialog(mwinContext* context, uint32_t slot, uint32_t request,
                             mwinOutcome outcome)
{
    if (outcome == mwin_outcomeDone)
    {
        outcome = context->dialogGathering.count > 0 ? context->dialogAnswer : mwin_outcomeFailed;
    }
    if (outcome == mwin_outcomeDone)
    {
        mwinListRelease(&context->dialogFiles, &context->allocator);
        context->dialogFiles = context->dialogGathering;
        context->dialogGathering = (mwinFileList){0};
        context->dialogRequest = mwinRequestIdOf(context, slot, request);
    }
    mwinListRelease(&context->dialogGathering, &context->allocator);
    return outcome;
}

void mwinReleaseDialogs(mwinContext* context)
{
    mwinListRelease(&context->dialogGathering, &context->allocator);
    mwinListRelease(&context->dialogFiles, &context->allocator);
}

mwinResult mwinGetDialogFiles(const mwinContext* context, mwinRequestId request, char* buffer,
                              size_t capacity, size_t* lengthOut, uint32_t* countOut)
{
    if (context == nullptr || lengthOut == nullptr || (buffer == nullptr && capacity > 0))
    {
        return mwinMisuse(context);
    }
    const mwinFileList* files = &context->dialogFiles;
    if (request.index1 == 0 || request.index1 != context->dialogRequest.index1 ||
        request.generation != context->dialogRequest.generation)
    {
        return mwin_errorStale;
    }
    if (files->length > 0 && capacity > 0)
    {
        memcpy(buffer, files->bytes, files->length < capacity ? files->length : capacity);
    }
    *lengthOut = files->length;
    if (countOut != nullptr)
    {
        *countOut = files->count;
    }
    return files->length > capacity ? mwin_errorCapacity : mwin_success;
}
