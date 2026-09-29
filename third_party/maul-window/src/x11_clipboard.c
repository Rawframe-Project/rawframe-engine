// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The X11 clipboard.

#include "x11_clipboard.h"

#include "allocator.h"
#include "monotonic.h"

#include <string.h>

// The largest piece of text sent at once; more goes in pieces (INCR).
#define PIECE_BYTES (64u * 1024u)

// How long a read, or a reader taking pieces, may take.
#define DEADLINE_NS 5000000000u

// Makes the hidden window; false when the X server refused.
static bool EnsureWindow(mwinX11Platform* platform)
{
    mwinX11Clipboard* clipboard = &platform->clipboard;
    const mwinX11Api* api = &platform->api;
    if (clipboard->window != 0)
    {
        return true;
    }
    xcb_window_t window = api->generateId(platform->connection);
    uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_generic_error_t* error = api->requestCheck(
        platform->connection,
        api->createWindowChecked(platform->connection, XCB_COPY_FROM_PARENT, window,
                                 platform->screen->root, 0, 0, 1, 1, 0, XCB_WINDOW_CLASS_INPUT_ONLY,
                                 XCB_COPY_FROM_PARENT, XCB_CW_EVENT_MASK, &mask));
    mwinReleaseSystemMemory(error);
    clipboard->window = error == nullptr ? window : 0;
    return error == nullptr;
}

// Ends a read: its text given back.
static void EndRead(mwinX11Platform* platform)
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

void mwinX11StopClipboard(mwinX11Platform* platform)
{
    EndRead(platform);
    if (platform->clipboard.window != 0)
    {
        platform->api.destroyWindow(platform->connection, platform->clipboard.window);
    }
    platform->clipboard = (mwinX11Clipboard){0};
}

int mwinX11WriteClipboard(mwinX11Platform* platform)
{
    mwinX11Clipboard* clipboard = &platform->clipboard;
    const mwinX11Api* api = &platform->api;
    if (!EnsureWindow(platform))
    {
        return mwin_outcomeFailed;
    }
    // The readers taking the last text in pieces are dropped.
    memset(clipboard->sends, 0, sizeof(clipboard->sends));
    xcb_timestamp_t time = platform->inputTime;
    xcb_atom_t selection = platform->atoms[mwin_atomClipboard];
    api->setSelectionOwner(platform->connection, clipboard->window, selection, time);
    xcb_get_selection_owner_reply_t* reply = api->getSelectionOwnerReply(
        platform->connection, api->getSelectionOwner(platform->connection, selection), nullptr);
    clipboard->owned = reply != nullptr && reply->owner == clipboard->window;
    clipboard->ownedTime = time;
    mwinReleaseSystemMemory(reply);
    return clipboard->owned ? mwin_outcomeDone : mwin_outcomeFailed;
}

int mwinX11ReadClipboard(mwinX11Platform* platform)
{
    mwinX11Clipboard* clipboard = &platform->clipboard;
    mwinContext* context = platform->context;
    if (clipboard->owned)
    {
        return mwinTakeClipboardText(context, context->clipboardOffer,
                                     context->clipboardOfferLength);
    }
    // A read under way answers this one too.
    if (clipboard->reading)
    {
        return -1;
    }
    if (!EnsureWindow(platform))
    {
        return mwin_outcomeFailed;
    }
    platform->api.convertSelection(platform->connection, clipboard->window,
                                   platform->atoms[mwin_atomClipboard],
                                   platform->atoms[mwin_atomUtf8String],
                                   platform->atoms[mwin_atomSelection], platform->inputTime);
    clipboard->reading = true;
    clipboard->deadlineNs = mwinMonotonicNow() + DEADLINE_NS;
    return -1;
}

// Answers the read requests of every window, and ends the read.
static void Finish(mwinX11Platform* platform, mwinOutcome outcome)
{
    mwinContext* context = platform->context;
    EndRead(platform);
    for (uint32_t slot = 0; slot < context->limits.windows; slot++)
    {
        const mwinWindow* window = &context->windows[slot];
        int32_t request = window->status == mwin_slotLive
                              ? mwinFindActiveRequest(window, context->limits.requestsPerWindow,
                                                      mwin_requestClipboardRead)
                              : -1;
        if (request >= 0)
        {
            mwinComplete(context, slot, (uint32_t)request, outcome);
        }
    }
}

