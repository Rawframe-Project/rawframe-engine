// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland primary selection, through its protocol: its device, the
// offers another client makes and the program's source.

#include "wayland_clipboard.h"

#include <unistd.h>

// zwp_primary_selection_device_manager_v1 1, the version the backend
// implements.
#define PRIMARY_MANAGER_VERSION 1

static void DestroyOffer(const mwinWaylandApi* api, struct zwp_primary_selection_offer_v1* offer)
{
    if (offer != nullptr)
    {
        (void)mwinWlRequest(api, offer, ZWP_PRIMARY_SELECTION_OFFER_V1_DESTROY, nullptr,
                            WL_MARSHAL_FLAG_DESTROY);
    }
}

static void OnOfferType(void* data, struct zwp_primary_selection_offer_v1* offer, const char* type)
{
    mwinWaylandClipboard* clipboard = &((mwinWaylandPlatform*)data)->clipboard;
    int8_t found = mwinWaylandTextType(type);
    bool better = found >= 0 &&
                  (clipboard->primaryIncomingType < 0 || found < clipboard->primaryIncomingType);
    if (offer == clipboard->primaryIncoming && better)
    {
        clipboard->primaryIncomingType = found;
    }
}

static const struct zwp_primary_selection_offer_v1_listener s_offerListener = {
    OnOfferType,
};

static void OnDataOffer(void* data, struct zwp_primary_selection_device_v1* device,
                        struct zwp_primary_selection_offer_v1* offer)
{
    (void)device;
    mwinWaylandPlatform* platform = data;
    DestroyOffer(&platform->api, platform->clipboard.primaryIncoming);
    platform->clipboard.primaryIncoming = offer;
    platform->clipboard.primaryIncomingType = -1;
    mwinWlListen(&platform->api, offer, &s_offerListener, platform);
}

static void OnSelection(void* data, struct zwp_primary_selection_device_v1* device,
                        struct zwp_primary_selection_offer_v1* offer)
{
    (void)device;
    mwinWaylandPlatform* platform = data;
    mwinWaylandClipboard* clipboard = &platform->clipboard;
    // The selection announced again keeps its offer and what is known of
    // it.
    if (offer == clipboard->primarySelection)
    {
        return;
    }
    DestroyOffer(&platform->api, clipboard->primarySelection);
    bool known = offer != nullptr && offer == clipboard->primaryIncoming;
    clipboard->primarySelection = offer;
    clipboard->primarySelectionType = known ? clipboard->primaryIncomingType : -1;
    clipboard->primaryIncoming = known ? nullptr : clipboard->primaryIncoming;
}

static const struct zwp_primary_selection_device_v1_listener s_deviceListener = {
    OnDataOffer,
    OnSelection,
};

void mwinWaylandBindPrimaryManager(mwinWaylandPlatform* platform, uint32_t name, uint32_t version)
{
    uint32_t bound = version < PRIMARY_MANAGER_VERSION ? version : PRIMARY_MANAGER_VERSION;
    platform->clipboard.primaryManager =
        (struct zwp_primary_selection_device_manager_v1*)platform->api.proxyMarshalFlags(
            (struct wl_proxy*)platform->registry, WL_REGISTRY_BIND,
            &zwp_primary_selection_device_manager_v1_interface, bound, 0, name,
            zwp_primary_selection_device_manager_v1_interface.name, bound, nullptr);
}

void mwinWaylandAttachPrimary(mwinWaylandPlatform* platform)
{
    mwinWaylandClipboard* clipboard = &platform->clipboard;
    if (clipboard->primaryManager == nullptr || platform->seat == nullptr ||
        clipboard->primaryDevice != nullptr)
    {
        return;
    }
    clipboard->primaryDevice =
        mwinWlCreateFor(&platform->api, clipboard->primaryManager,
                        ZWP_PRIMARY_SELECTION_DEVICE_MANAGER_V1_GET_DEVICE,
                        &zwp_primary_selection_device_v1_interface, platform->seat);
    mwinWlListen(&platform->api, clipboard->primaryDevice, &s_deviceListener, platform);
}

// Stops serving the program's selected text: its readers get what they
// have.
static void DestroySource(mwinWaylandPlatform* platform)
{
    mwinWaylandClipboard* clipboard = &platform->clipboard;
    mwinWaylandCloseSends(clipboard, true);
    if (clipboard->primarySource != nullptr)
    {
        (void)mwinWlRequest(&platform->api, clipboard->primarySource,
                            ZWP_PRIMARY_SELECTION_SOURCE_V1_DESTROY, nullptr,
                            WL_MARSHAL_FLAG_DESTROY);
        clipboard->primarySource = nullptr;
    }
}

void mwinWaylandDetachPrimary(mwinWaylandPlatform* platform)
{
    mwinWaylandClipboard* clipboard = &platform->clipboard;
    const mwinWaylandApi* api = &platform->api;
    DestroySource(platform);
    DestroyOffer(api, clipboard->primarySelection);
    DestroyOffer(api, clipboard->primaryIncoming);
    clipboard->primarySelection = nullptr;
    clipboard->primaryIncoming = nullptr;
    if (clipboard->primaryDevice != nullptr)
    {
        (void)mwinWlRequest(api, clipboard->primaryDevice, ZWP_PRIMARY_SELECTION_DEVICE_V1_DESTROY,
                            nullptr, WL_MARSHAL_FLAG_DESTROY);
        clipboard->primaryDevice = nullptr;
    }
}

// A reader's pipe, for the text: written without blocking at each pump;
// closed for a type the source lacks.
static void OnSend(void* data, struct zwp_primary_selection_source_v1* source, const char* type,
                   int32_t fd)
{
    (void)source;
    if (mwinWaylandTextType(type) >= 0)
    {
        mwinWaylandAddSend(data, fd, true, -1);
    }
    else
    {
        (void)close(fd);
    }
}

// Another client took the selection.
static void OnCancelled(void* data, struct zwp_primary_selection_source_v1* source)
{
    (void)source;
    DestroySource(data);
}

static const struct zwp_primary_selection_source_v1_listener s_sourceListener = {
    OnSend,
    OnCancelled,
};

int mwinWaylandWritePrimary(mwinWaylandPlatform* platform)
{
    mwinWaylandClipboard* clipboard = &platform->clipboard;
    const mwinWaylandApi* api = &platform->api;
    if (clipboard->primaryDevice == nullptr)
    {
        return mwin_outcomeUnsupported;
    }
    DestroySource(platform);
    struct zwp_primary_selection_source_v1* source = mwinWlRequest(
        api, clipboard->primaryManager, ZWP_PRIMARY_SELECTION_DEVICE_MANAGER_V1_CREATE_SOURCE,
        &zwp_primary_selection_source_v1_interface, 0);
    mwinWlListen(api, source, &s_sourceListener, platform);
    mwinWaylandOfferText(platform, source, ZWP_PRIMARY_SELECTION_SOURCE_V1_OFFER);
    (void)api->proxyMarshalFlags(
        (struct wl_proxy*)clipboard->primaryDevice, ZWP_PRIMARY_SELECTION_DEVICE_V1_SET_SELECTION,
        nullptr, mwinWlVersion(api, clipboard->primaryDevice), 0, source, platform->inputSerial);
    clipboard->primarySource = source;
    return mwin_outcomeDone;
}
