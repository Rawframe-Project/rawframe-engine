// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Wayland windows.

#include "wayland_window.h"

#include "wayland_activation.h"
#include "wayland_clipboard.h"
#include "wayland_cursor.h"
#include "wayland_frame.h"
#include "wayland_icon.h"
#include "wayland_keyboard.h"
#include "wayland_output.h"
#include "wayland_pointer.h"
#include "wayland_popup.h"
#include "wayland_tablet.h"
#include "wayland_text.h"

#include <math.h>
#include <string.h>

static mwinWaylandPlatform* PlatformOf(const mwinContext* context)
{
    return (mwinWaylandPlatform*)context->backendData;
}

static float ScaleOf(const mwinWaylandWindow* window)
{
    return window->scale120 != 0 ? (float)window->scale120 / 120.0f : (float)window->bufferScale;
}

static mwinPixelSize PixelsOf(const mwinWaylandWindow* window)
{
    float scale = ScaleOf(window);
    return (mwinPixelSize){(uint32_t)lroundf(window->size.width * scale),
                           (uint32_t)lroundf(window->size.height * scale)};
}

static void PostType(mwinWaylandWindow* window, mwinEventType type)
{
    mwinEvent event = {0};
    event.type = type;
    event.timeNs = mwinMonotonicNow();
    mwinPost(window->platform->context, window->slot, &event);
}

// Sets the surface's logical size and scale for its next commit, which
// the program's renderer makes with a buffer of the new pixel size.
static void ApplySurfaceSize(mwinWaylandWindow* window)
{
    const mwinWaylandApi* api = &window->platform->api;
    if (window->viewport != nullptr)
    {
        api->proxyMarshalFlags((struct wl_proxy*)window->viewport, WP_VIEWPORT_SET_DESTINATION,
                               nullptr, mwinWlVersion(api, window->viewport), 0,
                               (int32_t)lroundf(window->size.width),
                               (int32_t)lroundf(window->size.height));
    }
    else
    {
        api->proxyMarshalFlags((struct wl_proxy*)window->surface, WL_SURFACE_SET_BUFFER_SCALE,
                               nullptr, mwinWlVersion(api, window->surface), 0,
                               window->bufferScale);
    }
}

// Reports the window's size and the pixel size its scale gives it.
static void PostSize(mwinWaylandWindow* window)
{
    mwinEvent event = {0};
    event.timeNs = mwinMonotonicNow();
    event.type = mwin_eventResized;
    event.data.size = window->size;
    mwinPost(window->platform->context, window->slot, &event);
    event.type = mwin_eventPixelSizeChanged;
    event.data.pixelSize = PixelsOf(window);
    mwinPost(window->platform->context, window->slot, &event);
}

static void PostScale(mwinWaylandWindow* window)
{
    mwinEvent event = {0};
    event.type = mwin_eventScaleChanged;
    event.timeNs = mwinMonotonicNow();
    event.data.scale = (mwinScaleChange){ScaleOf(window), window->size};
    mwinPost(window->platform->context, window->slot, &event);
}

static void PostMode(mwinWaylandWindow* window, mwinWindowMode mode)
{
    mwinEvent event = {0};
    event.type = mwin_eventModeChanged;
    event.timeNs = mwinMonotonicNow();
    event.data.mode = mode;
    mwinPost(window->platform->context, window->slot, &event);
}

// A new scale: from fractional scaling in 120ths, or an integer buffer
// scale, which fractional scaling overrides.
static void SetScale(mwinWaylandWindow* window, uint32_t scale120, int32_t bufferScale)
{
    float before = ScaleOf(window);
    window->scale120 = scale120;
    window->bufferScale = bufferScale;
    if (!window->configured || ScaleOf(window) == before)
    {
        return;
    }
    ApplySurfaceSize(window);
    PostScale(window);
    PostSize(window);
    mwinWaylandUpdateFrame(window->platform, window->slot);
}

