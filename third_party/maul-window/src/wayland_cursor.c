// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Cursors over Wayland windows.

#include "wayland_cursor.h"

#include "cursor.h"
#include "icon.h"

#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

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

// Where a cursor shows: the pointer, or a tablet tool near a window.
// The request that sets its cursor surface (with the same arguments on
// both), the serial that quotes, its cursor shape device, and the
// surface theme cursors show on for it.
typedef struct CursorTarget
{
    void* proxy;
    uint32_t setCursor;
    uint32_t serial;
    struct wp_cursor_shape_device_v1* shapeDevice;
    struct wl_surface** themeSurface;
} CursorTarget;

static CursorTarget PointerTarget(mwinWaylandPlatform* platform)
{
    mwinWaylandPointer* pointer = &platform->pointer;
    return (CursorTarget){pointer->pointer, WL_POINTER_SET_CURSOR, pointer->enterSerial,
                          pointer->shapeDevice, &platform->cursorTheme.surface};
}

static CursorTarget ToolTarget(mwinWaylandTool* tool)
{
    return (CursorTarget){tool->tool, ZWP_TABLET_TOOL_V2_SET_CURSOR, tool->serial,
                          tool->shapeDevice, &tool->themeSurface};
}

static void SetCursor(mwinWaylandPlatform* platform, const CursorTarget* target,
                      struct wl_surface* surface, int32_t x, int32_t y)
{
    const mwinWaylandApi* api = &platform->api;
    api->proxyMarshalFlags((struct wl_proxy*)target->proxy, target->setCursor, nullptr,
                           mwinWlVersion(api, target->proxy), 0, target->serial, surface, x, y);
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
static bool ShowThemeCursor(mwinWaylandPlatform* platform, const CursorTarget* target,
                            const mwinWaylandWindow* window, mwinCursorShape shape)
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
    if (*target->themeSurface == nullptr)
    {
        *target->themeSurface = mwinWlRequest(
            api, platform->compositor, WL_COMPOSITOR_CREATE_SURFACE, &wl_surface_interface, 0);
    }
    struct wl_proxy* surface = (struct wl_proxy*)*target->themeSurface;
    uint32_t version = mwinWlVersion(api, surface);
    if (version >= WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION)
    {
        api->proxyMarshalFlags(surface, WL_SURFACE_SET_BUFFER_SCALE, nullptr, version, 0, scale);
    }
    api->proxyMarshalFlags(surface, WL_SURFACE_ATTACH, nullptr, version, 0, buffer, 0, 0);
    api->proxyMarshalFlags(surface, WL_SURFACE_DAMAGE, nullptr, version, 0, 0, 0,
                           (int32_t)image->width, (int32_t)image->height);
    (void)mwinWlRequest(api, surface, WL_SURFACE_COMMIT, nullptr, 0);
    SetCursor(platform, target, *target->themeSurface, (int32_t)image->hotspot_x / scale,
              (int32_t)image->hotspot_y / scale);
    return true;
}

// A buffer of an image: ARGB, premultiplied, in its own shared memory,
// which the buffer keeps; NULL when the memory cannot be had.
static struct wl_buffer* MakeBuffer(mwinWaylandPlatform* platform, const mwinIconCopyImage* image)
{
    const mwinWaylandApi* api = &platform->api;
    size_t bytes = (size_t)image->width * image->height * 4;
    void* memory = nullptr;
    int fd = mwinWaylandMapMemory(bytes, &memory);
    if (fd < 0)
    {
        return nullptr;
    }
    uint32_t* pixels = memory;
    for (size_t i = 0; i < bytes / 4; i++)
    {
        const uint8_t* rgba = &image->pixels[i * 4];
        uint32_t alpha = rgba[3];
        uint32_t red = (rgba[0] * alpha + 127) / 255;
        uint32_t green = (rgba[1] * alpha + 127) / 255;
        uint32_t blue = (rgba[2] * alpha + 127) / 255;
        pixels[i] = alpha << 24 | red << 16 | green << 8 | blue;
    }
    struct wl_proxy* pool = api->proxyMarshalFlags(
        (struct wl_proxy*)platform->shm, WL_SHM_CREATE_POOL, &wl_shm_pool_interface,
        mwinWlVersion(api, platform->shm), 0, nullptr, fd, (int32_t)bytes);
    (void)close(fd);
    int32_t width = (int32_t)image->width;
    struct wl_buffer* buffer = (struct wl_buffer*)api->proxyMarshalFlags(
        pool, WL_SHM_POOL_CREATE_BUFFER, &wl_buffer_interface, mwinWlVersion(api, pool), 0, nullptr,
        0, width, (int32_t)image->height, width * 4, WL_SHM_FORMAT_ARGB8888);
    (void)mwinWlRequest(api, pool, WL_SHM_POOL_DESTROY, nullptr, WL_MARSHAL_FLAG_DESTROY);
    munmap(memory, bytes);
    return buffer;
}

