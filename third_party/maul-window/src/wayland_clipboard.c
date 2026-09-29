// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland clipboard.

#include "wayland_clipboard.h"

#include "wayland_drop.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// wl_data_device_manager 3, the version the backend implements.
#define DATA_MANAGER_VERSION 3

// How long a read waits for the other client to finish.
#define READ_DEADLINE_NS 5000000000u

// The text types the backend offers and takes, best first.
static const char* const s_types[] = {"text/plain;charset=utf-8", "UTF8_STRING", "text/plain"};

#define TYPES ((int8_t)(sizeof(s_types) / sizeof(s_types[0])))

static int8_t TypeOf(const char* type)
{
    for (int8_t i = 0; i < TYPES; i++)
    {
        if (strcmp(type, s_types[i]) == 0)
        {
            return i;
        }
    }
    return -1;
}

static void DestroyOffer(const mwinWaylandApi* api, struct wl_data_offer* offer)
{
    if (offer != nullptr)
    {
        (void)mwinWlRequest(api, offer, WL_DATA_OFFER_DESTROY, nullptr, WL_MARSHAL_FLAG_DESTROY);
    }
}

static void OnOfferType(void* data, struct wl_data_offer* offer, const char* type)
{
    mwinWaylandClipboard* clipboard = &((mwinWaylandPlatform*)data)->clipboard;
    int8_t found = TypeOf(type);
    bool better = found >= 0 && (clipboard->incomingType < 0 || found < clipboard->incomingType);
    if (offer == clipboard->incoming && better)
    {
        clipboard->incomingType = found;
    }
    if (offer == clipboard->incoming && strcmp(type, "text/uri-list") == 0)
    {
        clipboard->incomingFiles = true;
    }
}

static void OnOfferActions(void* data, struct wl_data_offer* offer, uint32_t actions)
{
    (void)data;
    (void)offer;
    (void)actions;
}

static const struct wl_data_offer_listener s_offerListener = {
    OnOfferType,
    OnOfferActions,
    OnOfferActions,
};

static void OnDataOffer(void* data, struct wl_data_device* device, struct wl_data_offer* offer)
{
    (void)device;
    mwinWaylandPlatform* platform = data;
    platform->clipboard.incoming = offer;
    platform->clipboard.incomingType = -1;
    platform->clipboard.incomingFiles = false;
    mwinWlListen(&platform->api, offer, &s_offerListener, platform);
}

// A drag's offer came with the offer events just before its enter.
static void OnEnter(void* data, struct wl_data_device* device, uint32_t serial,
                    struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y,
                    struct wl_data_offer* offer)
{
    (void)device;
    mwinWaylandPlatform* platform = data;
    mwinWaylandClipboard* clipboard = &platform->clipboard;
    bool known = offer != nullptr && offer == clipboard->incoming;
    bool files = known && clipboard->incomingFiles;
    const char* textType =
        known && clipboard->incomingType >= 0 ? s_types[clipboard->incomingType] : nullptr;
    clipboard->incoming = known ? nullptr : clipboard->incoming;
    mwinPosition position = {(float)wl_fixed_to_double(x), (float)wl_fixed_to_double(y)};
    mwinWaylandDragEnter(platform, serial, surface, position, offer, files, textType);
}

static void OnLeave(void* data, struct wl_data_device* device)
{
    (void)device;
    mwinWaylandDragLeave(data);
}

static void OnMotion(void* data, struct wl_data_device* device, uint32_t time, wl_fixed_t x,
                     wl_fixed_t y)
{
    (void)device;
    (void)time;
    mwinWaylandDragMotion(
        data, (mwinPosition){(float)wl_fixed_to_double(x), (float)wl_fixed_to_double(y)});
}

static void OnDrop(void* data, struct wl_data_device* device)
{
    (void)device;
    mwinWaylandDragDrop(data);
}

static void OnSelection(void* data, struct wl_data_device* device, struct wl_data_offer* offer)
{
    (void)device;
    mwinWaylandPlatform* platform = data;
    mwinWaylandClipboard* clipboard = &platform->clipboard;
    DestroyOffer(&platform->api, clipboard->selection);
    clipboard->selection = offer;
    clipboard->selectionType =
        offer != nullptr && offer == clipboard->incoming ? clipboard->incomingType : -1;
    clipboard->incoming = nullptr;
}

static const struct wl_data_device_listener s_deviceListener = {
    OnDataOffer, OnEnter, OnLeave, OnMotion, OnDrop, OnSelection,
};

void mwinWaylandBindDataManager(mwinWaylandPlatform* platform, uint32_t name, uint32_t version)
{
    uint32_t bound = version < DATA_MANAGER_VERSION ? version : DATA_MANAGER_VERSION;
    platform->clipboard.manager = (struct wl_data_device_manager*)platform->api.proxyMarshalFlags(
        (struct wl_proxy*)platform->registry, WL_REGISTRY_BIND, &wl_data_device_manager_interface,
        bound, 0, name, wl_data_device_manager_interface.name, bound, nullptr);
}

