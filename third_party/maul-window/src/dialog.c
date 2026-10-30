// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// File dialogs: the def's copy and the paths chosen.

#include "dialog.h"

#include "allocator.h"
#include "utf8.h"

#include "maul-window/services.h"

#include <stdalign.h>
#include <stdckdint.h>
#include <string.h>

#define DIALOG_COOKIE 0x6D776664u // "mwfd"

mwinFileDialogDef mwinDefaultFileDialogDef(void)
{
    return (mwinFileDialogDef){.cookie = DIALOG_COOKIE, .kind = mwin_dialogOpen};
}

static mwinResult CheckText(const char* text, size_t length, size_t limit)
{
    return mwinCheckText(text, length, limit, mwin_errorLimit, false);
}

// Extensions without dots or wildcards, separated by ';', none empty.
static mwinResult CheckExtensions(const char* text, size_t length)
{
    mwinResult status = CheckText(text, length, MWIN_MAX_DIALOG_FILTER_BYTES);
    if (status != mwin_success)
    {
        return status;
    }
    if (length == 0 || text[0] == ';' || text[length - 1] == ';')
    {
        return mwin_errorInvalid;
    }
    for (size_t i = 0; i < length; i++)
    {
        bool wild = text[i] == '.' || text[i] == '*' || text[i] == '?' || text[i] == '/' ||
                    text[i] == '\\' || (unsigned char)text[i] <= ' ';
        if (wild || (text[i] == ';' && text[i + 1] == ';'))
        {
            return mwin_errorInvalid;
        }
    }
    return mwin_success;
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

// The def's status: mwin_errorLimit for text or filters past their
// named bounds, mwin_errorInvalid for anything else wrong.
static mwinResult CheckDef(const mwinFileDialogDef* def)
{
    if (def->cookie != DIALOG_COOKIE || def->kind > mwin_dialogFolder ||
        (def->filters == nullptr && def->filterCount != 0))
    {
        return mwin_errorInvalid;
    }
    if (def->filterCount > MWIN_MAX_DIALOG_FILTERS)
    {
        return mwin_errorLimit;
    }
    mwinResult status = CheckText(def->title, def->titleLength, MWIN_MAX_DIALOG_TITLE_BYTES);
    if (status == mwin_success)
    {
        status = CheckText(def->folder, def->folderLength, MWIN_MAX_ADDRESS_BYTES);
    }
    if (status == mwin_success && def->folderLength != 0 &&
        !IsAbsolute(def->folder, def->folderLength))
    {
        status = mwin_errorInvalid;
    }
    if (status == mwin_success)
    {
        status = CheckText(def->name, def->nameLength, MWIN_MAX_ADDRESS_BYTES);
    }
    for (uint32_t i = 0; status == mwin_success && i < def->filterCount; i++)
    {
        const mwinFileFilter* filter = &def->filters[i];
        status = filter->nameLength == 0
                     ? mwin_errorInvalid
                     : CheckText(filter->name, filter->nameLength, MWIN_MAX_DIALOG_FILTER_BYTES);
        if (status == mwin_success)
        {
            status = CheckExtensions(filter->extensions, filter->extensionsLength);
        }
    }
    return status;
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

mwinResult mwinWindow_RequestFileDialog(mwinContext* context, mwinWindowId window,
                                        const mwinFileDialogDef* def, mwinRequestId* requestOut)
{
    mwinClearOut(requestOut, sizeof *requestOut);
    if (mwinIsBusy(context))
    {
        return mwinBusy(context);
    }
    if (context == nullptr || def == nullptr)
    {
        return mwinMisuse(context);
    }
    mwinResult status = CheckDef(def);
    if (status != mwin_success)
    {
        return mwinRefuse(context, status);
    }
    mwinDialogCopy* copy = Copy(context, def);
    if (copy == nullptr)
    {
        return mwin_errorCapacity;
    }
    uint32_t slot = 0;
    int32_t request = 0;
    status = mwinBeginRequest(context, window, mwin_requestFileDialog, &slot, &request);
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

mwinResult mwinRequest_GetDialogFiles(const mwinContext* context, mwinRequestId request,
                                      char* bufferOut, size_t capacity, size_t* lengthOut,
                                      uint32_t* countOut)
{
    mwinClearOut(bufferOut, capacity);
    mwinClearOut(lengthOut, sizeof *lengthOut);
    mwinClearOut(countOut, sizeof *countOut);
    if (context == nullptr || lengthOut == nullptr || (bufferOut == nullptr && capacity > 0))
    {
        return mwinMisuse(context);
    }
    const mwinFileList* files = &context->dialogFiles;
    if (request.index1 == 0 || request.index1 != context->dialogRequest.index1 ||
        request.generation != context->dialogRequest.generation)
    {
        return mwin_errorStale;
    }
    if (countOut != nullptr)
    {
        *countOut = files->count;
    }
    return mwinGiveBytes(files->bytes, files->length, bufferOut, capacity, lengthOut);
}
