// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The frame the backend draws where the compositor draws none.

#include "wayland_frame.h"

#include "chrome.h"
#include "wayland_cursor.h"

#include <linux/input-event-codes.h>
#include <math.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

// The invisible resize margin around the frame, the reach of a corner
// along each edge, and the size of a button's glyph, in logical units.
#define MARGIN 6
#define CORNER 16
#define GLYPH  10

// The caption buttons, from the right.
enum
{
    buttonClose = 0,
    buttonMaximize = 1,
    buttonMinimize = 2,
    BUTTONS = 3,
};

// A part's place and size in the content surface's logical units.
typedef struct Rect
{
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} Rect;

// The caption's colors, premultiplied ARGB.
typedef struct Palette
{
    uint32_t background;
    uint32_t glyph;
    uint32_t hover;
    uint32_t closeHover;
    uint32_t closeGlyph;
} Palette;

static const Palette s_light = {0xFFEBEBEBu, 0xFF2E2E2Eu, 0xFFD4D4D4u, 0xFFC42B1Cu, 0xFFFFFFFFu};
static const Palette s_dark = {0xFF2B2B2Bu, 0xFFE6E6E6u, 0xFF454545u, 0xFFC42B1Cu, 0xFFFFFFFFu};

static const mwinWaylandWindow* WindowOf(const mwinWaylandPlatform* platform, uint32_t slot)
{
    return &platform->windows[slot];
}

int32_t mwinWaylandCaptionOf(const mwinWaylandPlatform* platform, const mwinWaylandWindow* window,
                             mwinWindowMode mode)
{
    const mwinWindow* core = &platform->context->windows[window->slot];
    bool serverSide = platform->decorations != nullptr &&
                      window->decorationMode != ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
    bool drawable = platform->subcompositor != nullptr && platform->shm != nullptr;
    mwinWindowStyle style = core->state.style;
    bool drawn = (style & mwin_styleDecorated) != 0 && (style & mwin_styleCustomChrome) == 0;
    return core->def.kind == mwin_windowNormal && drawn && !serverSide && drawable &&
                   mode != mwin_modeBorderlessFullscreen
               ? MWIN_FRAME_CAPTION
               : 0;
}

static Rect PartRect(int part, int32_t width, int32_t height)
{
    int32_t tall = height + MWIN_FRAME_CAPTION + 2 * MARGIN;
    switch (part)
    {
    case mwin_framePartCaption:
        return (Rect){0, -MWIN_FRAME_CAPTION, width, MWIN_FRAME_CAPTION};
    case mwin_framePartTop:
        return (Rect){0, -MWIN_FRAME_CAPTION - MARGIN, width, MARGIN};
    case mwin_framePartLeft:
        return (Rect){-MARGIN, -MWIN_FRAME_CAPTION - MARGIN, MARGIN, tall};
    case mwin_framePartRight:
        return (Rect){width, -MWIN_FRAME_CAPTION - MARGIN, MARGIN, tall};
    default:
        return (Rect){0, height, width, MARGIN};
    }
}

// The distance from a point to a segment.
static float SegmentDistance(float x, float y, float ax, float ay, float bx, float by)
{
    float dx = bx - ax;
    float dy = by - ay;
    float t = ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    float ex = x - (ax + t * dx);
    float ey = y - (ay + t * dy);
    return sqrtf(ex * ex + ey * ey);
}

// The distance from a point to the outline of a square centered at the
// origin.
static float SquareDistance(float x, float y, float half)
{
    return fabsf(fmaxf(fabsf(x) - half, fabsf(y) - half));
}

