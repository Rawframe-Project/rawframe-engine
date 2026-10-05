// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The platform services' requests.

#include "maul-window/services.h"

#include "allocator.h"
#include "core.h"

#include "maul-unicode/encoding.h"

#include <string.h>

static bool IsText(const char* text, size_t length)
{
    return (text != nullptr || length == 0) && length <= MWIN_ADDRESS_BYTES &&
           muniValidateUtf8(text, length).status == muni_success &&
           (length == 0 || memchr(text, '\0', length) == nullptr);
}

// Whether text begins with a prefix, in any case.
static bool Begins(const char* text, size_t length, const char* prefix)
{
    size_t size = strlen(prefix);
    if (length < size)
    {
        return false;
    }
    for (size_t i = 0; i < size; i++)
    {
        char c = text[i] >= 'A' && text[i] <= 'Z' ? (char)(text[i] - 'A' + 'a') : text[i];
        if (c != prefix[i])
        {
            return false;
        }
    }
    return true;
}

// An address this opens: web or mail, something after the scheme, and
// no space or control character another program could split it at.
static bool IsAddress(const char* url, size_t length)
{
    if (!IsText(url, length))
    {
        return false;
    }
    size_t scheme = Begins(url, length, "https://")  ? 8
                    : Begins(url, length, "http://") ? 7
                    : Begins(url, length, "mailto:") ? 7
                                                     : 0;
    if (scheme == 0 || length == scheme)
    {
        return false;
    }
    for (size_t i = 0; i < length; i++)
    {
        unsigned char c = (unsigned char)url[i];
        if (c <= ' ' || c == 0x7F)
        {
            return false;
        }
    }
    return true;
}

// An absolute path on the platform built for.
static bool IsAbsolute(const char* path, size_t length)
{
#ifdef _WIN32
    bool drive = length >= 3 &&
                 ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
                 path[1] == ':' && (path[2] == '\\' || path[2] == '/');
    bool share = length >= 3 && path[0] == '\\' && path[1] == '\\';
    return drive || share;
#else
    return length >= 1 && path[0] == '/';
#endif
}

// A request carrying a copy of text.
static mwinResult RequestText(mwinContext* context, mwinWindowId window, mwinRequestKind kind,
                              const char* text, size_t length, mwinRequestId* requestOut)
{
    char* copy = mwinAllocate(&context->allocator, length + 1, 1);
    if (copy == nullptr)
    {
        return mwin_errorCapacity;
    }
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, kind, &slot, &request);
    if (status != mwin_success)
    {
        mwinRelease(&context->allocator, copy, length + 1, 1);
        return status;
    }
    memcpy(copy, text, length);
    copy[length] = '\0';
    mwinRequest* entry = &context->windows[slot].requests[request];
    entry->value.text.bytes = copy;
    entry->value.text.length = (uint32_t)length;
    mwinSubmitRequest(context, slot, request, requestOut);
    return mwin_success;
}

void mwinReleaseRequestText(const mwinContext* context, mwinRequest* request)
{
    bool text = request->kind == mwin_requestOpenUrl || request->kind == mwin_requestRevealFile;
    if (text && request->value.text.bytes != nullptr)
    {
        mwinRelease(&context->allocator, request->value.text.bytes, request->value.text.length + 1u,
                    1);
        request->value.text.bytes = nullptr;
    }
}

bool mwinWantsAwake(const mwinContext* context)
{
    for (uint32_t i = 0; i < context->limits.windows; i++)
    {
        const mwinWindow* window = &context->windows[i];
        if (window->status == mwin_slotLive && window->state.awake && window->state.visible &&
            window->state.mode != mwin_modeMinimized)
        {
            return true;
        }
    }
    return false;
}

mwinResult mwinRequestOpenUrl(mwinContext* context, mwinWindowId window, const char* url,
                              size_t length, mwinRequestId* requestOut)
{
    if (context == nullptr || !IsAddress(url, length))
    {
        return mwinMisuse(context);
    }
    return RequestText(context, window, mwin_requestOpenUrl, url, length, requestOut);
}

mwinResult mwinRequestRevealFile(mwinContext* context, mwinWindowId window, const char* path,
                                 size_t length, mwinRequestId* requestOut)
{
    if (context == nullptr || !IsText(path, length) || !IsAbsolute(path, length))
    {
        return mwinMisuse(context);
    }
    return RequestText(context, window, mwin_requestRevealFile, path, length, requestOut);
}

mwinResult mwinRequestKeepAwake(mwinContext* context, mwinWindowId window, bool awake,
                                mwinRequestId* requestOut)
{
    uint32_t slot = 0;
    int32_t request = 0;
    mwinResult status = mwinBeginRequest(context, window, mwin_requestKeepAwake, &slot, &request);
    if (status == mwin_success)
    {
        context->windows[slot].requests[request].value.awake = awake;
        mwinSubmitRequest(context, slot, request, requestOut);
    }
    return status;
}
