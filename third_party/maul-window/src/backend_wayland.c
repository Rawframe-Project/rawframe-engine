// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland backend: the connection, the globals and the pump. A
// context opens libwayland-client for itself, connects to the display
// WAYLAND_DISPLAY names, and binds the globals it knows at the versions
// it implements. wl_compositor and xdg_wm_base are required; the
// decoration, fractional scale and viewporter managers are used when the
// compositor has them.
//
// The pump never waits: it sends what the program asked for, reads what
// the compositor sent if any is there, and dispatches it, which posts
// the records. A connection that fails stops the loop.

#include "allocator.h"
#include "backend.h"
#include "core.h"
#include "key_reach.h"
#include "wayland.h"
#include "wayland_api.h"
#include "wayland_clipboard.h"
#include "wayland_cursor.h"
#include "wayland_drop.h"
#include "wayland_keyboard.h"
#include "wayland_output.h"
#include "wayland_seat.h"
#include "wayland_tablet.h"
#include "wayland_text.h"
#include "wayland_window.h"
#include "xkb_api.h"

#include <poll.h>
#include <string.h>

// The highest version of each global the backend implements.
#define COMPOSITOR_VERSION       6
#define WM_BASE_VERSION          6
#define DECORATION_VERSION       1
#define FRACTIONAL_SCALE_VERSION 1
#define VIEWPORTER_VERSION       1
#define IDLE_INHIBIT_VERSION     1
#define TOPLEVEL_ICON_VERSION    1
#define COLOR_MANAGER_VERSION    1
#define TABLET_VERSION           1
#define CURSOR_SHAPE_VERSION     1
#define CONSTRAINTS_VERSION      1
#define RELATIVE_VERSION         1
#define SHM_VERSION              1
#define TEXT_INPUT_VERSION       1
#define ACTIVATION_VERSION       1
#define SUBCOMPOSITOR_VERSION    1

static mwinWaylandPlatform* PlatformOf(const mwinContext* context)
{
    return (mwinWaylandPlatform*)context->backendData;
}

// Where the platform block's parts lie, laid out with checked
// arithmetic: the platform, its windows and outputs, the title being
// set, and the text input's preedit and commit.
typedef struct PlatformParts
{
    mwinLayout layout;
    size_t windows;
    size_t outputs;
    size_t title;
    size_t preedit;
    size_t commit;
} PlatformParts;

static PlatformParts PartsOf(const mwinLimits* limits)
{
    PlatformParts parts = {0};
    mwinLayout* layout = &parts.layout;
    (void)mwinLayoutAdd(layout, 1, sizeof(mwinWaylandPlatform), alignof(mwinWaylandPlatform));
    parts.windows = mwinLayoutAdd(layout, limits->windows, sizeof(mwinWaylandWindow),
                                  alignof(mwinWaylandWindow));
    parts.outputs = mwinLayoutAdd(layout, limits->monitors, sizeof(mwinWaylandOutput),
                                  alignof(mwinWaylandOutput));
    parts.title = mwinLayoutAdd(layout, (size_t)limits->titleBytes + 1, 1, 1);
    parts.preedit = mwinLayoutAdd(layout, limits->textBytesPerWindow, 1, 1);
    parts.commit = mwinLayoutAdd(layout, limits->textBytesPerWindow, 1, 1);
    return parts;
}

static size_t PlatformBytes(const mwinContext* context)
{
    return PartsOf(&context->limits).layout.size;
}

static void OnPing(void* data, struct xdg_wm_base* wmBase, uint32_t serial)
{
    const mwinWaylandPlatform* platform = data;
    const mwinWaylandApi* api = &platform->api;
    api->proxyMarshalFlags((struct wl_proxy*)wmBase, XDG_WM_BASE_PONG, nullptr,
                           mwinWlVersion(api, wmBase), 0, serial);
}

static const struct xdg_wm_base_listener s_wmBaseListener = {
    OnPing,
};