void mwinWaylandInitClipboard(mwinWaylandClipboard* clipboard)
{
    clipboard->reading = (mwinWaylandPipe){.fd = -1};
    for (int i = 0; i < MWIN_WAYLAND_SENDS; i++)
    {
        clipboard->sends[i].fd = -1;
    }
}

void mwinWaylandAttachClipboard(mwinWaylandPlatform* platform)
{
    mwinWaylandClipboard* clipboard = &platform->clipboard;
    if (clipboard->manager == nullptr || platform->seat == nullptr || clipboard->device != nullptr)
    {
        return;
    }
    clipboard->device =
        mwinWlCreateFor(&platform->api, clipboard->manager, WL_DATA_DEVICE_MANAGER_GET_DATA_DEVICE,
                        &wl_data_device_interface, platform->seat);
    mwinWlListen(&platform->api, clipboard->device, &s_deviceListener, platform);
}

// Stops serving the program's text: its readers get what they have.
static void CloseSends(mwinWaylandClipboard* clipboard)
{
    for (int i = 0; i < MWIN_WAYLAND_SENDS; i++)
    {
        if (clipboard->sends[i].fd >= 0)
        {
            (void)close(clipboard->sends[i].fd);
            clipboard->sends[i].fd = -1;
        }
    }
}

static void DestroySource(mwinWaylandPlatform* platform)
{
    mwinWaylandClipboard* clipboard = &platform->clipboard;
    CloseSends(clipboard);
    if (clipboard->source != nullptr)
    {
        (void)mwinWlRequest(&platform->api, clipboard->source, WL_DATA_SOURCE_DESTROY, nullptr,
                            WL_MARSHAL_FLAG_DESTROY);
        clipboard->source = nullptr;
    }
}

static void AnswerReads(mwinContext* context, mwinOutcome outcome);

// Ends a read: its pipe closed and its text given back.
static void EndRead(mwinWaylandPlatform* platform)
{
    mwinWaylandClosePipe(&platform->clipboard.reading, platform->context);
}

void mwinWaylandDetachClipboard(mwinWaylandPlatform* platform)
{
    mwinWaylandClipboard* clipboard = &platform->clipboard;
    const mwinWaylandApi* api = &platform->api;
    mwinWaylandEndDrag(platform);
    // Without the seat, a read under way cannot finish.
    if (clipboard->reading.fd >= 0)
    {
        EndRead(platform);
        AnswerReads(platform->context, mwin_outcomeFailed);
    }
    DestroySource(platform);
    DestroyOffer(api, clipboard->selection);
    DestroyOffer(api, clipboard->incoming);
    clipboard->selection = nullptr;
    clipboard->incoming = nullptr;
    if (clipboard->device != nullptr)
    {
        if (mwinWlVersion(api, clipboard->device) >= WL_DATA_DEVICE_RELEASE_SINCE_VERSION)
        {
            (void)mwinWlRequest(api, clipboard->device, WL_DATA_DEVICE_RELEASE, nullptr,
                                WL_MARSHAL_FLAG_DESTROY);
        }
        else
        {
            api->proxyDestroy((struct wl_proxy*)clipboard->device);
        }
        clipboard->device = nullptr;
    }
}

static void OnTarget(void* data, struct wl_data_source* source, const char* type)
{
    (void)data;
    (void)source;
    (void)type;
}

// A reader's pipe, written without blocking at each pump; dropped when
// every place is taken.
static void OnSend(void* data, struct wl_data_source* source, const char* type, int32_t fd)
{
    (void)source;
    (void)type;
    mwinWaylandClipboard* clipboard = &((mwinWaylandPlatform*)data)->clipboard;
    for (int i = 0; i < MWIN_WAYLAND_SENDS; i++)
    {
        if (clipboard->sends[i].fd < 0 && fcntl(fd, F_SETFL, O_NONBLOCK) == 0)
        {
            clipboard->sends[i].fd = fd;
            clipboard->sends[i].offset = 0;
            return;
        }
    }
    (void)close(fd);
}

// Another client took the selection.
static void OnCancelled(void* data, struct wl_data_source* source)
{
    (void)source;
    DestroySource(data);
}

static void OnSourceEvent(void* data, struct wl_data_source* source)
{
    (void)data;
    (void)source;
}

static void OnSourceAction(void* data, struct wl_data_source* source, uint32_t action)
{
    (void)data;
    (void)source;
    (void)action;
}

static const struct wl_data_source_listener s_sourceListener = {
    OnTarget, OnSend, OnCancelled, OnSourceEvent, OnSourceEvent, OnSourceAction,
};

int mwinWaylandWriteClipboard(mwinWaylandPlatform* platform)
{
    mwinWaylandClipboard* clipboard = &platform->clipboard;
    const mwinWaylandApi* api = &platform->api;
    if (clipboard->device == nullptr)
    {
        return mwin_outcomeUnsupported;
    }
    DestroySource(platform);
    struct wl_data_source* source =
        mwinWlRequest(api, clipboard->manager, WL_DATA_DEVICE_MANAGER_CREATE_DATA_SOURCE,
                      &wl_data_source_interface, 0);
    mwinWlListen(api, source, &s_sourceListener, platform);
    uint32_t version = mwinWlVersion(api, source);
    for (int8_t i = 0; i < TYPES; i++)
    {
        (void)api->proxyMarshalFlags((struct wl_proxy*)source, WL_DATA_SOURCE_OFFER, nullptr,
                                     version, 0, s_types[i]);
    }
    (void)api->proxyMarshalFlags((struct wl_proxy*)clipboard->device, WL_DATA_DEVICE_SET_SELECTION,
                                 nullptr, mwinWlVersion(api, clipboard->device), 0, source,
                                 platform->inputSerial);
    clipboard->source = source;
    return mwin_outcomeDone;
}

