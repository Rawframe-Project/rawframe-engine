// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The X11 clipboard's and primary selection's reads.

#include "allocator.h"
#include "monotonic.h"
#include "x11_clipboard.h"

#include <stdckdint.h>
#include <string.h>

// How long a read may take.
#define DEADLINE_NS 5000000000u

void mwinX11EndRead(mwinX11Platform* platform)
{
    mwinX11Clipboard* clipboard = &platform->clipboard;
    if (clipboard->buffer != nullptr)
    {
        mwinRelease(&platform->context->allocator, clipboard->buffer, clipboard->capacity, 1);
    }
    clipboard->buffer = nullptr;
    clipboard->used = 0;
    clipboard->capacity = 0;
    clipboard->reading = false;
    clipboard->incremental = false;
}

// The atom of a MIME type when the X server has one, NONE else: no
// client offers a type no one interned.
static xcb_atom_t Existing(const mwinX11Platform* platform, const char* name, uint32_t length)
{
    const mwinX11Api* api = &platform->api;
    xcb_intern_atom_reply_t* reply = api->internAtomReply(
        platform->connection, api->internAtom(platform->connection, 1, (uint16_t)length, name),
        nullptr);
    xcb_atom_t atom = reply != nullptr ? reply->atom : XCB_ATOM_NONE;
    mwinReleaseSystemMemory(reply);
    return atom;
}

// Starts a read for a request, or answers it: an outcome, or -1 while a
// read, this one's or another's, is under way.
static int Start(mwinX11Platform* platform, const mwinRequest* request)
{
    mwinX11Clipboard* clipboard = &platform->clipboard;
    int selection = mwinReadsPrimary(request) ? mwin_x11Primary : mwin_x11Clipboard;
    if (clipboard->owned[selection])
    {
        return mwinAnswerOwnRead(platform->context, request);
    }
    if (clipboard->reading)
    {
        return -1;
    }
    if (!mwinX11EnsureClipboardWindow(platform))
    {
        return mwin_outcomeFailed;
    }
    xcb_atom_t target = platform->atoms[mwin_atomUtf8String];
    if (request->kind == mwin_requestClipboardReadData)
    {
        target = Existing(platform, request->value.text.bytes, request->value.text.length);
        if (target == XCB_ATOM_NONE)
        {
            return mwin_outcomeFailed;
        }
    }
    platform->api.convertSelection(platform->connection, clipboard->window,
                                   mwinX11SelectionAtom(platform, selection), target,
                                   platform->atoms[mwin_atomSelection], platform->inputTime);
    clipboard->reading = true;
    mwinBeginSelectionRead(&clipboard->read, request);
    clipboard->deadlineNs = mwinMonotonicNow() + DEADLINE_NS;
    return -1;
}

static int StartFor(void* platform, const mwinRequest* request)
{
    return Start(platform, request);
}

int mwinX11ReadSelection(mwinX11Platform* platform, const mwinRequest* request)
{
    return Start(platform, request);
}

// Answers the requests the read answers, ends the read, and starts the
// next.
static void Finish(mwinX11Platform* platform, mwinOutcome outcome)
{
    mwinFinishSelectionRead(platform->context, &platform->clipboard.read, outcome);
    mwinX11EndRead(platform);
    mwinStartWaitingReads(platform->context, StartFor, platform);
}