// How much of a pixel a button's glyph covers, from its center in the
// glyph's units, for a line half a unit thick.
static float Coverage(int button, bool maximized, float x, float y, float pixel)
{
    float half = GLYPH / 2.0f;
    float distance = 0.0f;
    switch (button)
    {
    case buttonClose:
        distance = fminf(SegmentDistance(x, y, -half, -half, half, half),
                         SegmentDistance(x, y, -half, half, half, -half));
        break;
    case buttonMaximize:
        // Restoring shows two squares, one behind the other.
        distance = maximized ? fminf(SquareDistance(x + 1.5f, y - 1.5f, half - 1.5f),
                                     SquareDistance(x - 1.5f, y + 1.5f, half - 1.5f))
                             : SquareDistance(x, y, half);
        break;
    default:
        distance = SegmentDistance(x, y, -half, 0.0f, half, 0.0f);
        break;
    }
    float coverage = (0.5f + pixel / 2.0f - distance) / pixel;
    return coverage < 0.0f ? 0.0f : (coverage > 1.0f ? 1.0f : coverage);
}

static uint32_t Blend(uint32_t under, uint32_t over, float amount)
{
    uint32_t result = 0xFF000000u;
    for (int shift = 0; shift < 24; shift += 8)
    {
        float a = (float)((under >> shift) & 0xFFu);
        float b = (float)((over >> shift) & 0xFFu);
        result |= (uint32_t)lroundf(a + (b - a) * amount) << shift;
    }
    return result;
}

// Draws the caption: its background and, from the right, the close,
// maximize and minimize buttons.
static void DrawCaption(uint32_t* pixels, int32_t width, int32_t height, int32_t scale,
                        const Palette* palette, int hover, bool maximized)
{
    int32_t size = MWIN_FRAME_CAPTION * scale;
    for (int32_t y = 0; y < height; y++)
    {
        for (int32_t x = 0; x < width; x++)
        {
            int32_t button = (width - 1 - x) / size;
            uint32_t color = palette->background;
            if (button >= BUTTONS)
            {
                pixels[y * width + x] = color;
                continue;
            }
            bool closeHover = button == buttonClose && hover == buttonClose;
            color = button == hover ? (closeHover ? palette->closeHover : palette->hover) : color;
            float cx = (float)(width - button * size) - (float)size / 2.0f;
            float gx = ((float)x + 0.5f - cx) / (float)scale;
            float gy = ((float)y + 0.5f - (float)height / 2.0f) / (float)scale;
            float cover = Coverage(button, maximized, gx, gy, 1.0f / (float)scale);
            uint32_t glyph = closeHover ? palette->closeGlyph : palette->glyph;
            pixels[y * width + x] = Blend(color, glyph, cover);
        }
    }
}

static void Destroy(const mwinWaylandApi* api, void* proxy, uint32_t opcode)
{
    if (proxy != nullptr)
    {
        (void)mwinWlRequest(api, proxy, opcode, nullptr, WL_MARSHAL_FLAG_DESTROY);
    }
}

// Frees the buffers and pool of the last drawing.
static void DropBuffers(const mwinWaylandApi* api, mwinWaylandFrame* frame)
{
    for (int part = 0; part < MWIN_FRAME_PARTS; part++)
    {
        Destroy(api, frame->buffers[part], WL_BUFFER_DESTROY);
        frame->buffers[part] = nullptr;
    }
    Destroy(api, frame->pool, WL_SHM_POOL_DESTROY);
    frame->pool = nullptr;
}

// Makes the parts' surfaces, above the content and committing on their
// own, so the frame shows without the renderer.
static void MakeParts(mwinWaylandPlatform* platform, mwinWaylandWindow* window)
{
    const mwinWaylandApi* api = &platform->api;
    mwinWaylandFrame* frame = &window->frame;
    if (frame->parts[0] != nullptr)
    {
        return;
    }
    for (int part = 0; part < MWIN_FRAME_PARTS; part++)
    {
        frame->parts[part] = mwinWlRequest(api, platform->compositor, WL_COMPOSITOR_CREATE_SURFACE,
                                           &wl_surface_interface, 0);
        frame->subsurfaces[part] = (struct wl_subsurface*)api->proxyMarshalFlags(
            (struct wl_proxy*)platform->subcompositor, WL_SUBCOMPOSITOR_GET_SUBSURFACE,
            &wl_subsurface_interface, mwinWlVersion(api, platform->subcompositor), 0, nullptr,
            frame->parts[part], window->surface);
        (void)mwinWlRequest(api, frame->subsurfaces[part], WL_SUBSURFACE_SET_DESYNC, nullptr, 0);
    }
}

