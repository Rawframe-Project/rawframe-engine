// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The X11 clipboard and primary selection as their owner.

#include "x11_clipboard.h"

#include "allocator.h"
#include "clipboard_data.h"
#include "monotonic.h"

#include <string.h>

// The largest piece sent at once; more goes in pieces (INCR).
#define PIECE_BYTES (64u * 1024u)

// How long a reader taking pieces may take over each.
#define DEADLINE_NS 5000000000u

// The most targets a selection offers: TARGETS, TIMESTAMP, the two text
// targets and the data's types.
#define MAX_TARGETS (4 + MWIN_CLIPBOARD_ITEMS)

bool mwinX11EnsureClipboardWindow(mwinX11Platform* platform)
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

xcb_atom_t mwinX11SelectionAtom(const mwinX11Platform* platform, int selection)
{
    return selection == mwin_x11Primary ? XCB_ATOM_PRIMARY : platform->atoms[mwin_atomClipboard];
}

void mwinX11StopClipboard(mwinX11Platform* platform)
{
    mwinX11EndRead(platform);
    if (platform->clipboard.window != 0)
    {
        platform->api.destroyWindow(platform->connection, platform->clipboard.window);
    }
    platform->clipboard = (mwinX11Clipboard){0};
}

// Interns the atoms of the data written's types, sent together before
// any reply; false when the X server answered none for one.
static bool InternTypes(mwinX11Platform* platform)
{
    const mwinX11Api* api = &platform->api;
    const mwinClipboardCopy* copy = platform->context->clipboardData;
    uint32_t count = copy != nullptr ? copy->count : 0;
    xcb_intern_atom_cookie_t cookies[MWIN_CLIPBOARD_ITEMS];
    memset(platform->clipboard.types, 0, sizeof(platform->clipboard.types));
    for (uint32_t i = 0; i < count; i++)
    {
        cookies[i] = api->internAtom(platform->connection, 0, (uint16_t)copy->items[i].mimeLength,
                                     copy->items[i].mime);
    }
    bool interned = true;
    for (uint32_t i = 0; i < count; i++)
    {
        xcb_intern_atom_reply_t* reply =
            api->internAtomReply(platform->connection, cookies[i], nullptr);
        platform->clipboard.types[i] = reply != nullptr ? reply->atom : XCB_ATOM_NONE;
        interned = interned && reply != nullptr;
        mwinReleaseSystemMemory(reply);
    }
    return interned;
}

int mwinX11WriteSelection(mwinX11Platform* platform, mwinRequestKind kind)
{
    mwinX11Clipboard* clipboard = &platform->clipboard;
    const mwinX11Api* api = &platform->api;
    int selection = kind == mwin_requestPrimaryWrite ? mwin_x11Primary : mwin_x11Clipboard;
    if (!mwinX11EnsureClipboardWindow(platform))
    {
        return mwin_outcomeFailed;
    }
    // The readers taking the last text or data in pieces are dropped.
    for (int i = 0; i < MWIN_X11_SENDS; i++)
    {
        clipboard->sends[i].requestor =
            clipboard->sends[i].selection == selection ? 0 : clipboard->sends[i].requestor;
    }
    if (selection == mwin_x11Clipboard && !InternTypes(platform))
    {
        clipboard->owned[selection] = false;
        return mwin_outcomeFailed;
    }
    xcb_timestamp_t time = platform->inputTime;
    xcb_atom_t atom = mwinX11SelectionAtom(platform, selection);
    api->setSelectionOwner(platform->connection, clipboard->window, atom, time);
    xcb_get_selection_owner_reply_t* reply = api->getSelectionOwnerReply(
        platform->connection, api->getSelectionOwner(platform->connection, atom), nullptr);
    clipboard->owned[selection] = reply != nullptr && reply->owner == clipboard->window;
    clipboard->ownedTime[selection] = time;
    mwinReleaseSystemMemory(reply);
    return clipboard->owned[selection] ? mwin_outcomeDone : mwin_outcomeFailed;
}