static void OnEnter(void* data, struct wl_surface* surface, struct wl_output* output)
{
    (void)surface;
    mwinWaylandWindow* window = data;
    mwinContext* context = window->platform->context;
    int32_t monitor = mwinWaylandMonitorOf(window->platform, output);
    if (monitor >= 0 && window->configured)
    {
        mwinEvent event = {0};
        event.type = mwin_eventDisplayChanged;
        event.timeNs = mwinMonotonicNow();
        event.data.monitor = mwinMonitorIdOf(context, (uint32_t)monitor);
        mwinPost(context, window->slot, &event);
    }
}

static void OnLeave(void* data, struct wl_surface* surface, struct wl_output* output)
{
    (void)data;
    (void)surface;
    (void)output;
}

static void OnPreferredBufferScale(void* data, struct wl_surface* surface, int32_t factor)
{
    (void)surface;
    mwinWaylandWindow* window = data;
    if (factor > 0 && window->fractionalScale == nullptr)
    {
        SetScale(window, 0, factor);
    }
}

static void OnPreferredBufferTransform(void* data, struct wl_surface* surface, uint32_t transform)
{
    (void)data;
    (void)surface;
    (void)transform;
}

static const struct wl_surface_listener s_surfaceListener = {
    OnEnter,
    OnLeave,
    OnPreferredBufferScale,
    OnPreferredBufferTransform,
};

static void OnPreferredScale(void* data, struct wp_fractional_scale_v1* object, uint32_t scale)
{
    (void)object;
    mwinWaylandWindow* window = data;
    if (scale > 0)
    {
        SetScale(window, scale, 1);
    }
}

static const struct wp_fractional_scale_v1_listener s_fractionalScaleListener = {
    OnPreferredScale,
};

static void OnToplevelConfigure(void* data, struct xdg_toplevel* toplevel, int32_t width,
                                int32_t height, struct wl_array* states)
{
    (void)toplevel;
    mwinWaylandWindow* window = data;
    window->proposedWidth = width;
    window->proposedHeight = height;
    window->proposedMode = mwin_modeWindowed;
    window->proposedActivated = false;
    window->proposedSuspended = false;
    const uint32_t* state = states->data;
    for (size_t i = 0; i < states->size / sizeof(uint32_t); i++)
    {
        switch (state[i])
        {
        case XDG_TOPLEVEL_STATE_FULLSCREEN:
            window->proposedMode = mwin_modeBorderlessFullscreen;
            break;
        case XDG_TOPLEVEL_STATE_MAXIMIZED:
            if (window->proposedMode == mwin_modeWindowed)
            {
                window->proposedMode = mwin_modeMaximized;
            }
            break;
        case XDG_TOPLEVEL_STATE_ACTIVATED:
            window->proposedActivated = true;
            break;
        case XDG_TOPLEVEL_STATE_SUSPENDED:
            window->proposedSuspended = true;
            break;
        default:
            break;
        }
    }
}

static void OnClose(void* data, struct xdg_toplevel* toplevel)
{
    (void)toplevel;
    PostType(data, mwin_eventCloseRequested);
}

static void OnConfigureBounds(void* data, struct xdg_toplevel* toplevel, int32_t width,
                              int32_t height)
{
    (void)data;
    (void)toplevel;
    (void)width;
    (void)height;
}

static void OnCapabilities(void* data, struct xdg_toplevel* toplevel, struct wl_array* list)
{
    (void)data;
    (void)toplevel;
    (void)list;
}

static const struct xdg_toplevel_listener s_toplevelListener = {
    OnToplevelConfigure,
    OnClose,
    OnConfigureBounds,
    OnCapabilities,
};