static void* Bind(mwinWaylandPlatform* platform, uint32_t name,
                  const struct wl_interface* interface, uint32_t offered, uint32_t implemented)
{
    uint32_t version = offered < implemented ? offered : implemented;
    return platform->api.proxyMarshalFlags((struct wl_proxy*)platform->registry, WL_REGISTRY_BIND,
                                           interface, version, 0, name, interface->name, version,
                                           nullptr);
}

// Binds a protocol extension the backend uses: false for another
// interface.
static bool BindExtension(mwinWaylandPlatform* platform, uint32_t name, const char* interface,
                          uint32_t version)
{
    if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0)
    {
        platform->decorations = Bind(platform, name, &zxdg_decoration_manager_v1_interface, version,
                                     DECORATION_VERSION);
    }
    else if (strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0)
    {
        platform->fractionalScale = Bind(platform, name, &wp_fractional_scale_manager_v1_interface,
                                         version, FRACTIONAL_SCALE_VERSION);
    }
    else if (strcmp(interface, wp_viewporter_interface.name) == 0)
    {
        platform->viewporter =
            Bind(platform, name, &wp_viewporter_interface, version, VIEWPORTER_VERSION);
    }
    else if (strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0)
    {
        platform->cursorShapes = Bind(platform, name, &wp_cursor_shape_manager_v1_interface,
                                      version, CURSOR_SHAPE_VERSION);
    }
    else if (strcmp(interface, zwp_pointer_constraints_v1_interface.name) == 0)
    {
        platform->constraints = Bind(platform, name, &zwp_pointer_constraints_v1_interface, version,
                                     CONSTRAINTS_VERSION);
    }
    else if (strcmp(interface, zwp_relative_pointer_manager_v1_interface.name) == 0)
    {
        platform->relativePointers = Bind(
            platform, name, &zwp_relative_pointer_manager_v1_interface, version, RELATIVE_VERSION);
    }
    else if (strcmp(interface, xdg_activation_v1_interface.name) == 0)
    {
        platform->activation =
            Bind(platform, name, &xdg_activation_v1_interface, version, ACTIVATION_VERSION);
    }
    else if (strcmp(interface, zwp_text_input_manager_v3_interface.name) == 0)
    {
        platform->textInputs =
            Bind(platform, name, &zwp_text_input_manager_v3_interface, version, TEXT_INPUT_VERSION);
    }
    else if (strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name) == 0)
    {
        platform->idleInhibits = Bind(platform, name, &zwp_idle_inhibit_manager_v1_interface,
                                      version, IDLE_INHIBIT_VERSION);
    }
    else if (strcmp(interface, zwp_primary_selection_device_manager_v1_interface.name) == 0 &&
             platform->clipboard.primaryManager == nullptr)
    {
        mwinWaylandBindPrimaryManager(platform, name, version);
    }
    else if (strcmp(interface, xdg_toplevel_icon_manager_v1_interface.name) == 0)
    {
        platform->toplevelIcons = Bind(platform, name, &xdg_toplevel_icon_manager_v1_interface,
                                       version, TOPLEVEL_ICON_VERSION);
    }
    else if (strcmp(interface, zwp_tablet_manager_v2_interface.name) == 0 &&
             platform->tablets.manager == nullptr)
    {
        platform->tablets.manager =
            Bind(platform, name, &zwp_tablet_manager_v2_interface, version, TABLET_VERSION);
        mwinWaylandAttachTablets(platform);
    }
    else if (strcmp(interface, wp_color_manager_v1_interface.name) == 0 &&
             platform->colorManager == nullptr)
    {
        platform->colorManager =
            Bind(platform, name, &wp_color_manager_v1_interface, version, COLOR_MANAGER_VERSION);
        mwinWaylandWatchColors(platform);
    }
    else
    {
        return false;
    }
    return true;
}