// Adds bytes to the read's text: false, with the outcome, when they
// pass the limit or the allocator has no room.
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
        uint32_t capacity = needed > clipboard->capacity * 2 ? needed : clipboard->capacity * 2;
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
// NULL. Asking for one word past the limit tells text too large.
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

// The owner's answer, or a piece of its text: takes the property.
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
        // The whole text, or the empty piece that ends the pieces.
        Finish(platform,
               mwinTakeClipboardText(platform->context, clipboard->buffer, clipboard->used));
    }
    mwinReleaseSystemMemory(reply);
}

// Sends the selection notification that answers a request.
static void Notify(const mwinX11Platform* platform, const xcb_selection_request_event_t* request,
                   xcb_atom_t property)
{
    union
    {
        xcb_selection_notify_event_t notify;
        char bytes[32];
    } event = {0};
    event.notify.response_type = XCB_SELECTION_NOTIFY;
    event.notify.time = request->time;
    event.notify.requestor = request->requestor;
    event.notify.selection = request->selection;
    event.notify.target = request->target;
    event.notify.property = property;
    platform->api.sendEvent(platform->connection, 0, request->requestor, XCB_EVENT_MASK_NO_EVENT,
                            event.bytes);
}

// Starts sending the text in pieces: false when every place is taken.
static bool SendInPieces(mwinX11Platform* platform, const xcb_selection_request_event_t* request,
                         xcb_atom_t property)
{
    mwinX11Clipboard* clipboard = &platform->clipboard;
    const mwinX11Api* api = &platform->api;
    for (int i = 0; i < MWIN_X11_SENDS; i++)
    {
        if (clipboard->sends[i].requestor != 0)
        {
            continue;
        }
        // The reader's deletions of the property ask for the pieces.
        uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
        api->changeWindowAttributes(platform->connection, request->requestor, XCB_CW_EVENT_MASK,
                                    &mask);
        uint32_t length = platform->context->clipboardOfferLength;
        api->changeProperty(platform->connection, XCB_PROP_MODE_REPLACE, request->requestor,
                            property, platform->atoms[mwin_atomIncr], 32, 1, &length);
        clipboard->sends[i].requestor = request->requestor;
        clipboard->sends[i].property = property;
        clipboard->sends[i].type = request->target;
        clipboard->sends[i].offset = 0;
        clipboard->sends[i].deadlineNs = mwinMonotonicNow() + DEADLINE_NS;
        return true;
    }
    return false;
}

// Answers another client's request for the selection: the property it
// was given, or none for a refusal.
static xcb_atom_t Serve(mwinX11Platform* platform, const xcb_selection_request_event_t* request)
{
    const mwinX11Api* api = &platform->api;
    const xcb_atom_t* atoms = platform->atoms;
    const mwinContext* context = platform->context;
    // A client older than ICCCM 2 names no property: the target serves.
    xcb_atom_t property = request->property != XCB_ATOM_NONE ? request->property : request->target;
    xcb_atom_t target = request->target;
    bool text = target == atoms[mwin_atomUtf8String] || target == atoms[mwin_atomTextPlainUtf8];
    if (!platform->clipboard.owned || request->selection != atoms[mwin_atomClipboard])
    {
        return XCB_ATOM_NONE;
    }
    if (target == atoms[mwin_atomTargets])
    {
        xcb_atom_t targets[] = {atoms[mwin_atomTargets], atoms[mwin_atomTimestamp],
                                atoms[mwin_atomUtf8String], atoms[mwin_atomTextPlainUtf8]};
        api->changeProperty(platform->connection, XCB_PROP_MODE_REPLACE, request->requestor,
                            property, XCB_ATOM_ATOM, 32, 4, targets);
        return property;
    }
    if (target == atoms[mwin_atomTimestamp])
    {
        api->changeProperty(platform->connection, XCB_PROP_MODE_REPLACE, request->requestor,
                            property, XCB_ATOM_INTEGER, 32, 1, &platform->clipboard.ownedTime);
        return property;
    }
    if (text && context->clipboardOfferLength > PIECE_BYTES)
    {
        return SendInPieces(platform, request, property) ? property : XCB_ATOM_NONE;
    }
    if (text)
    {
        api->changeProperty(platform->connection, XCB_PROP_MODE_REPLACE, request->requestor,
                            property, target, 8, context->clipboardOfferLength,
                            context->clipboardOffer);
        return property;
    }
    return XCB_ATOM_NONE;
}

