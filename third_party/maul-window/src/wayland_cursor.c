// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Cursors over Wayland windows.

#include "wayland_cursor.h"

#include <stdlib.h>

// The theme's cursor size in logical units where XCURSOR_SIZE says
// nothing usable.
#define DEFAULT_CURSOR_SIZE 24
#define MAX_CURSOR_SIZE     512

// Each shape in the cursor shape protocol, and its names in cursor
// themes: the CSS name, then the older X11 name.
static const struct
{
    uint32_t protocol;
    const char* name;
    const char* legacy;
} s_shapes[] = {
    {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT, "default", "left_ptr"},
    {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT, "text", "xterm"},
    {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER, "pointer", "hand2"},
    {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR, "crosshair", "cross"},
    {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_MOVE, "move", "fleur"},
    {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_EW_RESIZE, "ew-resize", "sb_h_double_arrow"},
    {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NS_RESIZE, "ns-resize", "sb_v_double_arrow"},
    {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NESW_RESIZE, "nesw-resize", "fd_double_arrow"},
    {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NWSE_RESIZE, "nwse-resize", "bd_double_arrow"},
    {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NOT_ALLOWED, "not-allowed", "crossed_circle"},
    {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_WAIT, "wait", "watch"},
    {WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_PROGRESS, "progress", "left_ptr_watch"},
};

static bool IsHidden(mwinCursorMode mode)
{
    return mode == mwin_cursorHidden || mode == mwin_cursorCaptured ||
           mode == mwin_cursorConfinedHidden;
}

// The cursor size XCURSOR_SIZE names, in logical units.
static int32_t CursorSize(void)
{
    const char* text = getenv("XCURSOR_SIZE");
    int32_t size = 0;
    for (size_t i = 0; text != nullptr && text[i] != '\0'; i++)
    {
        if (text[i] < '0' || text[i] > '9' || size > MAX_CURSOR_SIZE)
        {
            return DEFAULT_CURSOR_SIZE;
        }
        size = size * 10 + (text[i] - '0');
    }
    return size > 0 && size <= MAX_CURSOR_SIZE ? size : DEFAULT_CURSOR_SIZE;
}

static void SetCursor(mwinWaylandPlatform* platform, struct wl_surface* surface, int32_t x,
                      int32_t y)
{
    const mwinWaylandApi* api = &platform->api;
    void* pointer = platform->pointer.pointer;
    api->proxyMarshalFlags((struct wl_proxy*)pointer, WL_POINTER_SET_CURSOR, nullptr,
                           mwinWlVersion(api, pointer), 0, platform->pointer.enterSerial, surface,
                           x, y);
}

// Loads the theme at a scale, unless it is loaded at that scale.
static bool LoadTheme(mwinWaylandPlatform* platform, int32_t scale)
{
    const mwinWaylandApi* api = &platform->api;
    mwinWaylandCursorTheme* theme = &platform->cursorTheme;
    if (theme->theme != nullptr && theme->scale == scale)
    {
        return true;
    }
    if (theme->theme != nullptr)
    {
        api->cursorThemeDestroy(theme->theme);
    }
    theme->theme =
        api->cursorThemeLoad(getenv("XCURSOR_THEME"), CursorSize() * scale, platform->shm);
    theme->scale = scale;
    return theme->theme != nullptr;
}