int mwinWaylandMapMemory(size_t bytes, void** memory)
{
    int fd = memfd_create("maul-window-frame", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)bytes) != 0)
    {
        if (fd >= 0)
        {
            close(fd);
        }
        return -1;
    }
    *memory = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (*memory == MAP_FAILED)
    {
        close(fd);
        return -1;
    }
    return fd;
}

// The parts a frame shows: all, or the caption alone when maximized.
static int PartsShown(const mwinWaylandFrame* frame)
{
    return frame->captionOnly ? 1 : MWIN_FRAME_PARTS;
}

// Draws every part shown into a new pool and gives each its buffer.
static void Draw(mwinWaylandPlatform* platform, mwinWaylandWindow* window)
{
    const mwinWaylandApi* api = &platform->api;
    mwinWaylandFrame* frame = &window->frame;
    int32_t scale = mwinWaylandImageScale(window);
    int32_t width = (int32_t)lroundf(window->size.width);
    int32_t height = (int32_t)lroundf(window->size.height);
    size_t bytes = 0;
    for (int part = 0; part < PartsShown(frame); part++)
    {
        Rect rect = PartRect(part, width, height);
        bytes += (size_t)rect.width * (size_t)rect.height * (size_t)(scale * scale) * 4u;
    }
    void* memory = nullptr;
    int fd = mwinWaylandMapMemory(bytes, &memory);
    if (fd < 0)
    {
        return;
    }
    frame->pool = (struct wl_shm_pool*)api->proxyMarshalFlags(
        (struct wl_proxy*)platform->shm, WL_SHM_CREATE_POOL, &wl_shm_pool_interface,
        mwinWlVersion(api, platform->shm), 0, nullptr, fd, (int32_t)bytes);
    close(fd);
    const Palette* palette = platform->context->facts.theme == mwin_themeDark ? &s_dark : &s_light;
    bool maximized = platform->context->windows[window->slot].state.mode == mwin_modeMaximized;
    size_t offset = 0;
    for (int part = 0; part < PartsShown(frame); part++)
    {
        Rect rect = PartRect(part, width, height);
        int32_t pixelsWide = rect.width * scale;
        int32_t pixelsHigh = rect.height * scale;
        uint32_t* pixels = (uint32_t*)((unsigned char*)memory + offset);
        if (part == mwin_framePartCaption)
        {
            DrawCaption(pixels, pixelsWide, pixelsHigh, scale, palette, frame->hover, maximized);
        }
        else
        {
            // The margins take the pointer and show nothing.
            memset(pixels, 0, (size_t)pixelsWide * (size_t)pixelsHigh * 4u);
        }
        frame->buffers[part] = (struct wl_buffer*)api->proxyMarshalFlags(
            (struct wl_proxy*)frame->pool, WL_SHM_POOL_CREATE_BUFFER, &wl_buffer_interface,
            mwinWlVersion(api, frame->pool), 0, nullptr, (int32_t)offset, pixelsWide, pixelsHigh,
            pixelsWide * 4, WL_SHM_FORMAT_ARGB8888);
        offset += (size_t)pixelsWide * (size_t)pixelsHigh * 4u;
    }
    munmap(memory, bytes);
}