// The buffer scale an image shows at, so that it covers the first
// image's size: the ratio of their widths where it is whole and divides
// the image, else 1.
static int32_t BufferScaleOf(const mwinCursor* cursor, uint32_t image)
{
    const mwinIconCopyImage* first = &cursor->images->images[0];
    const mwinIconCopyImage* chosen = &cursor->images->images[image];
    uint32_t scale = chosen->width / first->width;
    return scale > 1 && chosen->width % scale == 0 && chosen->height % scale == 0 &&
                   chosen->width == first->width * scale
               ? (int32_t)scale
               : 1;
}

// Shows a cursor made from images: the image for the window's scale on
// the image surface, sized to the first image in logical units by the
// viewport, or by a buffer scale without one. False when it cannot be
// shown.
static bool ShowImage(mwinWaylandPlatform* platform, const mwinWaylandWindow* window)
{
    const mwinWaylandApi* api = &platform->api;
    mwinWaylandImageCursor* shown = &platform->imageCursor;
    mwinCursor* cursor = mwinFindCursor(platform->context, window->cursorImage);
    if (cursor == nullptr)
    {
        return false;
    }
    float scale =
        window->scale120 != 0 ? (float)window->scale120 / 120.0f : (float)window->bufferScale;
    uint32_t image = mwinCursorImageFor(cursor, scale);
    if (cursor->native[image] == nullptr)
    {
        cursor->native[image] = MakeBuffer(platform, &cursor->images->images[image]);
    }
    if (cursor->native[image] == nullptr)
    {
        return false;
    }
    if (shown->surface == nullptr)
    {
        shown->surface = mwinWlRequest(api, platform->compositor, WL_COMPOSITOR_CREATE_SURFACE,
                                       &wl_surface_interface, 0);
        shown->viewport =
            platform->viewporter == nullptr
                ? nullptr
                : mwinWlCreateFor(api, platform->viewporter, WP_VIEWPORTER_GET_VIEWPORT,
                                  &wp_viewport_interface, shown->surface);
    }
    const mwinIconCopyImage* first = &cursor->images->images[0];
    const mwinIconCopyImage* chosen = &cursor->images->images[image];
    struct wl_proxy* surface = (struct wl_proxy*)shown->surface;
    uint32_t version = mwinWlVersion(api, surface);
    int32_t bufferScale = shown->viewport != nullptr ? 1 : BufferScaleOf(cursor, image);
    if (shown->viewport != nullptr)
    {
        api->proxyMarshalFlags((struct wl_proxy*)shown->viewport, WP_VIEWPORT_SET_DESTINATION,
                               nullptr, mwinWlVersion(api, shown->viewport), 0,
                               (int32_t)first->width, (int32_t)first->height);
    }
    if (version >= WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION)
    {
        api->proxyMarshalFlags(surface, WL_SURFACE_SET_BUFFER_SCALE, nullptr, version, 0,
                               bufferScale);
    }
    shown->buffer = cursor->native[image];
    api->proxyMarshalFlags(surface, WL_SURFACE_ATTACH, nullptr, version, 0, shown->buffer, 0, 0);
    api->proxyMarshalFlags(surface, WL_SURFACE_DAMAGE, nullptr, version, 0, 0, 0,
                           (int32_t)chosen->width, (int32_t)chosen->height);
    (void)mwinWlRequest(api, surface, WL_SURFACE_COMMIT, nullptr, 0);
    // The hotspot is in surface-local units: the first image's pixels
    // under a viewport, the chosen image's over its buffer scale.
    uint32_t x = cursor->hotspotX;
    uint32_t y = cursor->hotspotY;
    if (shown->viewport == nullptr)
    {
        mwinCursorHotspotOf(cursor, image, &x, &y);
        x /= (uint32_t)bufferScale;
        y /= (uint32_t)bufferScale;
    }
    CursorTarget target = PointerTarget(platform);
    SetCursor(platform, &target, shown->surface, (int32_t)x, (int32_t)y);
    return true;
}

