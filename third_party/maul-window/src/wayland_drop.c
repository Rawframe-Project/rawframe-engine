// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Wayland drag and drop.

#include "wayland_drop.h"

#include "uri_list.h"

#include <unistd.h>

// How long a drop's reads may take.
#define DROP_DEADLINE_NS 5000000000u

static const char s_files[] = "text/uri-list";

void mwinWaylandInitDrag(mwinWaylandDrag* drag)
{
    *drag = (mwinWaylandDrag){.slot = -1, .dropSlot = -1};
    drag->files.fd = -1;
    drag->text.fd = -1;
}

static void DestroyOffer(const mwinWaylandApi* api, struct wl_data_offer* offer)
{
    if (offer != nullptr)
    {
        (void)mwinWlRequest(api, offer, WL_DATA_OFFER_DESTROY, nullptr, WL_MARSHAL_FLAG_DESTROY);
    }
}

static void Post(mwinWaylandPlatform* platform, mwinEventType type)
{
    const mwinWaylandDrag* drag = &platform->drag;
    mwinEvent event = {.type = type, .timeNs = mwinMonotonicNow()};
    event.data.drag = (mwinDragEvent){drag->position, drag->contents};
    mwinPost(platform->context, (uint32_t)drag->slot, &event);
}

// Tells the source which type the drop would take, or none, and that
// the program copies.
static void Accept(mwinWaylandPlatform* platform, const char* type)
{
    const mwinWaylandApi* api = &platform->api;
    const mwinWaylandDrag* drag = &platform->drag;
    struct wl_proxy* offer = (struct wl_proxy*)drag->offer;
    uint32_t version = mwinWlVersion(api, drag->offer);
    (void)api->proxyMarshalFlags(offer, WL_DATA_OFFER_ACCEPT, nullptr, version, 0, drag->serial,
                                 type);
    if (version >= WL_DATA_OFFER_SET_ACTIONS_SINCE_VERSION)
    {
        uint32_t copy = type != nullptr ? WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY : 0;
        (void)api->proxyMarshalFlags(offer, WL_DATA_OFFER_SET_ACTIONS, nullptr, version, 0, copy,
                                     copy);
    }
}

void mwinWaylandDragEnter(mwinWaylandPlatform* platform, uint32_t serial,
                          struct wl_surface* surface, mwinPosition position,
                          struct wl_data_offer* offer, bool files, const char* textType)
{
    mwinWaylandDrag* drag = &platform->drag;
    DestroyOffer(&platform->api, drag->offer);
    drag->offer = offer;
    drag->serial = serial;
    drag->position = position;
    drag->textType = textType;
    drag->slot = mwinWaylandSlotOf(platform, surface);
    drag->contents = (files ? mwin_dragFiles : 0) | (textType != nullptr ? mwin_dragText : 0);
    if (offer == nullptr)
    {
        return;
    }
    if (drag->slot < 0 || drag->contents == 0)
    {
        drag->contents = 0;
        Accept(platform, nullptr);
        return;
    }
    Accept(platform, files ? s_files : textType);
    Post(platform, mwin_eventDragEntered);
}

void mwinWaylandDragMotion(mwinWaylandPlatform* platform, mwinPosition position)
{
    mwinWaylandDrag* drag = &platform->drag;
    drag->position = position;
    if (drag->slot >= 0 && drag->contents != 0)
    {
        Post(platform, mwin_eventDragMoved);
    }
}

void mwinWaylandDragLeave(mwinWaylandPlatform* platform)
{
    mwinWaylandDrag* drag = &platform->drag;
    if (drag->slot >= 0 && drag->contents != 0)
    {
        Post(platform, mwin_eventDragLeft);
    }
    DestroyOffer(&platform->api, drag->offer);
    drag->offer = nullptr;
    drag->slot = -1;
    drag->contents = 0;
}

// Asks the offer for a type through a pipe of the drop's.
static void Receive(mwinWaylandPlatform* platform, mwinWaylandPipe* pipe, const char* type)
{
    const mwinWaylandApi* api = &platform->api;
    struct wl_data_offer* offer = platform->drag.dropped;
    int writer = mwinWaylandOpenPipe(pipe);
    if (writer < 0)
    {
        platform->drag.lost = true;
        return;
    }
    // The request carries a copy of the writing end.
    (void)api->proxyMarshalFlags((struct wl_proxy*)offer, WL_DATA_OFFER_RECEIVE, nullptr,
                                 mwinWlVersion(api, offer), 0, type, writer);
    (void)close(writer);
}