// The first configure: the window is made, and what it is follows.
static void Establish(mwinWaylandWindow* window)
{
    mwinContext* context = window->platform->context;
    mwinWindow* core = &context->windows[window->slot];
    window->configured = true;
    ApplySurfaceSize(window);
    PostType(window, mwin_eventWindowCreated);
    PostScale(window);
    PostSize(window);
    PostMode(window, window->proposedMode);
    if (core->def.visible)
    {
        PostType(window, mwin_eventShown);
        mwinWaylandActivateAtStart(window);
    }
    int32_t request =
        mwinFindActiveRequest(core, context->limits.requestsPerWindow, mwin_requestCreate);
    if (request >= 0)
    {
        mwinComplete(context, window->slot, (uint32_t)request, mwin_outcomeDone);
    }
}

// A later configure: what changed is reported, and a waiting mode
// request is answered, done when the compositor granted the mode.
static void Reconfigure(mwinWaylandWindow* window, bool resized)
{
    mwinContext* context = window->platform->context;
    const mwinWindow* core = &context->windows[window->slot];
    if (resized)
    {
        ApplySurfaceSize(window);
        PostSize(window);
    }
    if (window->minimized && window->proposedActivated)
    {
        window->minimized = false;
    }
    if (window->proposedMode != core->state.mode && !window->minimized)
    {
        PostMode(window, window->proposedMode);
    }
    if (window->modeRequest >= 0)
    {
        const mwinRequest* request = &core->requests[window->modeRequest];
        mwinComplete(context, window->slot, (uint32_t)window->modeRequest,
                     request->value.mode == window->proposedMode ? mwin_outcomeDone
                                                                 : mwin_outcomeDenied);
        window->modeRequest = -1;
    }
}

static void OnSurfaceConfigure(void* data, struct xdg_surface* surface, uint32_t serial)
{
    mwinWaylandWindow* window = data;
    const mwinWaylandApi* api = &window->platform->api;
    const mwinWindow* core = &window->platform->context->windows[window->slot];
    api->proxyMarshalFlags((struct wl_proxy*)surface, XDG_SURFACE_ACK_CONFIGURE, nullptr,
                           mwinWlVersion(api, surface), 0, serial);
    // The compositor sizes the window geometry, which holds a frame's
    // caption above the content.
    int32_t height = window->proposedHeight -
                     mwinWaylandCaptionOf(window->platform, window, window->proposedMode);
    bool resized = window->proposedWidth > 0 && height > 0 &&
                   ((float)window->proposedWidth != window->size.width ||
                    (float)height != window->size.height);
    if (resized)
    {
        window->size = (mwinSize){(float)window->proposedWidth, (float)height};
    }
    if (!window->configured)
    {
        Establish(window);
    }
    else
    {
        Reconfigure(window, resized);
    }
    if (window->popup != nullptr)
    {
        mwinWaylandSettlePopup(window);
    }
    if (window->proposedActivated != core->state.focused)
    {
        PostType(window, window->proposedActivated ? mwin_eventFocusGained : mwin_eventFocusLost);
    }
    if (window->proposedSuspended != core->state.occluded)
    {
        PostType(window, window->proposedSuspended ? mwin_eventOccluded : mwin_eventRevealed);
    }
    mwinWaylandUpdateFrame(window->platform, window->slot);
}

static const struct xdg_surface_listener s_xdgSurfaceListener = {
    OnSurfaceConfigure,
};

// The compositor chose who draws the decorations; the frame follows.
static void OnDecorationConfigure(void* data, struct zxdg_toplevel_decoration_v1* decoration,
                                  uint32_t mode)
{
    (void)decoration;
    mwinWaylandWindow* window = data;
    window->decorationMode = mode;
    mwinWaylandUpdateFrame(window->platform, window->slot);
}

static const struct zxdg_toplevel_decoration_v1_listener s_decorationListener = {
    OnDecorationConfigure,
};

// Sends a string request: a title or an application id.
static void SetString(const mwinWaylandApi* api, void* proxy, uint32_t opcode, const char* text)
{
    api->proxyMarshalFlags((struct wl_proxy*)proxy, opcode, nullptr, mwinWlVersion(api, proxy), 0,
                           text);
}

