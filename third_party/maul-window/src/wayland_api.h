// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// libwayland-client and libwayland-cursor, opened at run time (mwin-0005), and
// the protocols the backend speaks. The functions are reached through a
// table each context loads for itself, so the library keeps no
// process-wide state. The
// protocol headers' inline request wrappers call libwayland by name,
// which the library never links: they are poisoned below, and requests
// go through mwinWlRequest and the table instead.

#ifndef MAUL_WINDOW_SRC_WAYLAND_API_H
#define MAUL_WINDOW_SRC_WAYLAND_API_H

// clang-format off
#include <wayland-names.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-cursor.h>
#include <cursor-shape-v1-protocol.h>
#include <fractional-scale-v1-protocol.h>
#include <idle-inhibit-unstable-v1-protocol.h>
#include <pointer-constraints-unstable-v1-protocol.h>
#include <relative-pointer-unstable-v1-protocol.h>
#include <tablet-unstable-v2-protocol.h>
#include <text-input-unstable-v3-protocol.h>
#include <viewporter-protocol.h>
#include <xdg-activation-v1-protocol.h>
#include <xdg-decoration-unstable-v1-protocol.h>
#include <xdg-shell-protocol.h>
#include <xdg-toplevel-icon-v1-protocol.h>
// clang-format on

#include "maul-window/base.h"

#pragma GCC poison wl_proxy_marshal_flags wl_proxy_add_listener wl_proxy_get_version
#pragma GCC poison wl_proxy_destroy wl_proxy_get_user_data wl_proxy_set_user_data

typedef struct mwinWaylandApi
{
    void* library;
    struct wl_display* (*displayConnect)(const char* name);
    void (*displayDisconnect)(struct wl_display* display);
    int (*displayGetFd)(struct wl_display* display);
    int (*displayFlush)(struct wl_display* display);
    int (*displayDispatchPending)(struct wl_display* display);
    int (*displayPrepareRead)(struct wl_display* display);
    int (*displayReadEvents)(struct wl_display* display);
    void (*displayCancelRead)(struct wl_display* display);
    int (*displayRoundtrip)(struct wl_display* display);
    int (*displayGetError)(struct wl_display* display);
    struct wl_proxy* (*proxyMarshalFlags)(struct wl_proxy* proxy, uint32_t opcode,
                                          const struct wl_interface* interface, uint32_t version,
                                          uint32_t flags, ...);
    int (*proxyAddListener)(struct wl_proxy* proxy, void (**implementation)(void), void* data);
    uint32_t (*proxyGetVersion)(struct wl_proxy* proxy);
    void (*proxyDestroy)(struct wl_proxy* proxy);
    // libwayland-cursor, for compositors without cursor shapes; its
    // library is NULL where it is missing.
    void* cursorLibrary;
    typeof(wl_cursor_theme_load)* cursorThemeLoad;
    typeof(wl_cursor_theme_destroy)* cursorThemeDestroy;
    typeof(wl_cursor_theme_get_cursor)* cursorThemeGetCursor;
    typeof(wl_cursor_image_get_buffer)* cursorImageGetBuffer;
} mwinWaylandApi;

// Opens libwayland-client and fills the table: mwin_errorUnsupported
// when the library or a function is missing. libwayland-cursor is
// opened too when it is there.
mwinResult mwinLoadWayland(mwinWaylandApi* api);

// Closes what mwinLoadWayland opened.
void mwinUnloadWayland(mwinWaylandApi* api);

// Sends a request without arguments, or whose one argument is the new
// object (returned, of the created interface, or NULL for none). A
// destructor request (flags WL_MARSHAL_FLAG_DESTROY) destroys the proxy.
void* mwinWlRequest(const mwinWaylandApi* api, void* proxy, uint32_t opcode,
                    const struct wl_interface* created, uint32_t flags);

// A request whose arguments are the new object and one other object, in
// that order: get_xdg_surface, get_toplevel_decoration and the like.
void* mwinWlCreateFor(const mwinWaylandApi* api, void* proxy, uint32_t opcode,
                      const struct wl_interface* created, void* argument);

// Sets the listener of a proxy; data comes back to each handler.
void mwinWlListen(const mwinWaylandApi* api, void* proxy, const void* listener, void* data);

// The version of the object a proxy stands for.
uint32_t mwinWlVersion(const mwinWaylandApi* api, void* proxy);

#endif // MAUL_WINDOW_SRC_WAYLAND_API_H
