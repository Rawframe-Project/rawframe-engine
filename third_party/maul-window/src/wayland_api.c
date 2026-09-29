// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Opening libwayland-client and the request helpers.

#include "wayland_api.h"

#include <dlfcn.h>
#include <string.h>

// A function of the library by name, or NULL. The pointer comes back as
// data from dlsym and is copied, not cast, into the function pointer.
static bool Find(void* library, const char* name, void* function, size_t size)
{
    void* symbol = dlsym(library, name);
    if (symbol == nullptr)
    {
        return false;
    }
    memcpy(function, (const void*)&symbol, size);
    return true;
}

#define FIND(field, name) Find(api->library, name, (void*)&api->field, sizeof(api->field))

mwinResult mwinLoadWayland(mwinWaylandApi* api)
{
    memset(api, 0, sizeof(*api));
    api->library = dlopen("libwayland-client.so.0", RTLD_NOW | RTLD_LOCAL);
    if (api->library == nullptr)
    {
        return mwin_errorUnsupported;
    }
    bool found =
        FIND(displayConnect, "wl_display_connect") &&
        FIND(displayDisconnect, "wl_display_disconnect") &&
        FIND(displayGetFd, "wl_display_get_fd") && FIND(displayFlush, "wl_display_flush") &&
        FIND(displayDispatchPending, "wl_display_dispatch_pending") &&
        FIND(displayPrepareRead, "wl_display_prepare_read") &&
        FIND(displayReadEvents, "wl_display_read_events") &&
        FIND(displayCancelRead, "wl_display_cancel_read") &&
        FIND(displayRoundtrip, "wl_display_roundtrip") &&
        FIND(displayGetError, "wl_display_get_error") &&
        FIND(proxyMarshalFlags, "wl_proxy_marshal_flags") &&
        FIND(proxyAddListener, "wl_proxy_add_listener") &&
        FIND(proxyGetVersion, "wl_proxy_get_version") && FIND(proxyDestroy, "wl_proxy_destroy");
    if (!found)
    {
        // Older than 1.20, which added wl_proxy_marshal_flags.
        mwinUnloadWayland(api);
        return mwin_errorUnsupported;
    }
    api->cursorLibrary = dlopen("libwayland-cursor.so.0", RTLD_NOW | RTLD_LOCAL);
    bool cursors = api->cursorLibrary != nullptr &&
                   Find(api->cursorLibrary, "wl_cursor_theme_load", (void*)&api->cursorThemeLoad,
                        sizeof(api->cursorThemeLoad)) &&
                   Find(api->cursorLibrary, "wl_cursor_theme_destroy",
                        (void*)&api->cursorThemeDestroy, sizeof(api->cursorThemeDestroy)) &&
                   Find(api->cursorLibrary, "wl_cursor_theme_get_cursor",
                        (void*)&api->cursorThemeGetCursor, sizeof(api->cursorThemeGetCursor)) &&
                   Find(api->cursorLibrary, "wl_cursor_image_get_buffer",
                        (void*)&api->cursorImageGetBuffer, sizeof(api->cursorImageGetBuffer));
    if (!cursors && api->cursorLibrary != nullptr)
    {
        dlclose(api->cursorLibrary);
        api->cursorLibrary = nullptr;
    }
    return mwin_success;
}

void mwinUnloadWayland(mwinWaylandApi* api)
{
    if (api->cursorLibrary != nullptr)
    {
        dlclose(api->cursorLibrary);
    }
    if (api->library != nullptr)
    {
        dlclose(api->library);
    }
    memset(api, 0, sizeof(*api));
}

uint32_t mwinWlVersion(const mwinWaylandApi* api, void* proxy)
{
    return api->proxyGetVersion((struct wl_proxy*)proxy);
}

void* mwinWlRequest(const mwinWaylandApi* api, void* proxy, uint32_t opcode,
                    const struct wl_interface* created, uint32_t flags)
{
    struct wl_proxy* object = proxy;
    return api->proxyMarshalFlags(object, opcode, created, api->proxyGetVersion(object), flags,
                                  nullptr);
}

void* mwinWlCreateFor(const mwinWaylandApi* api, void* proxy, uint32_t opcode,
                      const struct wl_interface* created, void* argument)
{
    struct wl_proxy* object = proxy;
    return api->proxyMarshalFlags(object, opcode, created, api->proxyGetVersion(object), 0, nullptr,
                                  argument);
}

void mwinWlListen(const mwinWaylandApi* api, void* proxy, const void* listener, void* data)
{
    // The listener structs are arrays of function pointers.
    void (**implementation)(void) = nullptr;
    memcpy((void*)&implementation, (const void*)&listener, sizeof(implementation));
    (void)api->proxyAddListener((struct wl_proxy*)proxy, implementation, data);
}