// Sends a title, which Wayland takes NUL-terminated.
static void SetTitle(mwinWaylandWindow* window, const char* title, size_t length)
{
    mwinWaylandPlatform* platform = window->platform;
    memcpy(platform->title, title, length);
    platform->title[length] = '\0';
    SetString(&platform->api, window->toplevel, XDG_TOPLEVEL_SET_TITLE, platform->title);
}

// Asks the compositor for a mode; the next configure answers.
static void RequestMode(mwinWaylandWindow* window, mwinWindowMode mode)
{
    const mwinWaylandApi* api = &window->platform->api;
    void* toplevel = window->toplevel;
    switch (mode)
    {
    case mwin_modeBorderlessFullscreen:
        (void)mwinWlCreateFor(api, toplevel, XDG_TOPLEVEL_SET_FULLSCREEN, nullptr, nullptr);
        break;
    case mwin_modeMaximized:
        (void)mwinWlRequest(api, toplevel, XDG_TOPLEVEL_UNSET_FULLSCREEN, nullptr, 0);
        (void)mwinWlRequest(api, toplevel, XDG_TOPLEVEL_SET_MAXIMIZED, nullptr, 0);
        break;
    case mwin_modeMinimized:
        (void)mwinWlRequest(api, toplevel, XDG_TOPLEVEL_SET_MINIMIZED, nullptr, 0);
        break;
    default:
        (void)mwinWlRequest(api, toplevel, XDG_TOPLEVEL_UNSET_FULLSCREEN, nullptr, 0);
        (void)mwinWlRequest(api, toplevel, XDG_TOPLEVEL_UNSET_MAXIMIZED, nullptr, 0);
        break;
    }
}

// The optional objects: server-side decorations, fractional scaling and
// the viewport.
static void AddExtensions(mwinWaylandWindow* window, mwinWindowStyle style)
{
    mwinWaylandPlatform* platform = window->platform;
    const mwinWaylandApi* api = &platform->api;
    if (platform->decorations != nullptr && window->toplevel != nullptr)
    {
        window->decoration = mwinWlCreateFor(
            api, platform->decorations, ZXDG_DECORATION_MANAGER_V1_GET_TOPLEVEL_DECORATION,
            &zxdg_toplevel_decoration_v1_interface, window->toplevel);
        mwinWlListen(api, window->decoration, &s_decorationListener, window);
        bool decorated =
            (style & mwin_styleDecorated) != 0 && (style & mwin_styleCustomChrome) == 0;
        uint32_t mode = decorated ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
                                  : ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
        api->proxyMarshalFlags((struct wl_proxy*)window->decoration,
                               ZXDG_TOPLEVEL_DECORATION_V1_SET_MODE, nullptr,
                               mwinWlVersion(api, window->decoration), 0, mode);
    }
    if (platform->viewporter == nullptr)
    {
        return;
    }
    window->viewport = mwinWlCreateFor(api, platform->viewporter, WP_VIEWPORTER_GET_VIEWPORT,
                                       &wp_viewport_interface, window->surface);
    if (platform->fractionalScale != nullptr)
    {
        window->fractionalScale = mwinWlCreateFor(
            api, platform->fractionalScale, WP_FRACTIONAL_SCALE_MANAGER_V1_GET_FRACTIONAL_SCALE,
            &wp_fractional_scale_v1_interface, window->surface);
        mwinWlListen(api, window->fractionalScale, &s_fractionalScaleListener, window);
    }
}