// Puts each part in place with its buffer, or empties the parts hidden.
static void Present(mwinWaylandPlatform* platform, mwinWaylandWindow* window)
{
    const mwinWaylandApi* api = &platform->api;
    mwinWaylandFrame* frame = &window->frame;
    int32_t scale = mwinWaylandImageScale(window);
    int32_t width = (int32_t)lroundf(window->size.width);
    int32_t height = (int32_t)lroundf(window->size.height);
    for (int part = 0; part < MWIN_FRAME_PARTS && frame->parts[0] != nullptr; part++)
    {
        struct wl_proxy* surface = (struct wl_proxy*)frame->parts[part];
        uint32_t version = mwinWlVersion(api, surface);
        bool shown = frame->shown && part < PartsShown(frame);
        Rect rect = PartRect(part, width, height);
        api->proxyMarshalFlags((struct wl_proxy*)frame->subsurfaces[part],
                               WL_SUBSURFACE_SET_POSITION, nullptr,
                               mwinWlVersion(api, frame->subsurfaces[part]), 0, rect.x, rect.y);
        api->proxyMarshalFlags(surface, WL_SURFACE_SET_BUFFER_SCALE, nullptr, version, 0, scale);
        api->proxyMarshalFlags(surface, WL_SURFACE_ATTACH, nullptr, version, 0,
                               shown ? frame->buffers[part] : nullptr, 0, 0);
        api->proxyMarshalFlags(surface, WL_SURFACE_DAMAGE, nullptr, version, 0, 0, 0, rect.width,
                               rect.height);
        (void)mwinWlRequest(api, surface, WL_SURFACE_COMMIT, nullptr, 0);
    }
}

// Draws the frame anew and shows it, then frees the last drawing, which
// the compositor no longer shows.
static void Redraw(mwinWaylandPlatform* platform, mwinWaylandWindow* window)
{
    mwinWaylandFrame old = window->frame;
    mwinWaylandFrame* frame = &window->frame;
    for (int part = 0; part < MWIN_FRAME_PARTS; part++)
    {
        frame->buffers[part] = nullptr;
    }
    frame->pool = nullptr;
    if (frame->shown)
    {
        Draw(platform, window);
    }
    Present(platform, window);
    DropBuffers(&platform->api, &old);
}

// Tells the compositor what of the surfaces is the window: the content
// and the caption above it.
static void SetGeometry(mwinWaylandPlatform* platform, const mwinWaylandWindow* window,
                        int32_t caption)
{
    const mwinWaylandApi* api = &platform->api;
    api->proxyMarshalFlags((struct wl_proxy*)window->xdgSurface, XDG_SURFACE_SET_WINDOW_GEOMETRY,
                           nullptr, mwinWlVersion(api, window->xdgSurface), 0, 0, -caption,
                           (int32_t)lroundf(window->size.width),
                           (int32_t)lroundf(window->size.height) + caption);
}

void mwinWaylandUpdateFrame(mwinWaylandPlatform* platform, uint32_t slot)
{
    mwinWaylandWindow* window = &platform->windows[slot];
    mwinWaylandFrame* frame = &window->frame;
    mwinWindowMode mode = platform->context->windows[slot].state.mode;
    int32_t caption = mwinWaylandCaptionOf(platform, window, mode);
    if (!window->configured)
    {
        return;
    }
    SetGeometry(platform, window, caption);
    frame->shown = caption > 0;
    frame->captionOnly = mode == mwin_modeMaximized;
    if (!frame->shown && frame->parts[0] == nullptr)
    {
        return;
    }
    MakeParts(platform, window);
    Redraw(platform, window);
}

void mwinWaylandDestroyFrame(mwinWaylandPlatform* platform, uint32_t slot)
{
    const mwinWaylandApi* api = &platform->api;
    mwinWaylandFrame* frame = &platform->windows[slot].frame;
    DropBuffers(api, frame);
    for (int part = 0; part < MWIN_FRAME_PARTS; part++)
    {
        if (platform->pointer.framePart == frame->parts[part] && frame->parts[part] != nullptr)
        {
            platform->pointer.framePart = nullptr;
        }
        Destroy(api, frame->subsurfaces[part], WL_SUBSURFACE_DESTROY);
        Destroy(api, frame->parts[part], WL_SURFACE_DESTROY);
    }
    *frame = (mwinWaylandFrame){.hover = -1, .pressed = -1};
}