// Adds bytes to the read's: false, with the outcome, when they pass the
// limit or the allocator has no room.
static bool Append(mwinX11Platform* platform, const char* bytes, uint32_t length,
                   mwinOutcome* outcomeOut)
{
    mwinX11Clipboard* clipboard = &platform->clipboard;
    const mwinContext* context = platform->context;
    if (length > context->limits.clipboardBytes - clipboard->used)
    {
        *outcomeOut = mwin_outcomeTooLarge;
        return false;
    }
    uint32_t needed = clipboard->used + length;
    if (needed > clipboard->capacity)
    {
        // Doubling saturates, then the limit caps it.
        uint32_t doubled = 0;
        if (ckd_mul(&doubled, clipboard->capacity, 2u))
        {
            doubled = UINT32_MAX;
        }
        uint32_t capacity = needed > doubled ? needed : doubled;
        capacity =
            capacity < context->limits.clipboardBytes ? capacity : context->limits.clipboardBytes;
        char* grown = mwinAllocate(&context->allocator, capacity, 1);
        if (grown == nullptr)
        {
            *outcomeOut = mwin_outcomeFailed;
            return false;
        }
        if (clipboard->buffer != nullptr)
        {
            memcpy(grown, clipboard->buffer, clipboard->used);
            mwinRelease(&context->allocator, clipboard->buffer, clipboard->capacity, 1);
        }
        clipboard->buffer = grown;
        clipboard->capacity = capacity;
    }
    if (length > 0)
    {
        memcpy(clipboard->buffer + clipboard->used, bytes, length);
    }
    clipboard->used = needed;
    return true;
}

// Takes the selection property off the hidden window: the reply, or
// NULL. Asking for one word past the limit tells bytes too many.
static xcb_get_property_reply_t* TakeProperty(const mwinX11Platform* platform)
{
    const mwinX11Api* api = &platform->api;
    const mwinX11Clipboard* clipboard = &platform->clipboard;
    uint32_t room = platform->context->limits.clipboardBytes - clipboard->used;
    return api->getPropertyReply(platform->connection,
                                 api->getProperty(platform->connection, 1, clipboard->window,
                                                  platform->atoms[mwin_atomSelection],
                                                  XCB_GET_PROPERTY_TYPE_ANY, 0, room / 4 + 1),
                                 nullptr);
}

// The owner's answer, or a piece of its bytes: takes the property.
static void TakePiece(mwinX11Platform* platform, bool answer)
{
    mwinX11Clipboard* clipboard = &platform->clipboard;
    const mwinX11Api* api = &platform->api;
    xcb_get_property_reply_t* reply = TakeProperty(platform);
    if (reply == nullptr)
    {
        Finish(platform, mwin_outcomeFailed);
        return;
    }
    // The length is in bytes, whatever the format.
    uint32_t length = (uint32_t)api->getPropertyValueLength(reply);
    const char* bytes = api->getPropertyValue(reply);
    mwinOutcome outcome = mwin_outcomeDone;
    if (answer && reply->type == platform->atoms[mwin_atomIncr])
    {
        // Deleting the property asked for the first piece.
        clipboard->incremental = true;
        clipboard->deadlineNs = mwinMonotonicNow() + DEADLINE_NS;
    }
    else if (reply->bytes_after > 0 || !Append(platform, bytes, length, &outcome))
    {
        Finish(platform, reply->bytes_after > 0 ? mwin_outcomeTooLarge : outcome);
    }
    else if (!clipboard->incremental || length == 0)
    {
        // The whole of it, or the empty piece that ends the pieces.
        Finish(platform, mwinTakeSelectionRead(platform->context, &clipboard->read,
                                               clipboard->buffer, clipboard->used));
    }
    mwinReleaseSystemMemory(reply);
}

void mwinX11OnReadNotify(mwinX11Platform* platform, const xcb_selection_notify_event_t* notify)
{
    const mwinX11Clipboard* clipboard = &platform->clipboard;
    if (!clipboard->reading || clipboard->incremental)
    {
        return;
    }
    // No owner, or one without the target: empty text, or no data.
    if (notify->property == XCB_ATOM_NONE)
    {
        Finish(platform, mwinMissedSelectionRead(platform->context, &clipboard->read));
    }
    else
    {
        TakePiece(platform, true);
    }
}

void mwinX11OnReadProperty(mwinX11Platform* platform, const xcb_property_notify_event_t* event)
{
    if (platform->clipboard.incremental && event->state == XCB_PROPERTY_NEW_VALUE)
    {
        TakePiece(platform, false);
    }
}

void mwinX11CheckRead(mwinX11Platform* platform, uint64_t nowNs)
{
    if (platform->clipboard.reading && nowNs >= platform->clipboard.deadlineNs)
    {
        Finish(platform, mwin_outcomeFailed);
    }
}