// Makes the window a toplevel: in front of its owner's toplevel if it
// has one.
static void MakeToplevel(mwinWaylandWindow* window, const mwinWindow* core)
{
    const mwinWaylandApi* api = &window->platform->api;
    window->toplevel = mwinWlRequest(api, window->xdgSurface, XDG_SURFACE_GET_TOPLEVEL,
                                     &xdg_toplevel_interface, 0);
    mwinWlListen(api, window->toplevel, &s_toplevelListener, window);
    if (core->def.owner.index1 != 0)
    {
        const mwinWaylandWindow* owner = &window->platform->windows[core->def.owner.index1 - 1];
        if (owner->toplevel != nullptr)
        {
            api->proxyMarshalFlags((struct wl_proxy*)window->toplevel, XDG_TOPLEVEL_SET_PARENT,
                                   nullptr, mwinWlVersion(api, window->toplevel), 0,
                                   owner->toplevel);
        }
    }
    SetTitle(window, core->title, core->titleLength);
    AddExtensions(window, core->def.style);
    if (core->def.mode != mwin_modeWindowed && core->def.mode != mwin_modeMinimized)
    {
        RequestMode(window, core->def.mode);
    }
}

void mwinWaylandCreateWindow(mwinContext* context, uint32_t slot)
{
    mwinWaylandPlatform* platform = PlatformOf(context);
    const mwinWaylandApi* api = &platform->api;
    const mwinWindow* core = &context->windows[slot];
    mwinWaylandWindow* window = &platform->windows[slot];
    *window = (mwinWaylandWindow){.platform = platform,
                                  .slot = slot,
                                  .size = core->def.size,
                                  .bufferScale = 1,
                                  .modeRequest = -1,
                                  .frame = {.hover = -1, .pressed = -1}};
    window->surface = mwinWlRequest(api, platform->compositor, WL_COMPOSITOR_CREATE_SURFACE,
                                    &wl_surface_interface, 0);
    mwinWlListen(api, window->surface, &s_surfaceListener, window);
    window->xdgSurface = mwinWlCreateFor(api, platform->wmBase, XDG_WM_BASE_GET_XDG_SURFACE,
                                         &xdg_surface_interface, window->surface);
    mwinWlListen(api, window->xdgSurface, &s_xdgSurfaceListener, window);
    if (core->def.kind != mwin_windowNormal)
    {
        mwinWaylandMakePopup(window);
        AddExtensions(window, core->def.style);
    }
    else
    {
        MakeToplevel(window, core);
    }
    // A commit without a buffer asks for the first configure.
    (void)mwinWlRequest(api, window->surface, WL_SURFACE_COMMIT, nullptr, 0);
}

void mwinWaylandDestroyWindow(mwinContext* context, uint32_t slot)
{
    mwinWaylandPlatform* platform = PlatformOf(context);
    const mwinWaylandApi* api = &platform->api;
    mwinWaylandWindow* window = &platform->windows[slot];
    mwinWaylandForgetKeyboardFocus(platform, slot);
    mwinWaylandForgetPointerFocus(platform, slot);
    mwinWaylandForgetToolFocus(platform, slot);
    mwinWaylandDropCursor(platform, slot);
    mwinWaylandForgetTextFocus(platform, slot);
    mwinWaylandDestroyFrame(platform, slot);
    mwinWaylandDropActivation(window);
    if (window->viewport != nullptr)
    {
        (void)mwinWlRequest(api, window->viewport, WP_VIEWPORT_DESTROY, nullptr,
                            WL_MARSHAL_FLAG_DESTROY);
    }
    if (window->inhibitor != nullptr)
    {
        (void)mwinWlRequest(api, window->inhibitor, ZWP_IDLE_INHIBITOR_V1_DESTROY, nullptr,
                            WL_MARSHAL_FLAG_DESTROY);
    }
    if (window->fractionalScale != nullptr)
    {
        (void)mwinWlRequest(api, window->fractionalScale, WP_FRACTIONAL_SCALE_V1_DESTROY, nullptr,
                            WL_MARSHAL_FLAG_DESTROY);
    }
    if (window->decoration != nullptr)
    {
        (void)mwinWlRequest(api, window->decoration, ZXDG_TOPLEVEL_DECORATION_V1_DESTROY, nullptr,
                            WL_MARSHAL_FLAG_DESTROY);
    }
    if (window->popup != nullptr)
    {
        mwinWaylandDestroyPopup(window);
    }
    else
    {
        (void)mwinWlRequest(api, window->toplevel, XDG_TOPLEVEL_DESTROY, nullptr,
                            WL_MARSHAL_FLAG_DESTROY);
    }
    (void)mwinWlRequest(api, window->xdgSurface, XDG_SURFACE_DESTROY, nullptr,
                        WL_MARSHAL_FLAG_DESTROY);
    (void)mwinWlRequest(api, window->surface, WL_SURFACE_DESTROY, nullptr, WL_MARSHAL_FLAG_DESTROY);
    *window = (mwinWaylandWindow){0};
}