// The window whose frame has a surface, and the part it is; -1 for none.
static int32_t FindPart(const mwinWaylandPlatform* platform, const struct wl_surface* surface,
                        int* partOut)
{
    for (uint32_t slot = 0; slot < platform->context->limits.windows && surface != nullptr; slot++)
    {
        for (int part = 0; part < MWIN_FRAME_PARTS; part++)
        {
            if (WindowOf(platform, slot)->frame.parts[part] == surface)
            {
                *partOut = part;
                return (int32_t)slot;
            }
        }
    }
    return -1;
}

// What a place on a part does: a resize edge, or none on the caption.
static uint32_t EdgeAt(int part, mwinPosition at, Rect rect)
{
    bool nearStart = part == mwin_framePartLeft || part == mwin_framePartRight
                         ? at.y < (float)CORNER
                         : at.x < (float)CORNER;
    bool nearEnd = part == mwin_framePartLeft || part == mwin_framePartRight
                       ? at.y >= (float)(rect.height - CORNER)
                       : at.x >= (float)(rect.width - CORNER);
    switch (part)
    {
    case mwin_framePartTop:
        return nearStart
                   ? XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT
                   : (nearEnd ? XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT : XDG_TOPLEVEL_RESIZE_EDGE_TOP);
    case mwin_framePartBottom:
        return nearStart ? XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT
                         : (nearEnd ? XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT
                                    : XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM);
    case mwin_framePartLeft:
        return nearStart ? XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT
                         : (nearEnd ? XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT
                                    : XDG_TOPLEVEL_RESIZE_EDGE_LEFT);
    case mwin_framePartRight:
        return nearStart ? XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT
                         : (nearEnd ? XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT
                                    : XDG_TOPLEVEL_RESIZE_EDGE_RIGHT);
    default:
        return XDG_TOPLEVEL_RESIZE_EDGE_NONE;
    }
}

static mwinCursorShape ShapeOf(uint32_t edge)
{
    switch (edge)
    {
    case XDG_TOPLEVEL_RESIZE_EDGE_TOP:
    case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM:
        return mwin_shapeResizeNorthSouth;
    case XDG_TOPLEVEL_RESIZE_EDGE_LEFT:
    case XDG_TOPLEVEL_RESIZE_EDGE_RIGHT:
        return mwin_shapeResizeEastWest;
    case XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT:
    case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT:
        return mwin_shapeResizeNorthwestSoutheast;
    case XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT:
    case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT:
        return mwin_shapeResizeNortheastSouthwest;
    default:
        return mwin_shapeDefault;
    }
}

// The caption button at a place on the caption, or -1.
static int ButtonAt(const mwinWaylandWindow* window, mwinPosition at)
{
    float fromRight = window->size.width - at.x;
    int button = fromRight >= 0.0f ? (int)(fromRight / (float)MWIN_FRAME_CAPTION) : BUTTONS;
    return button < BUTTONS ? button : -1;
}

// What the pointer is over: the window's slot, the part and the place.
typedef struct Target
{
    int32_t slot;
    int part;
    mwinPosition at;
} Target;

static Target TargetOf(const mwinWaylandPlatform* platform, mwinPosition at)
{
    Target target = {.at = at};
    target.slot = FindPart(platform, platform->pointer.framePart, &target.part);
    return target;
}