// Shows a shape over a window, whose scale a theme image follows.
static void ShowShape(mwinWaylandPlatform* platform, const CursorTarget* target,
                      const mwinWaylandWindow* window, mwinCursorShape shape)
{
    const mwinWaylandApi* api = &platform->api;
    if (target->shapeDevice != nullptr)
    {
        api->proxyMarshalFlags(
            (struct wl_proxy*)target->shapeDevice, WP_CURSOR_SHAPE_DEVICE_V1_SET_SHAPE, nullptr,
            mwinWlVersion(api, target->shapeDevice), 0, target->serial, s_shapes[shape].protocol);
    }
    else
    {
        // Without a theme the compositor keeps the cursor it shows.
        (void)ShowThemeCursor(platform, target, window, shape);
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
    CursorTarget target = PointerTarget(platform);
    if (IsHidden(window->cursorMode))
    {
        SetCursor(platform, &target, nullptr, 0, 0);
    }
    else if (!ShowImage(platform, window))
    {
        ShowShape(platform, &target, window, window->cursorShape);
    }
}

void mwinWaylandShowToolCursor(mwinWaylandPlatform* platform, mwinWaylandTool* tool)
{
    if (tool->focus < 0)
    {
        return;
    }
    const mwinWaylandApi* api = &platform->api;
    if (tool->shapeDevice == nullptr && platform->cursorShapes != nullptr)
    {
        tool->shapeDevice = mwinWlCreateFor(api, platform->cursorShapes,
                                            WP_CURSOR_SHAPE_MANAGER_V1_GET_TABLET_TOOL_V2,
                                            &wp_cursor_shape_device_v1_interface, tool->tool);
    }
    const mwinWaylandWindow* window = &platform->windows[tool->focus];
    CursorTarget target = ToolTarget(tool);
    if (IsHidden(window->cursorMode))
    {
        SetCursor(platform, &target, nullptr, 0, 0);
    }
    else
    {
        ShowShape(platform, &target, window, window->cursorShape);
    }
}

// Shows the cursors over the window in a slot: the pointer's and those
// of the tools near it.
static void ShowCursorsOver(mwinWaylandPlatform* platform, uint32_t slot)
{
    if (platform->pointer.focus == (int32_t)slot)
    {
        mwinWaylandShowCursor(platform);
    }
    for (int i = 0; i < MWIN_WAYLAND_TOOLS; i++)
    {
        mwinWaylandTool* tool = &platform->tablets.tools[i];
        if (tool->tool != nullptr && tool->focus == (int32_t)slot)
        {
            mwinWaylandShowToolCursor(platform, tool);
        }
    }
}

void mwinWaylandShowFrameCursor(mwinWaylandPlatform* platform, uint32_t slot, mwinCursorShape shape)
{
    if (platform->pointer.pointer != nullptr)
    {
        CursorTarget target = PointerTarget(platform);
        ShowShape(platform, &target, &platform->windows[slot], shape);
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
    ShowCursorsOver(platform, slot);
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
    platform->windows[slot].cursorImage = (mwinCursorId){0};
    ShowCursorsOver(platform, slot);
    return mwin_outcomeDone;
}

mwinOutcome mwinWaylandSetCursorImage(mwinWaylandPlatform* platform, uint32_t slot,
                                      mwinCursorId cursor)
{
    if (platform->shm == nullptr)
    {
        return mwin_outcomeUnsupported;
    }
    platform->windows[slot].cursorImage = cursor;
    if (platform->pointer.focus == (int32_t)slot)
    {
        mwinWaylandShowCursor(platform);
    }
    return mwin_outcomeDone;
}

void mwinWaylandReleaseCursor(mwinContext* context, uint32_t slot)
{
    mwinWaylandPlatform* platform = context->backendData;
    const mwinWaylandApi* api = &platform->api;
    // Its windows show the default shape from here.
    for (uint32_t i = 0; i < context->limits.windows; i++)
    {
        mwinWaylandWindow* window = &platform->windows[i];
        if (window->cursorImage.index1 == slot + 1)
        {
            window->cursorShape = mwin_shapeDefault;
            window->cursorImage = (mwinCursorId){0};
            ShowCursorsOver(platform, i);
        }
    }
    mwinCursor* cursor = &context->cursors[slot];
    for (uint32_t i = 0; i < MWIN_CURSOR_IMAGES; i++)
    {
        if (cursor->native[i] == nullptr)
        {
            continue;
        }
        if (platform->imageCursor.buffer == cursor->native[i])
        {
            // Off the image surface before it goes.
            struct wl_proxy* surface = (struct wl_proxy*)platform->imageCursor.surface;
            api->proxyMarshalFlags(surface, WL_SURFACE_ATTACH, nullptr, mwinWlVersion(api, surface),
                                   0, nullptr, 0, 0);
            (void)mwinWlRequest(api, surface, WL_SURFACE_COMMIT, nullptr, 0);
            platform->imageCursor.buffer = nullptr;
        }
        (void)mwinWlRequest(api, cursor->native[i], WL_BUFFER_DESTROY, nullptr,
                            WL_MARSHAL_FLAG_DESTROY);
        cursor->native[i] = nullptr;
    }
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
    mwinWaylandImageCursor* shown = &platform->imageCursor;
    if (shown->viewport != nullptr)
    {
        (void)mwinWlRequest(api, shown->viewport, WP_VIEWPORT_DESTROY, nullptr,
                            WL_MARSHAL_FLAG_DESTROY);
    }
    if (shown->surface != nullptr)
    {
        (void)mwinWlRequest(api, shown->surface, WL_SURFACE_DESTROY, nullptr,
                            WL_MARSHAL_FLAG_DESTROY);
    }
    *shown = (mwinWaylandImageCursor){0};
}