// The selection a request names when the program owns it, -1 else.
static int OwnedSelection(const mwinX11Platform* platform, xcb_atom_t atom)
{
    for (int selection = 0; selection < MWIN_X11_SELECTIONS; selection++)
    {
        if (platform->clipboard.owned[selection] &&
            mwinX11SelectionAtom(platform, selection) == atom)
        {
            return selection;
        }
    }
    return -1;
}

// Whether a selection offers text: the primary selection always.
static bool OffersText(const mwinContext* context, int selection)
{
    return selection == mwin_x11Primary || mwinOffersClipboardText(context);
}

// The bytes a selection serves: its text (item -1) or an item of the
// clipboard's data.
static const char* SourceOf(const mwinContext* context, int selection, int item,
                            uint32_t* lengthOut)
{
    if (selection == mwin_x11Primary)
    {
        *lengthOut = context->primaryOfferLength;
        return context->primaryOffer;
    }
    if (item < 0)
    {
        *lengthOut = context->clipboardOfferLength;
        return context->clipboardOffer;
    }
    const mwinClipboardCopy* copy = context->clipboardData;
    *lengthOut = copy->items[item].length;
    return (const char*)mwinClipboardBytesOf(copy, &copy->items[item]);
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

// Starts sending bytes in pieces: false when every place is taken.
static bool SendInPieces(mwinX11Platform* platform, const xcb_selection_request_event_t* request,
                         xcb_atom_t property, int selection, int item, uint32_t length)
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
        api->changeProperty(platform->connection, XCB_PROP_MODE_REPLACE, request->requestor,
                            property, platform->atoms[mwin_atomIncr], 32, 1, &length);
        clipboard->sends[i].requestor = request->requestor;
        clipboard->sends[i].property = property;
        clipboard->sends[i].type = request->target;
        clipboard->sends[i].selection = (uint8_t)selection;
        clipboard->sends[i].item = (int8_t)item;
        clipboard->sends[i].offset = 0;
        clipboard->sends[i].deadlineNs = mwinMonotonicNow() + DEADLINE_NS;
        return true;
    }
    return false;
}

// Puts a selection's targets on the requestor's property.
static void ServeTargets(const mwinX11Platform* platform,
                         const xcb_selection_request_event_t* request, xcb_atom_t property,
                         int selection)
{
    const xcb_atom_t* atoms = platform->atoms;
    const mwinClipboardCopy* copy = platform->context->clipboardData;
    xcb_atom_t targets[MAX_TARGETS] = {atoms[mwin_atomTargets], atoms[mwin_atomTimestamp]};
    uint32_t count = 2;
    if (OffersText(platform->context, selection))
    {
        targets[count++] = atoms[mwin_atomUtf8String];
        targets[count++] = atoms[mwin_atomTextPlainUtf8];
    }
    for (uint32_t i = 0; selection == mwin_x11Clipboard && copy != nullptr && i < copy->count; i++)
    {
        targets[count++] = platform->clipboard.types[i];
    }
    platform->api.changeProperty(platform->connection, XCB_PROP_MODE_REPLACE, request->requestor,
                                 property, XCB_ATOM_ATOM, 32, count, targets);
}

// The item of the clipboard's data a target asks for, -2 for none; -1
// is the text.
static int ItemOf(const mwinX11Platform* platform, xcb_atom_t target, int selection)
{
    const xcb_atom_t* atoms = platform->atoms;
    if (target == atoms[mwin_atomUtf8String] || target == atoms[mwin_atomTextPlainUtf8])
    {
        return OffersText(platform->context, selection) ? -1 : -2;
    }
    const mwinClipboardCopy* copy = platform->context->clipboardData;
    for (uint32_t i = 0; selection == mwin_x11Clipboard && copy != nullptr && i < copy->count; i++)
    {
        if (platform->clipboard.types[i] == target)
        {
            return (int)i;
        }
    }
    return -2;
}

