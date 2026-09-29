// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Cursors over X11 windows.

#include "x11_cursor.h"

#include "allocator.h"

// Each shape's names in cursor themes: the CSS name, then the X11 name,
// which the core cursor font also has.
static const struct
{
    const char* name;
    const char* legacy;
} s_shapes[] = {
    {"default", "left_ptr"},
    {"text", "xterm"},
    {"pointer", "hand2"},
    {"crosshair", "crosshair"},
    {"move", "fleur"},
    {"ew-resize", "sb_h_double_arrow"},
    {"ns-resize", "sb_v_double_arrow"},
    {"nesw-resize", "fd_double_arrow"},
    {"nwse-resize", "bd_double_arrow"},
    {"not-allowed", "crossed_circle"},
    {"wait", "watch"},
    {"progress", "left_ptr_watch"},
};

void mwinX11StartCursors(mwinX11Platform* platform)
{
    const mwinX11Api* api = &platform->api;
    mwinX11Cursors* cursors = &platform->cursors;
    // An empty cursor: one pixel of a one-bit pixmap, masked out.
    xcb_pixmap_t pixmap = api->generateId(platform->connection);
    api->createPixmap(platform->connection, 1, pixmap, platform->screen->root, 1, 1);
    cursors->blank = api->generateId(platform->connection);
    api->createCursor(platform->connection, cursors->blank, pixmap, pixmap, 0, 0, 0, 0, 0, 0, 0, 0);
    api->freePixmap(platform->connection, pixmap);
    if (api->cursorLibrary != nullptr &&
        api->cursorContextNew(platform->connection, platform->screen, &cursors->context) < 0)
    {
        cursors->context = nullptr;
    }
}

void mwinX11StopCursors(mwinX11Platform* platform)
{
    const mwinX11Api* api = &platform->api;
    mwinX11Cursors* cursors = &platform->cursors;
    for (size_t i = 0; i < sizeof(cursors->shapes) / sizeof(cursors->shapes[0]); i++)
    {
        if (cursors->shapes[i] != XCB_CURSOR_NONE)
        {
            api->freeCursor(platform->connection, cursors->shapes[i]);
        }
    }
    if (cursors->blank != XCB_CURSOR_NONE)
    {
        api->freeCursor(platform->connection, cursors->blank);
    }
    if (cursors->context != nullptr)
    {
        api->cursorContextFree(cursors->context);
    }
    *cursors = (mwinX11Cursors){0};
}

static bool IsHidden(mwinCursorMode mode)
{
    return mode == mwin_cursorHidden || mode == mwin_cursorConfinedHidden ||
           mode == mwin_cursorCaptured;
}

// The cursor of a shape, loaded once; none (the root window's) for the
// default shape or one the theme lacks.
static xcb_cursor_t ShapeCursor(mwinX11Platform* platform, mwinCursorShape shape)
{
    mwinX11Cursors* cursors = &platform->cursors;
    const mwinX11Api* api = &platform->api;
    if (shape == mwin_shapeDefault || cursors->context == nullptr)
    {
        return XCB_CURSOR_NONE;
    }
    if (cursors->shapes[shape] == XCB_CURSOR_NONE)
    {
        cursors->shapes[shape] = api->cursorLoad(cursors->context, s_shapes[shape].name);
    }
    if (cursors->shapes[shape] == XCB_CURSOR_NONE)
    {
        cursors->shapes[shape] = api->cursorLoad(cursors->context, s_shapes[shape].legacy);
    }
    return cursors->shapes[shape];
}

// Sets the cursor the window's mode and shape call for.
static void Apply(mwinX11Platform* platform, const mwinX11Window* window)
{
    xcb_cursor_t cursor = IsHidden(window->cursorMode) ? platform->cursors.blank
                                                       : ShapeCursor(platform, window->cursorShape);
    platform->api.changeWindowAttributes(platform->connection, window->window, XCB_CW_CURSOR,
                                         &cursor);
}

// Grabs the pointer into the window, or lets it go.
static void Confine(mwinX11Platform* platform, mwinX11Window* window, bool confined)
{
    const mwinX11Api* api = &platform->api;
    if (!confined)
    {
        if (window->confined)
        {
            api->ungrabPointer(platform->connection, XCB_CURRENT_TIME);
        }
        window->confined = false;
        return;
    }
    const uint16_t events = XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                            XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_ENTER_WINDOW |
                            XCB_EVENT_MASK_LEAVE_WINDOW;
    xcb_grab_pointer_reply_t* reply = api->grabPointerReply(
        platform->connection,
        api->grabPointer(platform->connection, 1, window->window, events, XCB_GRAB_MODE_ASYNC,
                         XCB_GRAB_MODE_ASYNC, window->window, XCB_CURSOR_NONE, XCB_CURRENT_TIME),
        nullptr);
    // A window that is not viewable cannot hold the grab; its next focus
    // tries again.
    window->confined = reply != nullptr && reply->status == XCB_GRAB_STATUS_SUCCESS;
    mwinReleaseSystemMemory(reply);
}

static bool IsConfined(mwinCursorMode mode)
{
    return mode == mwin_cursorConfined || mode == mwin_cursorConfinedHidden ||
           mode == mwin_cursorCaptured;
}

mwinOutcome mwinX11SetCursorMode(mwinX11Platform* platform, uint32_t slot, mwinCursorMode mode)
{
    if (mode == mwin_cursorCaptured && platform->xinputOpcode == 0)
    {
        return mwin_outcomeUnsupported;
    }
    mwinX11Window* window = &platform->windows[slot];
    window->cursorMode = mode;
    Apply(platform, window);
    bool focused = platform->context->windows[slot].state.focused;
    Confine(platform, window, IsConfined(mode) && focused);
    if (mode == mwin_cursorCaptured && focused)
    {
        // Kept in the middle, the pointer never meets an edge.
        platform->api.warpPointer(platform->connection, XCB_NONE, window->window, 0, 0, 0, 0,
                                  (int16_t)(window->width / 2), (int16_t)(window->height / 2));
    }
    return mwin_outcomeDone;
}

mwinOutcome mwinX11SetCursorShape(mwinX11Platform* platform, uint32_t slot, mwinCursorShape shape)
{
    if (shape != mwin_shapeDefault && platform->cursors.context == nullptr)
    {
        return mwin_outcomeUnsupported;
    }
    mwinX11Window* window = &platform->windows[slot];
    window->cursorShape = shape;
    Apply(platform, window);
    return mwin_outcomeDone;
}

void mwinX11CursorFocus(mwinX11Platform* platform, uint32_t slot, bool focused)
{
    mwinX11Window* window = &platform->windows[slot];
    if (IsConfined(window->cursorMode))
    {
        Confine(platform, window, focused);
    }
}