static void OnGlobal(void* data, struct wl_registry* registry, uint32_t name, const char* interface,
                     uint32_t version)
{
    (void)registry;
    mwinWaylandPlatform* platform = data;
    if (BindExtension(platform, name, interface, version))
    {
        return;
    }
    if (strcmp(interface, wl_compositor_interface.name) == 0 && platform->compositor == nullptr)
    {
        platform->compositor =
            Bind(platform, name, &wl_compositor_interface, version, COMPOSITOR_VERSION);
    }
    else if (strcmp(interface, xdg_wm_base_interface.name) == 0 && platform->wmBase == nullptr)
    {
        platform->wmBase = Bind(platform, name, &xdg_wm_base_interface, version, WM_BASE_VERSION);
        mwinWlListen(&platform->api, platform->wmBase, &s_wmBaseListener, platform);
    }
    else if (strcmp(interface, wl_subcompositor_interface.name) == 0)
    {
        platform->subcompositor =
            Bind(platform, name, &wl_subcompositor_interface, version, SUBCOMPOSITOR_VERSION);
    }
    else if (strcmp(interface, wl_shm_interface.name) == 0 && platform->shm == nullptr)
    {
        platform->shm = Bind(platform, name, &wl_shm_interface, version, SHM_VERSION);
    }
    else if (strcmp(interface, wl_output_interface.name) == 0)
    {
        mwinWaylandBindOutput(platform, name, version);
    }
    else if (strcmp(interface, wl_seat_interface.name) == 0)
    {
        mwinWaylandBindSeat(platform, name, version);
        mwinWaylandAttachTablets(platform);
    }
    else if (strcmp(interface, wl_data_device_manager_interface.name) == 0 &&
             platform->clipboard.manager == nullptr)
    {
        mwinWaylandBindDataManager(platform, name, version);
    }
}

static void OnGlobalRemove(void* data, struct wl_registry* registry, uint32_t name)
{
    (void)registry;
    mwinWaylandPlatform* platform = data;
    mwinWaylandRemoveOutput(platform, name);
    if (platform->seat != nullptr && platform->seatName == name)
    {
        mwinWaylandReleaseTablets(platform);
    }
    mwinWaylandRemoveSeat(platform, name);
}

static const struct wl_registry_listener s_registryListener = {
    OnGlobal,
    OnGlobalRemove,
};

// Destroys a global's proxy with its destructor request, or without one
// for an interface that has none.
static void DestroyGlobal(const mwinWaylandApi* api, void* proxy, int32_t destructor)
{
    if (proxy == nullptr)
    {
        return;
    }
    if (destructor < 0)
    {
        api->proxyDestroy((struct wl_proxy*)proxy);
        return;
    }
    (void)mwinWlRequest(api, proxy, (uint32_t)destructor, nullptr, WL_MARSHAL_FLAG_DESTROY);
}

static void Disconnect(mwinWaylandPlatform* platform)
{
    const mwinWaylandApi* api = &platform->api;
    if (platform->display != nullptr)
    {
        mwinWaylandReleaseTablets(platform);
        DestroyGlobal(api, platform->tablets.manager, ZWP_TABLET_MANAGER_V2_DESTROY);
        mwinWaylandReleaseSeat(platform);
        DestroyGlobal(api, platform->clipboard.manager, -1);
        DestroyGlobal(api, platform->clipboard.primaryManager,
                      ZWP_PRIMARY_SELECTION_DEVICE_MANAGER_V1_DESTROY);
        mwinWaylandReleaseCursorTheme(platform);
        mwinWaylandReleaseOutputs(platform);
        DestroyGlobal(api, platform->colorManager, WP_COLOR_MANAGER_V1_DESTROY);
        DestroyGlobal(api, platform->shm, -1);
        DestroyGlobal(api, platform->subcompositor, WL_SUBCOMPOSITOR_DESTROY);
        DestroyGlobal(api, platform->idleInhibits, ZWP_IDLE_INHIBIT_MANAGER_V1_DESTROY);
        DestroyGlobal(api, platform->toplevelIcons, XDG_TOPLEVEL_ICON_MANAGER_V1_DESTROY);
        DestroyGlobal(api, platform->textInputs, ZWP_TEXT_INPUT_MANAGER_V3_DESTROY);
        DestroyGlobal(api, platform->activation, XDG_ACTIVATION_V1_DESTROY);
        DestroyGlobal(api, platform->relativePointers, ZWP_RELATIVE_POINTER_MANAGER_V1_DESTROY);
        DestroyGlobal(api, platform->constraints, ZWP_POINTER_CONSTRAINTS_V1_DESTROY);
        DestroyGlobal(api, platform->cursorShapes, WP_CURSOR_SHAPE_MANAGER_V1_DESTROY);
        DestroyGlobal(api, platform->viewporter, WP_VIEWPORTER_DESTROY);
        DestroyGlobal(api, platform->fractionalScale, WP_FRACTIONAL_SCALE_MANAGER_V1_DESTROY);
        DestroyGlobal(api, platform->decorations, ZXDG_DECORATION_MANAGER_V1_DESTROY);
        DestroyGlobal(api, platform->wmBase, XDG_WM_BASE_DESTROY);
        DestroyGlobal(api, platform->compositor, -1);
        DestroyGlobal(api, platform->registry, -1);
        (void)api->displayFlush(platform->display);
        api->displayDisconnect(platform->display);
    }
    mwinUnloadWayland(&platform->api);
    mwinUnloadXkb(&platform->xkb);
}

