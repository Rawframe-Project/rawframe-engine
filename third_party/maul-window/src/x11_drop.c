// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// X11 drag and drop by XDND 5.

#include "x11_drop.h"

#include "allocator.h"
#include "monotonic.h"
#include "uri_list.h"

#include <string.h>

// How long a drop's conversions may take.
#define DEADLINE_NS 5000000000u

// The most types an XdndTypeList is read for.
#define MAX_TYPES 64u

// Sends a client message to the drag's source.
static void Send(const mwinX11Platform* platform, xcb_atom_t type, const uint32_t data[5])
{
    xcb_client_message_event_t message = {0};
    message.response_type = XCB_CLIENT_MESSAGE;
    message.format = 32;
    message.window = platform->drag.source;
    message.type = type;
    memcpy(message.data.data32, data, sizeof(message.data.data32));
    platform->api.sendEvent(platform->connection, 0, platform->drag.source, XCB_EVENT_MASK_NO_EVENT,
                            (const char*)&message);
}

// Notes one type the source offers.
static void Offer(mwinX11Platform* platform, xcb_atom_t type)
{
    mwinX11Drag* drag = &platform->drag;
    const xcb_atom_t* atoms = platform->atoms;
    if (type == atoms[mwin_atomUriList])
    {
        drag->contents |= mwin_dragFiles;
    }
    // text/plain;charset=utf-8 before UTF8_STRING.
    if (type == atoms[mwin_atomTextPlainUtf8] ||
        (type == atoms[mwin_atomUtf8String] && drag->textType == XCB_ATOM_NONE))
    {
        drag->contents |= mwin_dragText;
        drag->textType = type;
    }
}

// The types past three, in the source's XdndTypeList.
static void ReadTypeList(mwinX11Platform* platform)
{
    const mwinX11Api* api = &platform->api;
    xcb_get_property_reply_t* reply = api->getPropertyReply(
        platform->connection,
        api->getProperty(platform->connection, 0, platform->drag.source,
                         platform->atoms[mwin_atomXdndTypeList], XCB_ATOM_ATOM, 0, MAX_TYPES),
        nullptr);
    if (reply != nullptr && reply->format == 32)
    {
        const xcb_atom_t* types = api->getPropertyValue(reply);
        // The length is in bytes.
        int count = api->getPropertyValueLength(reply) / (int)sizeof(xcb_atom_t);
        for (int i = 0; i < count; i++)
        {
            Offer(platform, types[i]);
        }
    }
    mwinReleaseSystemMemory(reply);
}

static void OnEnter(mwinX11Platform* platform, const xcb_client_message_event_t* event)
{
    mwinX11Drag* drag = &platform->drag;
    const uint32_t* data = event->data.data32;
    // A drop being converted keeps its state; a new drag waits for it.
    if (drag->dropping)
    {
        return;
    }
    *drag = (mwinX11Drag){.source = data[0],
                          .version = data[1] >> 24,
                          .slot = mwinX11SlotOf(platform, event->window)};
    if ((data[1] & 1u) != 0)
    {
        ReadTypeList(platform);
    }
    for (int i = 2; i < 5; i++)
    {
        Offer(platform, data[i]);
    }
}

static void Post(mwinX11Platform* platform, mwinEventType type)
{
    const mwinX11Drag* drag = &platform->drag;
    mwinEvent event = {.type = type, .timeNs = mwinMonotonicNow()};
    event.data.drag = (mwinDragEvent){drag->position, drag->contents};
    mwinPost(platform->context, (uint32_t)drag->slot, &event);
}

// A position on the screen in the window, in logical units.
static mwinPosition PositionOf(const mwinX11Platform* platform, xcb_window_t window,
                               uint32_t packed)
{
    const mwinX11Api* api = &platform->api;
    int16_t x = (int16_t)(packed >> 16);
    int16_t y = (int16_t)(packed & 0xFFFFu);
    xcb_translate_coordinates_reply_t* reply = api->translateCoordinatesReply(
        platform->connection,
        api->translateCoordinates(platform->connection, platform->screen->root, window, x, y),
        nullptr);
    mwinPosition position = {0.0f, 0.0f};
    if (reply != nullptr)
    {
        position = (mwinPosition){(float)reply->dst_x / platform->scale,
                                  (float)reply->dst_y / platform->scale};
    }
    mwinReleaseSystemMemory(reply);
    return position;
}

static void OnPosition(mwinX11Platform* platform, const xcb_client_message_event_t* event)
{
    mwinX11Drag* drag = &platform->drag;
    const uint32_t* data = event->data.data32;
    if (drag->dropping || data[0] != drag->source)
    {
        return;
    }
    bool taken = drag->slot >= 0 && drag->contents != 0;
    if (taken)
    {
        mwinPosition position = PositionOf(platform, event->window, data[2]);
        bool moved = position.x != drag->position.x || position.y != drag->position.y;
        drag->position = position;
        if (!drag->entered || moved)
        {
            Post(platform, drag->entered ? mwin_eventDragMoved : mwin_eventDragEntered);
        }
        drag->entered = true;
    }
    // Accepted or not, with positions asked for at each move.
    uint32_t status[5] = {event->window, taken ? 3u : 2u, 0, 0,
                          taken ? platform->atoms[mwin_atomXdndActionCopy] : XCB_ATOM_NONE};
    Send(platform, platform->atoms[mwin_atomXdndStatus], status);
}

static void OnLeave(mwinX11Platform* platform, const xcb_client_message_event_t* event)
{
    mwinX11Drag* drag = &platform->drag;
    if (drag->dropping || event->data.data32[0] != drag->source)
    {
        return;
    }
    if (drag->entered)
    {
        Post(platform, mwin_eventDragLeft);
    }
    *drag = (mwinX11Drag){.slot = -1};
}

