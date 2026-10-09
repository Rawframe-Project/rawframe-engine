// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland clipboard: the data device, its offers and sources, and the
// readers of the program's text, data and primary selection.

#include "wayland_clipboard.h"

#include "clipboard_data.h"
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

// The text types the backend offers and takes, best first.
static const char* const s_types[] = {"text/plain;charset=utf-8", "UTF8_STRING", "text/plain"};

#define TYPES ((int8_t)(sizeof(s_types) / sizeof(s_types[0])))

int8_t mwinWaylandTextType(const char* type)
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

const char* mwinWaylandTextTypeName(int8_t index)
{
    return s_types[index];
}

void mwinWaylandOfferText(const mwinWaylandPlatform* platform, void* source, uint32_t opcode)
{
    uint32_t version = mwinWlVersion(&platform->api, source);
    for (int8_t i = 0; i < TYPES; i++)
    {
        (void)platform->api.proxyMarshalFlags((struct wl_proxy*)source, opcode, nullptr, version, 0,
                                              s_types[i]);
    }
}

// Keeps a type of an offer while there is room and a data read could ask
// for it.
static void KeepType(mwinWaylandTypes* types, const char* type)
{
    size_t length = strlen(type);
    if (length <= MWIN_CLIPBOARD_MIME && length < sizeof(types->bytes) - types->length)
    {
        memcpy(types->bytes + types->length, type, length + 1);
        types->length += (uint16_t)(length + 1);
    }
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
    int8_t found = mwinWaylandTextType(type);
    bool better = found >= 0 && (clipboard->incomingType < 0 || found < clipboard->incomingType);
    if (offer == clipboard->incoming && better)
    {
        clipboard->incomingType = found;
    }
    if (offer == clipboard->incoming && strcmp(type, "text/uri-list") == 0)
    {
        clipboard->incomingFiles = true;
    }
    if (offer == clipboard->incoming)
    {
        KeepType(&clipboard->incomingTypes, type);
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
    platform->clipboard.incomingTypes.length = 0;
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
    // The selection announced again keeps its offer and what is known of
    // it.
    if (offer == clipboard->selection)
    {
        return;
    }
    DestroyOffer(&platform->api, clipboard->selection);
    clipboard->selection = offer;
    bool known = offer != nullptr && offer == clipboard->incoming;
    clipboard->selectionType = known ? clipboard->incomingType : -1;
    clipboard->selectionTypes = known ? clipboard->incomingTypes : (mwinWaylandTypes){0};
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
    mwinWaylandAttachPrimary(platform);
    if (clipboard->manager == nullptr || platform->seat == nullptr || clipboard->device != nullptr)
    {
        return;
    }
    clipboard->device =
        mwinWlCreateFor(&platform->api, clipboard->manager, WL_DATA_DEVICE_MANAGER_GET_DATA_DEVICE,
                        &wl_data_device_interface, platform->seat);
    mwinWlListen(&platform->api, clipboard->device, &s_deviceListener, platform);
}

void mwinWaylandCloseSends(mwinWaylandClipboard* clipboard, bool primary)
{
    for (int i = 0; i < MWIN_WAYLAND_SENDS; i++)
    {
        if (clipboard->sends[i].fd >= 0 && clipboard->sends[i].primary == primary)
        {
            (void)close(clipboard->sends[i].fd);
            clipboard->sends[i].fd = -1;
        }
    }
}

// Stops serving the program's text and data: its readers get what they
// have.
static void DestroySource(mwinWaylandPlatform* platform)
{
    mwinWaylandClipboard* clipboard = &platform->clipboard;
    mwinWaylandCloseSends(clipboard, false);
    if (clipboard->source != nullptr)
    {
        (void)mwinWlRequest(&platform->api, clipboard->source, WL_DATA_SOURCE_DESTROY, nullptr,
                            WL_MARSHAL_FLAG_DESTROY);
        clipboard->source = nullptr;
    }
}

void mwinWaylandDetachClipboard(mwinWaylandPlatform* platform)
{
    mwinWaylandClipboard* clipboard = &platform->clipboard;
    const mwinWaylandApi* api = &platform->api;
    mwinWaylandEndDrag(platform);
    mwinWaylandDetachPrimary(platform);
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
    // Without the seat, a read under way cannot finish.
    mwinWaylandFailRead(platform);
}

static void OnTarget(void* data, struct wl_data_source* source, const char* type)
{
    (void)data;
    (void)source;
    (void)type;
}

void mwinWaylandAddSend(mwinWaylandPlatform* platform, int32_t fd, bool primary, int8_t item)
{
    mwinWaylandClipboard* clipboard = &platform->clipboard;
    for (int i = 0; i < MWIN_WAYLAND_SENDS; i++)
    {
        if (clipboard->sends[i].fd < 0 && fcntl(fd, F_SETFL, O_NONBLOCK) == 0)
        {
            clipboard->sends[i].fd = fd;
            clipboard->sends[i].offset = 0;
            clipboard->sends[i].primary = primary;
            clipboard->sends[i].item = item;
            return;
        }
    }
    (void)close(fd);
}

// A reader's pipe, for the text or an item of the data: written without
// blocking at each pump; closed for a type the source lacks.
static void OnSend(void* data, struct wl_data_source* source, const char* type, int32_t fd)
{
    (void)source;
    mwinWaylandPlatform* platform = data;
    const mwinContext* context = platform->context;
    const mwinClipboardDataItem* item = mwinFindClipboardItem(context, type, strlen(type));
    if (item != nullptr)
    {
        mwinWaylandAddSend(platform, fd, false, (int8_t)(item - context->clipboardData->items));
    }
    else if (mwinWaylandTextType(type) >= 0 && mwinOffersClipboardText(context))
    {
        mwinWaylandAddSend(platform, fd, false, -1);
    }
    else
    {
        (void)close(fd);
    }
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

// Sets a source of the clipboard's text, when it has one, and data.
static int WriteClipboard(mwinWaylandPlatform* platform)
{
    mwinWaylandClipboard* clipboard = &platform->clipboard;
    const mwinWaylandApi* api = &platform->api;
    const mwinClipboardCopy* copy = platform->context->clipboardData;
    if (clipboard->device == nullptr)
    {
        return mwin_outcomeUnsupported;
    }
    DestroySource(platform);
    struct wl_data_source* source =
        mwinWlRequest(api, clipboard->manager, WL_DATA_DEVICE_MANAGER_CREATE_DATA_SOURCE,
                      &wl_data_source_interface, 0);
    mwinWlListen(api, source, &s_sourceListener, platform);
    if (mwinOffersClipboardText(platform->context))
    {
        mwinWaylandOfferText(platform, source, WL_DATA_SOURCE_OFFER);
    }
    for (uint32_t i = 0; copy != nullptr && i < copy->count; i++)
    {
        (void)api->proxyMarshalFlags((struct wl_proxy*)source, WL_DATA_SOURCE_OFFER, nullptr,
                                     mwinWlVersion(api, source), 0, copy->items[i].mime);
    }
    (void)api->proxyMarshalFlags((struct wl_proxy*)clipboard->device, WL_DATA_DEVICE_SET_SELECTION,
                                 nullptr, mwinWlVersion(api, clipboard->device), 0, source,
                                 platform->inputSerial);
    clipboard->source = source;
    return mwin_outcomeDone;
}

int mwinWaylandWriteSelection(mwinWaylandPlatform* platform, mwinRequestKind kind)
{
    return kind == mwin_requestPrimaryWrite ? mwinWaylandWritePrimary(platform)
                                            : WriteClipboard(platform);
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

// The bytes a reader takes: the primary selection's text, the
// clipboard's text (item -1) or an item of its data.
static const char* SourceOf(const mwinContext* context, bool primary, int8_t item,
                            uint32_t* lengthOut)
{
    if (primary)
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

static void PumpSends(mwinWaylandPlatform* platform)
{
    mwinWaylandClipboard* clipboard = &platform->clipboard;
    for (int i = 0; i < MWIN_WAYLAND_SENDS; i++)
    {
        uint32_t offset = clipboard->sends[i].offset;
        if (clipboard->sends[i].fd < 0)
        {
            continue;
        }
        uint32_t length = 0;
        const char* bytes = SourceOf(platform->context, clipboard->sends[i].primary,
                                     clipboard->sends[i].item, &length);
        ssize_t written =
            offset < length ? WriteQuietly(clipboard->sends[i].fd, bytes + offset, length - offset)
                            : 0;
        bool waiting = written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);
        clipboard->sends[i].offset += written > 0 ? (uint32_t)written : 0;
        bool done = clipboard->sends[i].offset >= length;
        if ((written < 0 && !waiting) || done)
        {
            (void)close(clipboard->sends[i].fd);
            clipboard->sends[i].fd = -1;
        }
    }
}

void mwinWaylandPumpClipboard(mwinWaylandPlatform* platform, uint64_t nowNs)
{
    PumpSends(platform);
    mwinWaylandPumpRead(platform, nowNs);
}