// Connects, binds the globals, and waits for the outputs' first facts.
static mwinResult Connect(mwinWaylandPlatform* platform)
{
    mwinResult status = mwinLoadWayland(&platform->api);
    if (status != mwin_success)
    {
        return status;
    }
    // Without libxkbcommon the windows work and no keyboard is used.
    (void)mwinLoadXkb(&platform->xkb);
    const mwinWaylandApi* api = &platform->api;
    platform->display = api->displayConnect(nullptr);
    if (platform->display == nullptr)
    {
        return mwin_errorPlatform;
    }
    platform->registry =
        mwinWlRequest(api, platform->display, WL_DISPLAY_GET_REGISTRY, &wl_registry_interface, 0);
    mwinWlListen(api, platform->registry, &s_registryListener, platform);
    // The first round trip brings the globals, the second the facts of
    // the objects bound to them; with the color manager a third brings
    // what the outputs' image descriptions tell, so that a monitor has
    // its HDR facts from the start.
    for (int trip = 0; trip < (platform->colorManager != nullptr ? 3 : 2); trip++)
    {
        if (api->displayRoundtrip(platform->display) < 0)
        {
            return mwin_errorPlatform;
        }
    }
    mwinWaylandAttachText(platform);
    mwinWaylandAttachClipboard(platform);
    return platform->compositor != nullptr && platform->wmBase != nullptr ? mwin_success
                                                                          : mwin_errorUnsupported;
}

static void Stop(mwinContext* context)
{
    mwinWaylandPlatform* platform = PlatformOf(context);
    for (uint32_t i = 0; i < context->limits.windows; i++)
    {
        if (platform->windows[i].surface != nullptr)
        {
            mwinWaylandDestroyWindow(context, i);
        }
    }
    Disconnect(platform);
    mwinLinuxServicesStop(&platform->services);
#ifdef MAUL_WINDOW_GAMEPAD
    mwinLinuxPadsStop(&platform->pads);
#endif
    mwinRelease(&context->allocator, platform, PlatformBytes(context), alignof(max_align_t));
    context->backendData = nullptr;
}

static mwinResult Start(mwinContext* context)
{
    PlatformParts parts = PartsOf(&context->limits);
    unsigned char* block =
        parts.layout.overflow
            ? nullptr
            : mwinAllocate(&context->allocator, parts.layout.size, alignof(max_align_t));
    if (block == nullptr)
    {
        return mwin_errorCapacity;
    }
    memset(block, 0, parts.layout.size);
    mwinWaylandPlatform* platform = (mwinWaylandPlatform*)block;
    platform->windows = (mwinWaylandWindow*)(block + parts.windows);
    platform->outputs = (mwinWaylandOutput*)(block + parts.outputs);
    platform->title = (char*)(block + parts.title);
    platform->text.preedit.bytes = (char*)(block + parts.preedit);
    platform->text.commit.bytes = (char*)(block + parts.commit);
    platform->context = context;
    platform->keyboard.focus = -1;
    platform->pointer.focus = -1;
    platform->text.focus = -1;
    mwinWaylandInitClipboard(&platform->clipboard);
    mwinWaylandInitDrag(&platform->drag);
    mwinLinuxServicesStart(&platform->services, context);
    context->backendData = platform;
    mwinResult status = Connect(platform);
#ifdef MAUL_WINDOW_GAMEPAD
    if (status == mwin_success && !mwinLinuxPadsStart(&platform->pads, context))
    {
        status = mwin_errorCapacity;
    }
#endif
    if (status != mwin_success)
    {
        Stop(context);
    }
    return status;
}