// Sets the smallest and largest size, 0 for no bound. The compositor
// bounds the window geometry, which holds a frame's caption.
static void SetLimits(mwinWaylandWindow* window, mwinSize minimum, mwinSize maximum)
{
    const mwinWaylandApi* api = &window->platform->api;
    struct wl_proxy* toplevel = (struct wl_proxy*)window->toplevel;
    uint32_t version = mwinWlVersion(api, toplevel);
    int32_t caption = mwinWaylandCaptionOf(window->platform, window, mwin_modeWindowed);
    int32_t minimumHeight = (int32_t)lroundf(minimum.height);
    int32_t maximumHeight = (int32_t)lroundf(maximum.height);
    api->proxyMarshalFlags(toplevel, XDG_TOPLEVEL_SET_MIN_SIZE, nullptr, version, 0,
                           (int32_t)lroundf(minimum.width),
                           minimumHeight > 0 ? minimumHeight + caption : 0);
    api->proxyMarshalFlags(toplevel, XDG_TOPLEVEL_SET_MAX_SIZE, nullptr, version, 0,
                           (int32_t)lroundf(maximum.width),
                           maximumHeight > 0 ? maximumHeight + caption : 0);
}

// Carries out a request the protocol can; the outcome, or -1 for one the
// next configure answers.
// An inhibitor on the surface while the window asks, which the
// compositor heeds while the surface shows; the session bus without.
static int KeepAwake(mwinWaylandWindow* window, bool awake)
{
    mwinWaylandPlatform* platform = window->platform;
    const mwinWaylandApi* api = &platform->api;
    if (platform->idleInhibits == nullptr)
    {
        return mwinLinuxCanKeepAwake(&platform->services);
    }
    if (awake && window->inhibitor == nullptr)
    {
        window->inhibitor = mwinWlCreateFor(api, platform->idleInhibits,
                                            ZWP_IDLE_INHIBIT_MANAGER_V1_CREATE_INHIBITOR,
                                            &zwp_idle_inhibitor_v1_interface, window->surface);
    }
    else if (!awake && window->inhibitor != nullptr)
    {
        (void)mwinWlRequest(api, window->inhibitor, ZWP_IDLE_INHIBITOR_V1_DESTROY, nullptr,
                            WL_MARSHAL_FLAG_DESTROY);
        window->inhibitor = nullptr;
    }
    return mwin_outcomeDone;
}

// The requests a popup answers otherwise: its place and size through
// its positioner, its title kept for the program, and what only a
// toplevel has unsupported.
static int CarryOutPopup(mwinWaylandWindow* window, mwinWindow* core, const mwinRequest* request)
{
    switch (request->kind)
    {
    case mwin_requestTitle:
        memmove(core->title, core->pendingTitle, core->pendingTitleLength);
        core->titleLength = core->pendingTitleLength;
        return mwin_outcomeDone;
    case mwin_requestPosition:
        return mwinWaylandPlacePopup(window, request->value.position, window->size);
    case mwin_requestSize:
        // The configure that answers resizes it.
        return mwinWaylandPlacePopup(window, window->placed, request->value.size);
    default:
        return mwin_outcomeUnsupported;
    }
}