// Follows the pointer over a part: the cursor for its edge, and the
// caption button it is over.
static void Hover(mwinWaylandPlatform* platform, Target target)
{
    mwinWaylandWindow* window = &platform->windows[target.slot];
    int32_t width = (int32_t)lroundf(window->size.width);
    int32_t height = (int32_t)lroundf(window->size.height);
    uint32_t edge = EdgeAt(target.part, target.at, PartRect(target.part, width, height));
    mwinWaylandShowFrameCursor(platform, (uint32_t)target.slot, ShapeOf(edge));
    int hover = target.part == mwin_framePartCaption ? ButtonAt(window, target.at) : -1;
    if (hover != window->frame.hover)
    {
        window->frame.hover = (int8_t)hover;
        Redraw(platform, window);
    }
}

bool mwinWaylandFrameEnter(mwinWaylandPlatform* platform, struct wl_surface* surface,
                           mwinPosition position)
{
    int part = 0;
    if (FindPart(platform, surface, &part) < 0)
    {
        return false;
    }
    platform->pointer.framePart = surface;
    Hover(platform, TargetOf(platform, position));
    return true;
}

void mwinWaylandFrameMotion(mwinWaylandPlatform* platform, mwinPosition position)
{
    Target target = TargetOf(platform, position);
    if (target.slot >= 0)
    {
        platform->pointer.position = position;
        Hover(platform, target);
    }
}

void mwinWaylandFrameLeave(mwinWaylandPlatform* platform)
{
    Target target = TargetOf(platform, platform->pointer.position);
    platform->pointer.framePart = nullptr;
    if (target.slot < 0)
    {
        return;
    }
    mwinWaylandWindow* window = &platform->windows[target.slot];
    window->frame.pressed = -1;
    if (window->frame.hover >= 0)
    {
        window->frame.hover = -1;
        Redraw(platform, window);
    }
}

// Maximizes the window, or gives it back its size.
static void ToggleMaximized(mwinWaylandPlatform* platform, const mwinWaylandWindow* window)
{
    bool maximized = platform->context->windows[window->slot].state.mode == mwin_modeMaximized;
    (void)mwinWlRequest(&platform->api, window->toplevel,
                        maximized ? XDG_TOPLEVEL_UNSET_MAXIMIZED : XDG_TOPLEVEL_SET_MAXIMIZED,
                        nullptr, 0);
}

// A caption button was clicked.
static void Click(mwinWaylandPlatform* platform, mwinWaylandWindow* window, int button)
{
    mwinEvent event = {0};
    event.timeNs = mwinMonotonicNow();
    switch (button)
    {
    case buttonClose:
        event.type = mwin_eventCloseRequested;
        mwinPost(platform->context, window->slot, &event);
        break;
    case buttonMaximize:
        ToggleMaximized(platform, window);
        break;
    default:
        // No configure tells of it, as with a minimize request.
        (void)mwinWlRequest(&platform->api, window->toplevel, XDG_TOPLEVEL_SET_MINIMIZED, nullptr,
                            0);
        window->minimized = true;
        event.type = mwin_eventModeChanged;
        event.data.mode = mwin_modeMinimized;
        mwinPost(platform->context, window->slot, &event);
        break;
    }
}

// A press on the caption away from the buttons: a double click
// maximizes, the right button opens the window menu, and a drag moves.
static void PressCaption(mwinWaylandPlatform* platform, mwinWaylandWindow* window, uint32_t serial,
                         uint32_t button, Target target, uint64_t timeNs)
{
    const mwinWaylandApi* api = &platform->api;
    struct wl_proxy* toplevel = (struct wl_proxy*)window->toplevel;
    uint32_t version = mwinWlVersion(api, toplevel);
    if (button == BTN_RIGHT)
    {
        api->proxyMarshalFlags(toplevel, XDG_TOPLEVEL_SHOW_WINDOW_MENU, nullptr, version, 0,
                               platform->seat, serial, (int32_t)lroundf(target.at.x),
                               (int32_t)lroundf(target.at.y));
        return;
    }
    if (timeNs - window->frame.captionPressNs <= MWIN_DOUBLE_CLICK_NS)
    {
        window->frame.captionPressNs = 0;
        ToggleMaximized(platform, window);
        return;
    }
    window->frame.captionPressNs = timeNs;
    api->proxyMarshalFlags(toplevel, XDG_TOPLEVEL_MOVE, nullptr, version, 0, platform->seat,
                           serial);
}

