// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Wayland popups.

#include "wayland_popup.h"

#include "wayland_api.h"
#include "wayland_frame.h"

#include <math.h>

#define REPOSITION_VERSION 3

static const mwinWaylandWindow* OwnerOf(const mwinWaylandWindow* window)
{
    mwinWindowId owner = window->platform->context->windows[window->slot].def.owner;
    return &window->platform->windows[owner.index1 - 1];
}

// The owner's caption: its window geometry starts that far above its
// content.
static int32_t CaptionOf(const mwinWaylandWindow* window)
{
    const mwinWaylandWindow* owner = OwnerOf(window);
    mwinWindowMode mode = window->platform->context->windows[owner->slot].state.mode;
    return mwinWaylandCaptionOf(window->platform, owner, mode);
}

static void OnConfigure(void* data, struct xdg_popup* popup, int32_t x, int32_t y, int32_t width,
                        int32_t height)
{
    (void)popup;
    mwinWaylandWindow* window = data;
    window->proposedX = x;
    window->proposedY = y;
    window->proposedWidth = width;
    window->proposedHeight = height;
}

// The compositor dismissed the popup: a click elsewhere ended its grab.
static void OnDone(void* data, struct xdg_popup* popup)
{
    (void)popup;
    mwinWaylandWindow* window = data;
    mwinEvent event = {.type = mwin_eventCloseRequested};
    mwinPost(window->platform->context, window->slot, &event);
}

static void OnRepositioned(void* data, struct xdg_popup* popup, uint32_t token)
{
    (void)data;
    (void)popup;
    (void)token;
}

static const struct xdg_popup_listener s_popupListener = {
    OnConfigure,
    OnDone,
    OnRepositioned,
};

// A positioner for a place and size: the top left corner of the owner's
// window geometry as the anchor, the popup below and right of it by the
// place, slid and flipped onto the output. The anchor rectangle may not
// leave the owner's geometry; the offset may.
static struct xdg_positioner* Positioner(const mwinWaylandWindow* window, mwinPosition place,
                                         mwinSize size)
{
    const mwinWaylandApi* api = &window->platform->api;
    struct wl_proxy* positioner = mwinWlRequest(
        api, window->platform->wmBase, XDG_WM_BASE_CREATE_POSITIONER, &xdg_positioner_interface, 0);
    uint32_t version = mwinWlVersion(api, positioner);
    int32_t width = (int32_t)lroundf(size.width);
    int32_t height = (int32_t)lroundf(size.height);
    api->proxyMarshalFlags(positioner, XDG_POSITIONER_SET_SIZE, nullptr, version, 0,
                           width > 0 ? width : 1, height > 0 ? height : 1);
    api->proxyMarshalFlags(positioner, XDG_POSITIONER_SET_ANCHOR_RECT, nullptr, version, 0, 0, 0, 1,
                           1);
    api->proxyMarshalFlags(positioner, XDG_POSITIONER_SET_ANCHOR, nullptr, version, 0,
                           XDG_POSITIONER_ANCHOR_TOP_LEFT);
    api->proxyMarshalFlags(positioner, XDG_POSITIONER_SET_GRAVITY, nullptr, version, 0,
                           XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
    api->proxyMarshalFlags(positioner, XDG_POSITIONER_SET_OFFSET, nullptr, version, 0,
                           (int32_t)lroundf(place.x),
                           (int32_t)lroundf(place.y) + CaptionOf(window));
    api->proxyMarshalFlags(positioner, XDG_POSITIONER_SET_CONSTRAINT_ADJUSTMENT, nullptr, version,
                           0,
                           XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X |
                               XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y |
                               XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X |
                               XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y);
    if (version >= REPOSITION_VERSION)
    {
        // The compositor places it again as its owner moves or resizes.
        api->proxyMarshalFlags(positioner, XDG_POSITIONER_SET_REACTIVE, nullptr, version, 0);
    }
    return (struct xdg_positioner*)positioner;
}

static void DestroyPositioner(const mwinWaylandApi* api, struct xdg_positioner* positioner)
{
    (void)mwinWlRequest(api, positioner, XDG_POSITIONER_DESTROY, nullptr, WL_MARSHAL_FLAG_DESTROY);
}

void mwinWaylandMakePopup(mwinWaylandWindow* window)
{
    mwinWaylandPlatform* platform = window->platform;
    const mwinWaylandApi* api = &platform->api;
    const mwinWindow* core = &platform->context->windows[window->slot];
    struct xdg_positioner* positioner = Positioner(window, core->def.position, core->def.size);
    struct wl_proxy* surface = (struct wl_proxy*)window->xdgSurface;
    window->popup = (struct xdg_popup*)api->proxyMarshalFlags(
        surface, XDG_SURFACE_GET_POPUP, &xdg_popup_interface, mwinWlVersion(api, surface), 0,
        nullptr, OwnerOf(window)->xdgSurface, positioner);
    DestroyPositioner(api, positioner);
    mwinWlListen(api, window->popup, &s_popupListener, window);
    window->placed = (mwinPosition){NAN, NAN};
    if (core->def.kind == mwin_windowMenu && platform->seat != nullptr &&
        platform->inputSerial != 0)
    {
        // Without a serial of recent input the compositor would dismiss
        // the menu at once; it shows without the keyboard then.
        api->proxyMarshalFlags((struct wl_proxy*)window->popup, XDG_POPUP_GRAB, nullptr,
                               mwinWlVersion(api, window->popup), 0, platform->seat,
                               platform->inputSerial);
    }
}

mwinOutcome mwinWaylandPlacePopup(mwinWaylandWindow* window, mwinPosition position, mwinSize size)
{
    const mwinWaylandApi* api = &window->platform->api;
    uint32_t version = mwinWlVersion(api, window->popup);
    if (version < REPOSITION_VERSION)
    {
        return mwin_outcomeUnsupported;
    }
    struct xdg_positioner* positioner = Positioner(window, position, size);
    window->repositions += 1;
    api->proxyMarshalFlags((struct wl_proxy*)window->popup, XDG_POPUP_REPOSITION, nullptr, version,
                           0, positioner, window->repositions);
    DestroyPositioner(api, positioner);
    return mwin_outcomeDone;
}

void mwinWaylandSettlePopup(mwinWaylandWindow* window)
{
    mwinPosition place = {(float)window->proposedX, (float)(window->proposedY - CaptionOf(window))};
    if (place.x == window->placed.x && place.y == window->placed.y)
    {
        return;
    }
    window->placed = place;
    mwinEvent event = {.type = mwin_eventMoved};
    event.data.position = place;
    mwinPost(window->platform->context, window->slot, &event);
}

void mwinWaylandDestroyPopup(mwinWaylandWindow* window)
{
    (void)mwinWlRequest(&window->platform->api, window->popup, XDG_POPUP_DESTROY, nullptr,
                        WL_MARSHAL_FLAG_DESTROY);
}