// Shows a shape from the cursor theme: false where the theme has none.
static bool ShowThemeCursor(mwinWaylandPlatform* platform, const mwinWaylandWindow* window,
                            mwinCursorShape shape)
{
    const mwinWaylandApi* api = &platform->api;
    mwinWaylandCursorTheme* theme = &platform->cursorTheme;
    int32_t scale = mwinWaylandImageScale(window);
    if (api->cursorLibrary == nullptr || platform->shm == nullptr || !LoadTheme(platform, scale))
    {
        return false;
    }
    struct wl_cursor* cursor = api->cursorThemeGetCursor(theme->theme, s_shapes[shape].name);
    if (cursor == nullptr)
    {
        cursor = api->cursorThemeGetCursor(theme->theme, s_shapes[shape].legacy);
    }
    if (cursor == nullptr || cursor->image_count == 0)
    {
        return false;
    }
    // An animated cursor shows its first image.
    struct wl_cursor_image* image = cursor->images[0];
    struct wl_buffer* buffer = api->cursorImageGetBuffer(image);
    if (buffer == nullptr)
    {
        return false;
    }
    if (theme->surface == nullptr)
    {
        theme->surface = mwinWlRequest(api, platform->compositor, WL_COMPOSITOR_CREATE_SURFACE,
                                       &wl_surface_interface, 0);
    }
    struct wl_proxy* surface = (struct wl_proxy*)theme->surface;
    uint32_t version = mwinWlVersion(api, surface);
    if (version >= WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION)
    {
        api->proxyMarshalFlags(surface, WL_SURFACE_SET_BUFFER_SCALE, nullptr, version, 0, scale);
    }
    api->proxyMarshalFlags(surface, WL_SURFACE_ATTACH, nullptr, version, 0, buffer, 0, 0);
    api->proxyMarshalFlags(surface, WL_SURFACE_DAMAGE, nullptr, version, 0, 0, 0,
                           (int32_t)image->width, (int32_t)image->height);
    (void)mwinWlRequest(api, surface, WL_SURFACE_COMMIT, nullptr, 0);
    SetCursor(platform, theme->surface, (int32_t)image->hotspot_x / scale,
              (int32_t)image->hotspot_y / scale);
    return true;
}

// Shows a shape over a window, whose scale a theme image follows.
static void ShowShape(mwinWaylandPlatform* platform, const mwinWaylandWindow* window,
                      mwinCursorShape shape)
{
    const mwinWaylandApi* api = &platform->api;
    const mwinWaylandPointer* pointer = &platform->pointer;
    if (pointer->shapeDevice != nullptr)
    {
        api->proxyMarshalFlags((struct wl_proxy*)pointer->shapeDevice,
                               WP_CURSOR_SHAPE_DEVICE_V1_SET_SHAPE, nullptr,
                               mwinWlVersion(api, pointer->shapeDevice), 0, pointer->enterSerial,
                               s_shapes[shape].protocol);
    }
    else
    {
        // Without a theme the compositor keeps the cursor it shows.
        (void)ShowThemeCursor(platform, window, shape);
    }
}

void mwinWaylandShowCursor(mwinWaylandPlatform* platform)
{
    const mwinWaylandPointer* pointer = &platform->pointer;
    if (pointer->pointer == nullptr || pointer->focus < 0)
    {
        return;
    }
    const mwinWaylandWindow* window = &platform->windows[pointer->focus];
    if (IsHidden(window->cursorMode))
    {
        SetCursor(platform, nullptr, 0, 0);
    }
    else
    {
        ShowShape(platform, window, window->cursorShape);
    }
}

void mwinWaylandShowFrameCursor(mwinWaylandPlatform* platform, uint32_t slot, mwinCursorShape shape)
{
    if (platform->pointer.pointer != nullptr)
    {
        ShowShape(platform, &platform->windows[slot], shape);
    }
}

static void OnLocked(void* data, struct zwp_locked_pointer_v1* locked)
{
    (void)data;
    (void)locked;
}

static void OnUnlocked(void* data, struct zwp_locked_pointer_v1* locked)
{
    (void)data;
    (void)locked;
}

static const struct zwp_locked_pointer_v1_listener s_lockedListener = {
    OnLocked,
    OnUnlocked,
};

static void OnConfined(void* data, struct zwp_confined_pointer_v1* confined)
{
    (void)data;
    (void)confined;
}

static void OnUnconfined(void* data, struct zwp_confined_pointer_v1* confined)
{
    (void)data;
    (void)confined;
}

