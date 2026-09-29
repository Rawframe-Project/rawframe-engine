// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland seat.

#include "wayland_seat.h"

#include "wayland_clipboard.h"
#include "wayland_keyboard.h"
#include "wayland_pointer.h"
#include "wayland_text.h"

// wl_seat 9, with its devices: the version the backend implements.
#define SEAT_VERSION 9

static void OnCapabilities(void* data, struct wl_seat* seat, uint32_t capabilities)
{
    (void)seat;
    mwinWaylandPlatform* platform = data;
    bool keyboard = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
    if (keyboard && platform->keyboard.keyboard == nullptr)
    {
        mwinWaylandAddKeyboard(platform);
    }
    else if (!keyboard && platform->keyboard.keyboard != nullptr)
    {
        mwinWaylandRemoveKeyboard(platform);
    }
    bool pointer = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
    if (pointer && platform->pointer.pointer == nullptr)
    {
        mwinWaylandAddPointer(platform);
    }
    else if (!pointer && platform->pointer.pointer != nullptr)
    {
        mwinWaylandRemovePointer(platform);
    }
    bool touch = (capabilities & WL_SEAT_CAPABILITY_TOUCH) != 0;
    if (touch && platform->touch.touch == nullptr)
    {
        mwinWaylandAddTouch(platform);
    }
    else if (!touch && platform->touch.touch != nullptr)
    {
        mwinWaylandRemoveTouch(platform);
    }
}

static void OnName(void* data, struct wl_seat* seat, const char* name)
{
    (void)data;
    (void)seat;
    (void)name;
}

static const struct wl_seat_listener s_seatListener = {
    OnCapabilities,
    OnName,
};

void mwinWaylandBindSeat(mwinWaylandPlatform* platform, uint32_t name, uint32_t version)
{
    if (platform->seat != nullptr)
    {
        return;
    }
    uint32_t bound = version < SEAT_VERSION ? version : SEAT_VERSION;
    platform->seat = (struct wl_seat*)platform->api.proxyMarshalFlags(
        (struct wl_proxy*)platform->registry, WL_REGISTRY_BIND, &wl_seat_interface, bound, 0, name,
        wl_seat_interface.name, bound, nullptr);
    platform->seatName = name;
    mwinWlListen(&platform->api, platform->seat, &s_seatListener, platform);
}

void mwinWaylandReleaseSeat(mwinWaylandPlatform* platform)
{
    if (platform->seat == nullptr)
    {
        return;
    }
    mwinWaylandDetachText(platform);
    mwinWaylandDetachClipboard(platform);
    if (platform->keyboard.keyboard != nullptr)
    {
        mwinWaylandRemoveKeyboard(platform);
    }
    if (platform->pointer.pointer != nullptr)
    {
        mwinWaylandRemovePointer(platform);
    }
    if (platform->touch.touch != nullptr)
    {
        mwinWaylandRemoveTouch(platform);
    }
    const mwinWaylandApi* api = &platform->api;
    if (mwinWlVersion(api, platform->seat) >= WL_SEAT_RELEASE_SINCE_VERSION)
    {
        (void)mwinWlRequest(api, platform->seat, WL_SEAT_RELEASE, nullptr, WL_MARSHAL_FLAG_DESTROY);
    }
    else
    {
        api->proxyDestroy((struct wl_proxy*)platform->seat);
    }
    platform->seat = nullptr;
}

void mwinWaylandRemoveSeat(mwinWaylandPlatform* platform, uint32_t name)
{
    if (platform->seat != nullptr && platform->seatName == name)
    {
        mwinWaylandReleaseSeat(platform);
    }
}