// Delivers a drop whose reads ended (mwin_outcomeCancelled for a type
// not asked for, -1 for one cut off by the deadline), and lets its
// offer go.
static void Deliver(mwinWaylandPlatform* platform, int files, int text)
{
    mwinWaylandDrag* drag = &platform->drag;
    mwinContext* context = platform->context;
    const mwinWaylandApi* api = &platform->api;
    mwinBeginDrop(context);
    if (files == mwin_outcomeDone)
    {
        mwinGatherUriList(context, drag->files.bytes, drag->files.length);
    }
    if (text == mwin_outcomeDone)
    {
        mwinSetDroppedText(context, drag->text.bytes, drag->text.length);
    }
    bool filesLost = files != mwin_outcomeDone && files != mwin_outcomeCancelled;
    bool textLost = text != mwin_outcomeDone && text != mwin_outcomeCancelled;
    context->dropping.truncated =
        context->dropping.truncated || filesLost || textLost || drag->lost;
    if (drag->dropSlot >= 0 && context->windows[drag->dropSlot].status == mwin_slotLive)
    {
        mwinFinishDrop(context, (uint32_t)drag->dropSlot, drag->dropPosition, mwinMonotonicNow());
    }
    if (mwinWlVersion(api, drag->dropped) >= WL_DATA_OFFER_FINISH_SINCE_VERSION)
    {
        (void)mwinWlRequest(api, drag->dropped, WL_DATA_OFFER_FINISH, nullptr, 0);
    }
    DestroyOffer(api, drag->dropped);
    drag->dropped = nullptr;
    drag->dropSlot = -1;
    drag->lost = false;
    mwinWaylandClosePipe(&drag->files, context);
    mwinWaylandClosePipe(&drag->text, context);
}

void mwinWaylandDragDrop(mwinWaylandPlatform* platform)
{
    mwinWaylandDrag* drag = &platform->drag;
    if (drag->slot < 0 || drag->contents == 0 || drag->dropped != nullptr)
    {
        mwinWaylandDragLeave(platform);
        return;
    }
    drag->dropped = drag->offer;
    drag->dropSlot = drag->slot;
    drag->dropPosition = drag->position;
    drag->deadlineNs = mwinMonotonicNow() + DROP_DEADLINE_NS;
    if ((drag->contents & mwin_dragFiles) != 0)
    {
        Receive(platform, &drag->files, s_files);
    }
    if (drag->textType != nullptr)
    {
        Receive(platform, &drag->text, drag->textType);
    }
    // The drop has the offer now; no leaving is reported.
    drag->offer = nullptr;
    drag->slot = -1;
    drag->contents = 0;
}

// Reads a pipe of the drop: -1 while it goes on, else its outcome;
// one never opened ended at once, with nothing.
static int ReadDropPipe(const mwinWaylandPlatform* platform, mwinWaylandPipe* pipe)
{
    const mwinContext* context = platform->context;
    return pipe->fd >= 0 ? mwinWaylandReadPipe(pipe, context, context->limits.dropBytes)
                         : mwin_outcomeCancelled;
}

void mwinWaylandPumpDrag(mwinWaylandPlatform* platform, uint64_t nowNs)
{
    mwinWaylandDrag* drag = &platform->drag;
    if (drag->dropped == nullptr)
    {
        return;
    }
    // A pipe that ended reads its end again, so both are read each pump.
    int files = ReadDropPipe(platform, &drag->files);
    int text = ReadDropPipe(platform, &drag->text);
    if ((files >= 0 && text >= 0) || nowNs >= drag->deadlineNs)
    {
        Deliver(platform, files, text);
    }
}

void mwinWaylandEndDrag(mwinWaylandPlatform* platform)
{
    mwinWaylandDrag* drag = &platform->drag;
    DestroyOffer(&platform->api, drag->offer);
    DestroyOffer(&platform->api, drag->dropped);
    mwinWaylandClosePipe(&drag->files, platform->context);
    mwinWaylandClosePipe(&drag->text, platform->context);
    mwinWaylandInitDrag(drag);
}