static const struct zwp_confined_pointer_v1_listener s_confinedListener = {
    OnConfined,
    OnUnconfined,
};

static void DropConstraint(mwinWaylandPlatform* platform, mwinWaylandWindow* window)
{
    const mwinWaylandApi* api = &platform->api;
    if (window->locked != nullptr)
    {
        (void)mwinWlRequest(api, window->locked, ZWP_LOCKED_POINTER_V1_DESTROY, nullptr,
                            WL_MARSHAL_FLAG_DESTROY);
        window->locked = nullptr;
    }
    if (window->confined != nullptr)
    {
        (void)mwinWlRequest(api, window->confined, ZWP_CONFINED_POINTER_V1_DESTROY, nullptr,
                            WL_MARSHAL_FLAG_DESTROY);
        window->confined = nullptr;
    }
}

// Gives a window the constraint its cursor mode needs, if any.
static void Constrain(mwinWaylandPlatform* platform, mwinWaylandWindow* window)
{
    const mwinWaylandApi* api = &platform->api;
    DropConstraint(platform, window);
    void* pointer = platform->pointer.pointer;
    struct wl_proxy* constraints = (struct wl_proxy*)platform->constraints;
    if (pointer == nullptr || constraints == nullptr || window->surface == nullptr)
    {
        return;
    }
    uint32_t version = mwinWlVersion(api, constraints);
    if (window->cursorMode == mwin_cursorCaptured)
    {
        window->locked = (struct zwp_locked_pointer_v1*)api->proxyMarshalFlags(
            constraints, ZWP_POINTER_CONSTRAINTS_V1_LOCK_POINTER, &zwp_locked_pointer_v1_interface,
            version, 0, nullptr, window->surface, pointer, nullptr,
            ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
        mwinWlListen(api, window->locked, &s_lockedListener, window);
    }
    else if (window->cursorMode == mwin_cursorConfined ||
             window->cursorMode == mwin_cursorConfinedHidden)
    {
        window->confined = (struct zwp_confined_pointer_v1*)api->proxyMarshalFlags(
            constraints, ZWP_POINTER_CONSTRAINTS_V1_CONFINE_POINTER,
            &zwp_confined_pointer_v1_interface, version, 0, nullptr, window->surface, pointer,
            nullptr, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
        mwinWlListen(api, window->confined, &s_confinedListener, window);
    }
}

// Relative motion counts only while the window under the pointer holds
// it captured.
static void OnRelativeMotion(void* data, struct zwp_relative_pointer_v1* relative,
                             uint32_t microsecondsHigh, uint32_t microsecondsLow, wl_fixed_t dx,
                             wl_fixed_t dy, wl_fixed_t dxRaw, wl_fixed_t dyRaw)
{
    (void)relative;
    (void)dx;
    (void)dy;
    mwinWaylandPlatform* platform = data;
    int32_t focus = platform->pointer.focus;
    if (focus < 0 || platform->windows[focus].cursorMode != mwin_cursorCaptured)
    {
        return;
    }
    uint64_t now = mwinMonotonicNow();
    uint64_t timeNs = (((uint64_t)microsecondsHigh << 32) | microsecondsLow) * 1000u;
    mwinEvent event = {0};
    event.type = mwin_eventRawPointerDelta;
    // A time far from now is on another clock.
    event.timeNs = timeNs <= now && now - timeNs < 60000000000u ? timeNs : now;
    event.data.delta =
        (mwinDeltaEvent){(float)wl_fixed_to_double(dxRaw), (float)wl_fixed_to_double(dyRaw)};
    mwinPost(platform->context, (uint32_t)focus, &event);
}

static const struct zwp_relative_pointer_v1_listener s_relativeListener = {
    OnRelativeMotion,
};

void mwinWaylandAttachCursors(mwinWaylandPlatform* platform)
{
    const mwinWaylandApi* api = &platform->api;
    mwinWaylandPointer* pointer = &platform->pointer;
    if (platform->cursorShapes != nullptr)
    {
        pointer->shapeDevice =
            mwinWlCreateFor(api, platform->cursorShapes, WP_CURSOR_SHAPE_MANAGER_V1_GET_POINTER,
                            &wp_cursor_shape_device_v1_interface, pointer->pointer);
    }
    if (platform->relativePointers != nullptr)
    {
        pointer->relative = mwinWlCreateFor(api, platform->relativePointers,
                                            ZWP_RELATIVE_POINTER_MANAGER_V1_GET_RELATIVE_POINTER,
                                            &zwp_relative_pointer_v1_interface, pointer->pointer);
        mwinWlListen(api, pointer->relative, &s_relativeListener, platform);
    }
    for (uint32_t i = 0; i < platform->context->limits.windows; i++)
    {
        Constrain(platform, &platform->windows[i]);
    }
}

void mwinWaylandDetachCursors(mwinWaylandPlatform* platform)
{
    const mwinWaylandApi* api = &platform->api;
    mwinWaylandPointer* pointer = &platform->pointer;
    for (uint32_t i = 0; i < platform->context->limits.windows; i++)
    {
        DropConstraint(platform, &platform->windows[i]);
    }
    if (pointer->shapeDevice != nullptr)
    {
        (void)mwinWlRequest(api, pointer->shapeDevice, WP_CURSOR_SHAPE_DEVICE_V1_DESTROY, nullptr,
                            WL_MARSHAL_FLAG_DESTROY);
        pointer->shapeDevice = nullptr;
    }
    if (pointer->relative != nullptr)
    {
        (void)mwinWlRequest(api, pointer->relative, ZWP_RELATIVE_POINTER_V1_DESTROY, nullptr,
                            WL_MARSHAL_FLAG_DESTROY);
        pointer->relative = nullptr;
    }
}

mwinOutcome mwinWaylandSetCursorMode(mwinWaylandPlatform* platform, uint32_t slot,
                                     mwinCursorMode mode)
{
    bool constrained = mode == mwin_cursorCaptured || mode == mwin_cursorConfined ||
                       mode == mwin_cursorConfinedHidden;
    if ((constrained && platform->constraints == nullptr) ||
        (mode == mwin_cursorCaptured && platform->relativePointers == nullptr))
    {
        return mwin_outcomeUnsupported;
    }
    mwinWaylandWindow* window = &platform->windows[slot];
    window->cursorMode = mode;
    Constrain(platform, window);
    if (platform->pointer.focus == (int32_t)slot)
    {
        mwinWaylandShowCursor(platform);
    }
    return mwin_outcomeDone;
}

mwinOutcome mwinWaylandSetCursorShape(mwinWaylandPlatform* platform, uint32_t slot,
                                      mwinCursorShape shape)
{
    if (platform->cursorShapes == nullptr &&
        (platform->api.cursorLibrary == nullptr || platform->shm == nullptr))
    {
        return mwin_outcomeUnsupported;
    }
    platform->windows[slot].cursorShape = shape;
    if (platform->pointer.focus == (int32_t)slot)
    {
        mwinWaylandShowCursor(platform);
    }
    return mwin_outcomeDone;
}

void mwinWaylandDropCursor(mwinWaylandPlatform* platform, uint32_t slot)
{
    DropConstraint(platform, &platform->windows[slot]);
}

void mwinWaylandReleaseCursorTheme(mwinWaylandPlatform* platform)
{
    const mwinWaylandApi* api = &platform->api;
    mwinWaylandCursorTheme* theme = &platform->cursorTheme;
    if (theme->surface != nullptr)
    {
        (void)mwinWlRequest(api, theme->surface, WL_SURFACE_DESTROY, nullptr,
                            WL_MARSHAL_FLAG_DESTROY);
    }
    if (theme->theme != nullptr)
    {
        api->cursorThemeDestroy(theme->theme);
    }
    *theme = (mwinWaylandCursorTheme){0};
}