static int CarryOut(mwinWaylandWindow* window, mwinWindow* core, uint32_t index)
{
    const mwinRequest* request = &core->requests[index];
    bool popupRequest =
        request->kind == mwin_requestTitle || request->kind == mwin_requestPosition ||
        request->kind == mwin_requestSize || request->kind == mwin_requestSizeLimits ||
        request->kind == mwin_requestIcon;
    if (window->popup != nullptr && popupRequest)
    {
        return CarryOutPopup(window, core, request);
    }
    switch (request->kind)
    {
    case mwin_requestTitle:
        SetTitle(window, core->pendingTitle, core->pendingTitleLength);
        memmove(core->title, core->pendingTitle, core->pendingTitleLength);
        core->titleLength = core->pendingTitleLength;
        return mwin_outcomeDone;
    case mwin_requestSize:
        // A toplevel sizes itself only while windowed.
        if (core->state.mode != mwin_modeWindowed || !window->configured)
        {
            return mwin_outcomeDenied;
        }
        window->size = request->value.size;
        ApplySurfaceSize(window);
        PostSize(window);
        mwinWaylandUpdateFrame(window->platform, window->slot);
        return mwin_outcomeDone;
    case mwin_requestMode:
        RequestMode(window, request->value.mode);
        if (request->value.mode == mwin_modeMinimized)
        {
            window->minimized = true;
            PostMode(window, mwin_modeMinimized);
            return mwin_outcomeDone;
        }
        window->modeRequest = (int32_t)index;
        return -1;
    case mwin_requestSizeLimits:
        SetLimits(window, request->value.limits.minimum, request->value.limits.maximum);
        return mwin_outcomeDone;
    case mwin_requestCursorMode:
        return mwinWaylandSetCursorMode(window->platform, window->slot, request->value.code);
    case mwin_requestCursorShape:
        return mwinWaylandSetCursorShape(window->platform, window->slot, request->value.code);
    case mwin_requestCursorImage:
        return mwinWaylandSetCursorImage(window->platform, window->slot, request->value.cursor);
    case mwin_requestTextInput:
        return mwinWaylandSetTextInput(window->platform, window->slot,
                                       request->value.textInput.enabled,
                                       request->value.textInput.caret);
    case mwin_requestClipboardWrite:
    case mwin_requestClipboardWriteData:
    case mwin_requestPrimaryWrite:
        return mwinWaylandWriteSelection(window->platform, request->kind);
    case mwin_requestClipboardRead:
    case mwin_requestClipboardReadData:
    case mwin_requestPrimaryRead:
        return mwinWaylandReadSelection(window->platform, request);
    case mwin_requestOpenUrl:
        return mwinLinuxOpenUrl(&window->platform->services, window->slot, index);
    case mwin_requestRevealFile:
        return mwinLinuxRevealFile(&window->platform->services, window->slot, index);
    case mwin_requestKeepAwake:
        return KeepAwake(window, request->value.awake);
    case mwin_requestIcon:
        return mwinWaylandSetIcon(window, request);
    case mwin_requestHitRegions:
        // Presses read them (wayland_frame.c).
        return mwin_outcomeDone;
    case mwin_requestFocus:
        return mwinWaylandRequestFocus(window, index);
    case mwin_requestFileDialog:
        // The portal places dialogs over a Wayland window only through an
        // exported handle (xdg-foreign), which the backend does not make.
        return mwinDialogsOpen(&window->platform->services.dialogs, window->slot, index, "");
    default:
        // Positions, visibility and the rest have no request in the
        // protocols bound.
        return mwin_outcomeUnsupported;
    }
}

void mwinWaylandSubmit(mwinContext* context, uint32_t slot, uint32_t request)
{
    mwinWaylandWindow* window = &PlatformOf(context)->windows[slot];
    int outcome = CarryOut(window, &context->windows[slot], request);
    if (outcome >= 0)
    {
        mwinComplete(context, slot, request, (mwinOutcome)outcome);
    }
}