// Asks the source for the drop in a type, into the window's property.
static void Convert(mwinX11Platform* platform, xcb_atom_t type)
{
    mwinX11Drag* drag = &platform->drag;
    drag->converting = type;
    platform->api.convertSelection(platform->connection, platform->windows[drag->slot].window,
                                   platform->atoms[mwin_atomXdndSelection], type,
                                   platform->atoms[mwin_atomSelection], drag->time);
}

// Delivers the drop gathered so far and tells the source it finished.
static void Finish(mwinX11Platform* platform, bool truncated)
{
    mwinX11Drag* drag = &platform->drag;
    mwinContext* context = platform->context;
    context->dropping.truncated = context->dropping.truncated || truncated;
    bool live = drag->slot >= 0 && context->windows[drag->slot].status == mwin_slotLive;
    if (live)
    {
        mwinFinishDrop(context, (uint32_t)drag->slot, drag->position, mwinMonotonicNow());
    }
    uint32_t window = live ? platform->windows[drag->slot].window : 0;
    uint32_t finished[5] = {window, live ? 1u : 0u,
                            live ? platform->atoms[mwin_atomXdndActionCopy] : XCB_ATOM_NONE, 0, 0};
    Send(platform, platform->atoms[mwin_atomXdndFinished], finished);
    *drag = (mwinX11Drag){.slot = -1};
}

static void OnDrop(mwinX11Platform* platform, const xcb_client_message_event_t* event)
{
    mwinX11Drag* drag = &platform->drag;
    const uint32_t* data = event->data.data32;
    if (drag->dropping || data[0] != drag->source)
    {
        return;
    }
    if (drag->slot < 0 || drag->contents == 0 || !drag->entered)
    {
        uint32_t finished[5] = {event->window, 0, XCB_ATOM_NONE, 0, 0};
        Send(platform, platform->atoms[mwin_atomXdndFinished], finished);
        *drag = (mwinX11Drag){.slot = -1};
        return;
    }
    drag->dropping = true;
    drag->time = data[2];
    drag->deadlineNs = mwinMonotonicNow() + DEADLINE_NS;
    mwinBeginDrop(platform->context);
    Convert(platform, (drag->contents & mwin_dragFiles) != 0 ? platform->atoms[mwin_atomUriList]
                                                             : drag->textType);
}

// Takes the converted property: false when it is not all there.
static bool Take(mwinX11Platform* platform, xcb_window_t window)
{
    const mwinX11Api* api = &platform->api;
    mwinX11Drag* drag = &platform->drag;
    mwinContext* context = platform->context;
    xcb_get_property_reply_t* reply = api->getPropertyReply(
        platform->connection,
        api->getProperty(platform->connection, 1, window, platform->atoms[mwin_atomSelection],
                         XCB_GET_PROPERTY_TYPE_ANY, 0, context->limits.dropBytes / 4 + 1),
        nullptr);
    bool whole = reply != nullptr && reply->bytes_after == 0 && reply->format == 8 &&
                 reply->type != platform->atoms[mwin_atomIncr];
    if (whole)
    {
        // The reply's bytes are the program's to decode in place.
        char* bytes = api->getPropertyValue(reply);
        size_t length = (size_t)api->getPropertyValueLength(reply);
        if (drag->converting == platform->atoms[mwin_atomUriList])
        {
            mwinGatherUriList(context, bytes, length);
        }
        else
        {
            mwinSetDroppedText(context, bytes, length);
        }
    }
    mwinReleaseSystemMemory(reply);
    return whole;
}

static void OnConverted(mwinX11Platform* platform, const xcb_selection_notify_event_t* notify)
{
    mwinX11Drag* drag = &platform->drag;
    bool whole = notify->property != XCB_ATOM_NONE && Take(platform, notify->requestor);
    platform->context->dropping.truncated = platform->context->dropping.truncated || !whole;
    bool text = drag->textType != XCB_ATOM_NONE && drag->converting != drag->textType;
    if (text)
    {
        Convert(platform, drag->textType);
        return;
    }
    Finish(platform, false);
}

bool mwinX11HandleDropEvent(mwinX11Platform* platform, const xcb_generic_event_t* event)
{
    uint8_t type = event->response_type & 0x7F;
    const xcb_atom_t* atoms = platform->atoms;
    if (type == XCB_SELECTION_NOTIFY)
    {
        const xcb_selection_notify_event_t* notify = (const xcb_selection_notify_event_t*)event;
        bool ours = platform->drag.dropping && notify->selection == atoms[mwin_atomXdndSelection];
        if (ours)
        {
            OnConverted(platform, notify);
        }
        return ours;
    }
    if (type != XCB_CLIENT_MESSAGE)
    {
        return false;
    }
    const xcb_client_message_event_t* message = (const xcb_client_message_event_t*)event;
    xcb_atom_t kind = message->type;
    if (kind == atoms[mwin_atomXdndEnter])
    {
        OnEnter(platform, message);
    }
    else if (kind == atoms[mwin_atomXdndPosition])
    {
        OnPosition(platform, message);
    }
    else if (kind == atoms[mwin_atomXdndLeave])
    {
        OnLeave(platform, message);
    }
    else if (kind == atoms[mwin_atomXdndDrop])
    {
        OnDrop(platform, message);
    }
    else
    {
        return false;
    }
    return true;
}

void mwinX11CheckDrop(mwinX11Platform* platform, uint64_t nowNs)
{
    if (platform->drag.dropping && nowNs >= platform->drag.deadlineNs)
    {
        Finish(platform, true);
    }
}