// Sends what waits, then reads and dispatches what arrived, without
// waiting for more.
static void Pump(mwinContext* context)
{
    mwinWaylandPlatform* platform = PlatformOf(context);
    const mwinWaylandApi* api = &platform->api;
    struct wl_display* display = platform->display;
    while (api->displayPrepareRead(display) != 0)
    {
        if (api->displayDispatchPending(display) < 0)
        {
            break;
        }
    }
    (void)api->displayFlush(display);
    struct pollfd descriptor = {.fd = api->displayGetFd(display), .events = POLLIN};
    if (poll(&descriptor, 1, 0) > 0)
    {
        (void)api->displayReadEvents(display);
    }
    else
    {
        api->displayCancelRead(display);
    }
    (void)api->displayDispatchPending(display);
    mwinWaylandRepeatKeys(platform);
    mwinWaylandPumpClipboard(platform, mwinMonotonicNow());
    mwinWaylandPumpDrag(platform, mwinMonotonicNow());
    mwinLinuxServicesPump(&platform->services, mwinMonotonicNow(),
                          platform->idleInhibits == nullptr && mwinWantsAwake(context));
#ifdef MAUL_WINDOW_GAMEPAD
    mwinLinuxPadsPump(&platform->pads);
#endif
    if (api->displayGetError(display) != 0)
    {
        platform->failed = true;
        context->stopping = true;
    }
}

static mwinResult Run(mwinContext* context)
{
    mwinResult status = mwinRunLoop(context, Pump);
    return PlatformOf(context)->failed ? mwin_errorPlatform : status;
}

static uint64_t Now(const mwinContext* context)
{
    (void)context;
    return mwinMonotonicNow();
}

static mwinKey MapKeyCode(const mwinContext* context, mwinKeyCode code)
{
    return mwinWaylandMapKeyCode(PlatformOf(context), code);
}

static mwinResult KeyboardLayout(const mwinContext* context, char* buffer, size_t capacity,
                                 size_t* lengthOut)
{
    return mwinWaylandKeyboardLayout(PlatformOf(context), buffer, capacity, lengthOut);
}

static void NativeHandles(const mwinContext* context, uint32_t slot, mwinNativeHandles* out)
{
    const mwinWaylandPlatform* platform = PlatformOf(context);
    out->platform = mwin_platformWayland;
    out->handles.wayland.display = platform->display;
    out->handles.wayland.surface = platform->windows[slot].surface;
}

static mwinResult Rumble(mwinContext* context, uint32_t slot, float low, float high,
                         uint32_t durationMs)
{
#ifdef MAUL_WINDOW_GAMEPAD
    return mwinLinuxPadsRumble(&PlatformOf(context)->pads, slot, low, high, durationMs);
#else
    (void)context;
    (void)slot;
    (void)low;
    (void)high;
    (void)durationMs;
    return mwin_errorUnsupported;
#endif
}

const mwinBackendOps mwinWaylandBackend = {
    Start,
    Stop,
    Run,
    mwinWaylandCreateWindow,
    mwinWaylandDestroyWindow,
    mwinWaylandSubmit,
    Now,
    MapKeyCode,
    KeyboardLayout,
    NativeHandles,
    Rumble,
    mwinWaylandReleaseCursor,
    nullptr,
#ifdef MAUL_WINDOW_GAMEPAD
    mwinLinuxPadsSetMotion,
#else
    nullptr,
#endif
    mwinLinuxKeyReach,
};