bool mwinWaylandPressChrome(mwinWaylandPlatform* platform, uint32_t slot, uint32_t serial,
                            uint32_t button, mwinPosition at, uint8_t clicks)
{
    static const uint32_t edges[] = {
        XDG_TOPLEVEL_RESIZE_EDGE_LEFT,        XDG_TOPLEVEL_RESIZE_EDGE_RIGHT,
        XDG_TOPLEVEL_RESIZE_EDGE_TOP,         XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM,
        XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT,    XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT,
        XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT, XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT,
    };
    const mwinWaylandWindow* window = &platform->windows[slot];
    mwinHitKind kind = mwinHitAt(&platform->context->windows[slot], at.x, at.y);
    bool caption = kind == mwin_hitCaption;
    if (!mwinHitMoves(kind) || window->toplevel == nullptr || platform->seat == nullptr ||
        !(button == BTN_LEFT || (caption && button == BTN_RIGHT)))
    {
        return false;
    }
    const mwinWaylandApi* api = &platform->api;
    struct wl_proxy* toplevel = (struct wl_proxy*)window->toplevel;
    uint32_t version = mwinWlVersion(api, toplevel);
    if (caption && button == BTN_RIGHT)
    {
        api->proxyMarshalFlags(toplevel, XDG_TOPLEVEL_SHOW_WINDOW_MENU, nullptr, version, 0,
                               platform->seat, serial, (int32_t)lroundf(at.x),
                               (int32_t)lroundf(at.y));
    }
    else if (caption && clicks == 2)
    {
        ToggleMaximized(platform, window);
    }
    else if (caption)
    {
        api->proxyMarshalFlags(toplevel, XDG_TOPLEVEL_MOVE, nullptr, version, 0, platform->seat,
                               serial);
    }
    else
    {
        api->proxyMarshalFlags(toplevel, XDG_TOPLEVEL_RESIZE, nullptr, version, 0, platform->seat,
                               serial, edges[kind - mwin_hitLeft]);
    }
    return true;
}

void mwinWaylandFrameButton(mwinWaylandPlatform* platform, uint32_t serial, uint32_t button,
                            bool pressed, uint64_t timeNs)
{
    Target target = TargetOf(platform, platform->pointer.position);
    if (target.slot < 0 || platform->seat == nullptr)
    {
        return;
    }
    mwinWaylandWindow* window = &platform->windows[target.slot];
    int hover = target.part == mwin_framePartCaption ? ButtonAt(window, target.at) : -1;
    if (!pressed)
    {
        if (hover >= 0 && hover == window->frame.pressed && button == BTN_LEFT)
        {
            Click(platform, window, hover);
        }
        window->frame.pressed = -1;
        return;
    }
    if (hover >= 0)
    {
        window->frame.pressed = (int8_t)hover;
        return;
    }
    if (target.part == mwin_framePartCaption)
    {
        PressCaption(platform, window, serial, button, target, timeNs);
        return;
    }
    int32_t width = (int32_t)lroundf(window->size.width);
    int32_t height = (int32_t)lroundf(window->size.height);
    uint32_t edge = EdgeAt(target.part, target.at, PartRect(target.part, width, height));
    const mwinWaylandApi* api = &platform->api;
    if (button == BTN_LEFT && !window->frame.captionOnly)
    {
        api->proxyMarshalFlags((struct wl_proxy*)window->toplevel, XDG_TOPLEVEL_RESIZE, nullptr,
                               mwinWlVersion(api, window->toplevel), 0, platform->seat, serial,
                               edge);
    }
}