// Answers another client's request for a selection: the property it was
// given, or none for a refusal.
static xcb_atom_t Serve(mwinX11Platform* platform, const xcb_selection_request_event_t* request)
{
    const mwinX11Api* api = &platform->api;
    const xcb_atom_t* atoms = platform->atoms;
    // A client older than ICCCM 2 names no property: the target serves.
    xcb_atom_t property = request->property != XCB_ATOM_NONE ? request->property : request->target;
    xcb_atom_t target = request->target;
    int selection = OwnedSelection(platform, request->selection);
    if (selection < 0)
    {
        return XCB_ATOM_NONE;
    }
    if (target == atoms[mwin_atomTargets])
    {
        ServeTargets(platform, request, property, selection);
        return property;
    }
    if (target == atoms[mwin_atomTimestamp])
    {
        api->changeProperty(platform->connection, XCB_PROP_MODE_REPLACE, request->requestor,
                            property, XCB_ATOM_INTEGER, 32, 1,
                            &platform->clipboard.ownedTime[selection]);
        return property;
    }
    int item = ItemOf(platform, target, selection);
    if (item < -1)
    {
        return XCB_ATOM_NONE;
    }
    uint32_t length = 0;
    const char* bytes = SourceOf(platform->context, selection, item, &length);
    if (length > PIECE_BYTES)
    {
        return SendInPieces(platform, request, property, selection, item, length) ? property
                                                                                  : XCB_ATOM_NONE;
    }
    api->changeProperty(platform->connection, XCB_PROP_MODE_REPLACE, request->requestor, property,
                        target, 8, length, bytes);
    return property;
}

// A reader took the last piece: sends the next, or the empty one that
// ends them.
static bool SendPiece(mwinX11Platform* platform, const xcb_property_notify_event_t* event)
{
    mwinX11Clipboard* clipboard = &platform->clipboard;
    for (int i = 0; i < MWIN_X11_SENDS; i++)
    {
        if (clipboard->sends[i].requestor != event->window ||
            clipboard->sends[i].property != event->atom || event->state != XCB_PROPERTY_DELETE)
        {
            continue;
        }
        uint32_t total = 0;
        const char* bytes = SourceOf(platform->context, clipboard->sends[i].selection,
                                     clipboard->sends[i].item, &total);
        uint32_t offset = clipboard->sends[i].offset;
        uint32_t left = total - offset;
        uint32_t length = left < PIECE_BYTES ? left : PIECE_BYTES;
        platform->api.changeProperty(platform->connection, XCB_PROP_MODE_REPLACE, event->window,
                                     event->atom, clipboard->sends[i].type, 8, length,
                                     bytes + offset);
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
        int selection =
            OwnedSelection(platform, ((const xcb_selection_clear_event_t*)event)->selection);
        if (selection >= 0)
        {
            clipboard->owned[selection] = false;
        }
        return true;
    }
    if (type == XCB_SELECTION_NOTIFY &&
        ((const xcb_selection_notify_event_t*)event)->requestor == window)
    {
        mwinX11OnReadNotify(platform, (const xcb_selection_notify_event_t*)event);
        return true;
    }
    if (type != XCB_PROPERTY_NOTIFY)
    {
        return false;
    }
    const xcb_property_notify_event_t* property = (const xcb_property_notify_event_t*)event;
    if (property->window == window && property->atom == platform->atoms[mwin_atomSelection])
    {
        mwinX11OnReadProperty(platform, property);
        return true;
    }
    return SendPiece(platform, property);
}

void mwinX11CheckClipboard(mwinX11Platform* platform, uint64_t nowNs)
{
    mwinX11Clipboard* clipboard = &platform->clipboard;
    mwinX11CheckRead(platform, nowNs);
    for (int i = 0; i < MWIN_X11_SENDS; i++)
    {
        if (clipboard->sends[i].requestor != 0 && nowNs >= clipboard->sends[i].deadlineNs)
        {
            clipboard->sends[i].requestor = 0;
        }
    }
}