// Starts taking the selection's text through a pipe; false when it
// cannot.
static bool StartRead(mwinWaylandPlatform* platform)
{
    mwinWaylandClipboard* clipboard = &platform->clipboard;
    int writer = mwinWaylandOpenPipe(&clipboard->reading);
    if (writer < 0)
    {
        return false;
    }
    // The request carries a copy of the writing end.
    (void)platform->api.proxyMarshalFlags((struct wl_proxy*)clipboard->selection,
                                          WL_DATA_OFFER_RECEIVE, nullptr,
                                          mwinWlVersion(&platform->api, clipboard->selection), 0,
                                          s_types[clipboard->selectionType], writer);
    (void)close(writer);
    clipboard->deadlineNs = mwinMonotonicNow() + READ_DEADLINE_NS;
    return true;
}

int mwinWaylandReadClipboard(mwinWaylandPlatform* platform)
{
    mwinWaylandClipboard* clipboard = &platform->clipboard;
    mwinContext* context = platform->context;
    if (clipboard->device == nullptr)
    {
        return mwin_outcomeUnsupported;
    }
    if (clipboard->source != nullptr)
    {
        return mwinTakeClipboardText(context, context->clipboardOffer,
                                     context->clipboardOfferLength);
    }
    // A read under way answers this one too.
    if (clipboard->reading.fd >= 0)
    {
        return -1;
    }
    if (clipboard->selection == nullptr || clipboard->selectionType < 0)
    {
        return mwinTakeClipboardText(context, nullptr, 0);
    }
    return StartRead(platform) ? -1 : mwin_outcomeFailed;
}

// Writes without the process being killed by SIGPIPE when the reader
// has gone: the signal is held for this thread and, when the write
// raised it, taken back, unless it was waiting before.
static ssize_t WriteQuietly(int fd, const void* bytes, size_t size)
{
    sigset_t pipe;
    sigset_t old;
    sigset_t waiting;
    sigemptyset(&pipe);
    sigaddset(&pipe, SIGPIPE);
    (void)pthread_sigmask(SIG_BLOCK, &pipe, &old);
    sigemptyset(&waiting);
    (void)sigpending(&waiting);
    bool before = sigismember(&waiting, SIGPIPE) == 1;
    ssize_t written = write(fd, bytes, size);
    int error = errno;
    if (written < 0 && error == EPIPE && !before)
    {
        struct timespec none = {0, 0};
        (void)sigtimedwait(&pipe, nullptr, &none);
    }
    (void)pthread_sigmask(SIG_SETMASK, &old, nullptr);
    errno = error;
    return written;
}

static void PumpSends(mwinWaylandPlatform* platform)
{
    mwinWaylandClipboard* clipboard = &platform->clipboard;
    const mwinContext* context = platform->context;
    for (int i = 0; i < MWIN_WAYLAND_SENDS; i++)
    {
        uint32_t offset = clipboard->sends[i].offset;
        if (clipboard->sends[i].fd < 0)
        {
            continue;
        }
        ssize_t written =
            offset < context->clipboardOfferLength
                ? WriteQuietly(clipboard->sends[i].fd, context->clipboardOffer + offset,
                               context->clipboardOfferLength - offset)
                : 0;
        bool waiting = written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);
        clipboard->sends[i].offset += written > 0 ? (uint32_t)written : 0;
        bool done = clipboard->sends[i].offset >= context->clipboardOfferLength;
        if ((written < 0 && !waiting) || done)
        {
            (void)close(clipboard->sends[i].fd);
            clipboard->sends[i].fd = -1;
        }
    }
}

// Answers the read requests of every window.
static void AnswerReads(mwinContext* context, mwinOutcome outcome)
{
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

void mwinWaylandPumpClipboard(mwinWaylandPlatform* platform, uint64_t nowNs)
{
    PumpSends(platform);
    mwinWaylandClipboard* clipboard = &platform->clipboard;
    const mwinContext* context = platform->context;
    if (clipboard->reading.fd < 0)
    {
        return;
    }
    int outcome = mwinWaylandReadPipe(&clipboard->reading, context, context->limits.clipboardBytes);
    if (outcome == mwin_outcomeDone)
    {
        outcome = mwinTakeClipboardText(platform->context, clipboard->reading.bytes,
                                        clipboard->reading.length);
    }
    if (outcome < 0 && nowNs >= clipboard->deadlineNs)
    {
        outcome = mwin_outcomeFailed;
    }
    if (outcome >= 0)
    {
        EndRead(platform);
        AnswerReads(platform->context, (mwinOutcome)outcome);
    }
}