// A reader took the last piece: sends the next, or the empty one that
// ends them.
static bool SendPiece(mwinX11Platform* platform, const xcb_property_notify_event_t* event)
{
    mwinX11Clipboard* clipboard = &platform->clipboard;
    const mwinContext* context = platform->context;
    for (int i = 0; i < MWIN_X11_SENDS; i++)
    {
        if (clipboard->sends[i].requestor != event->window ||
            clipboard->sends[i].property != event->atom || event->state != XCB_PROPERTY_DELETE)
        {
            continue;
        }
        uint32_t offset = clipboard->sends[i].offset;
        uint32_t left = context->clipboardOfferLength - offset;
        uint32_t length = left < PIECE_BYTES ? left : PIECE_BYTES;
        platform->api.changeProperty(platform->connection, XCB_PROP_MODE_REPLACE, event->window,
                                     event->atom, clipboard->sends[i].type, 8, length,
                                     context->clipboardOffer + offset);
        clipboard->sends[i].offset += length;
        clipboard->sends[i].deadlineNs = mwinMonotonicNow() + DEADLINE_NS;
        if (length == 0)
        {
            clipboard->sends[i].requestor = 0;
        }
        return true;
    }
    return false;
}

static bool OnProperty(mwinX11Platform* platform, const xcb_property_notify_event_t* event)
{
    mwinX11Clipboard* clipboard = &platform->clipboard;
    if (event->window == clipboard->window && event->atom == platform->atoms[mwin_atomSelection])
    {
        if (clipboard->incremental && event->state == XCB_PROPERTY_NEW_VALUE)
        {
            TakePiece(platform, false);
        }
        return true;
    }
    return SendPiece(platform, event);
}

// The owner's answer to a read.
static void OnNotify(mwinX11Platform* platform, const xcb_selection_notify_event_t* notify)
{
    const mwinX11Clipboard* clipboard = &platform->clipboard;
    if (!clipboard->reading || clipboard->incremental)
    {
        return;
    }
    // No owner, or one without UTF-8 text: empty text.
    if (notify->property == XCB_ATOM_NONE)
    {
        Finish(platform, mwinTakeClipboardText(platform->context, nullptr, 0));
    }
    else
    {
        TakePiece(platform, true);
    }
}

bool mwinX11HandleClipboardEvent(mwinX11Platform* platform, const xcb_generic_event_t* event)
{
    mwinX11Clipboard* clipboard = &platform->clipboard;
    uint8_t type = event->response_type & 0x7F;
    xcb_window_t window = clipboard->window;
    // Only the hidden window's selection events are the clipboard's.
    if (window == 0)
    {
        return false;
    }
    if (type == XCB_SELECTION_REQUEST &&
        ((const xcb_selection_request_event_t*)event)->owner == window)
    {
        const xcb_selection_request_event_t* request = (const xcb_selection_request_event_t*)event;
        Notify(platform, request, Serve(platform, request));
        return true;
    }
    if (type == XCB_SELECTION_CLEAR && ((const xcb_selection_clear_event_t*)event)->owner == window)
    {
        clipboard->owned = false;
        return true;
    }
    if (type == XCB_SELECTION_NOTIFY &&
        ((const xcb_selection_notify_event_t*)event)->requestor == window)
    {
        OnNotify(platform, (const xcb_selection_notify_event_t*)event);
        return true;
    }
    return type == XCB_PROPERTY_NOTIFY &&
           OnProperty(platform, (const xcb_property_notify_event_t*)event);
}

void mwinX11CheckClipboard(mwinX11Platform* platform, uint64_t nowNs)
{
    mwinX11Clipboard* clipboard = &platform->clipboard;
    if (clipboard->reading && nowNs >= clipboard->deadlineNs)
    {
        Finish(platform, mwin_outcomeFailed);
    }
    for (int i = 0; i < MWIN_X11_SENDS; i++)
    {
        if (clipboard->sends[i].requestor != 0 && nowNs >= clipboard->sends[i].deadlineNs)
        {
            clipboard->sends[i].requestor = 0;
        }
    }
}
