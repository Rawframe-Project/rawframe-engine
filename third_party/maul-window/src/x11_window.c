// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// X11 windows.

#include "x11_window.h"

#include "allocator.h"
#include "x11_chrome.h"
#include "x11_clipboard.h"
#include "x11_cursor.h"
#include "x11_icon.h"
#include "x11_input.h"
#include "x11_output.h"

#include <math.h>
#include <string.h>

#define MODE_DEADLINE_NS 500000000u

// WM_NORMAL_HINTS: its flags and the places of its fields.
enum
{
    hintMinimumSize = 1 << 4,
    hintMaximumSize = 1 << 5,
    hintAspect = 1 << 7,
    HINT_FIELDS = 18,
};

// _NET_WM_STATE client message actions.
enum
{
    stateRemove = 0,
    stateAdd = 1,
};

static mwinX11Platform* PlatformOf(const mwinContext* context)
{
    return (mwinX11Platform*)context->backendData;
}

static uint32_t ToPixels(const mwinX11Platform* platform, float logical)
{
    long pixels = lroundf(logical * platform->scale);
    return pixels > 0 ? (uint32_t)pixels : 1u;
}

static void Post(mwinX11Platform* platform, uint32_t slot, mwinEvent event)
{
    event.timeNs = mwinMonotonicNow();
    mwinPost(platform->context, slot, &event);
}

static void PostType(mwinX11Platform* platform, uint32_t slot, mwinEventType type)
{
    Post(platform, slot, (mwinEvent){.type = type});
}

static void PostSize(mwinX11Platform* platform, const mwinX11Window* window)
{
    float scale = platform->scale;
    mwinEvent event = {.type = mwin_eventResized};
    event.data.size = (mwinSize){(float)window->width / scale, (float)window->height / scale};
    Post(platform, window->slot, event);
    event.type = mwin_eventPixelSizeChanged;
    event.data.pixelSize = (mwinPixelSize){window->width, window->height};
    Post(platform, window->slot, event);
}

// Reports the monitor under the window's center when it changed.
static void PostMonitor(mwinX11Platform* platform, mwinX11Window* window)
{
    int32_t monitor = mwinX11MonitorAt(platform, window->x + (int32_t)window->width / 2,
                                       window->y + (int32_t)window->height / 2);
    if (monitor < 0 || monitor == window->monitor)
    {
        return;
    }
    window->monitor = monitor;
    mwinEvent event = {.type = mwin_eventDisplayChanged};
    event.data.monitor = mwinMonitorIdOf(platform->context, (uint32_t)monitor);
    Post(platform, window->slot, event);
}

static void SetProperty(const mwinX11Platform* platform, xcb_window_t window, xcb_atom_t property,
                        xcb_atom_t type, uint8_t format, uint32_t count, const void* data)
{
    platform->api.changeProperty(platform->connection, XCB_PROP_MODE_REPLACE, window, property,
                                 type, format, count, data);
}

static void SetTitle(const mwinX11Platform* platform, xcb_window_t window, const char* title,
                     size_t length)
{
    xcb_atom_t utf8 = platform->atoms[mwin_atomUtf8String];
    SetProperty(platform, window, platform->atoms[mwin_atomNetWmName], utf8, 8, (uint32_t)length,
                title);
    SetProperty(platform, window, XCB_ATOM_WM_NAME, utf8, 8, (uint32_t)length, title);
}

// Writes the size bounds and aspect ratio a window manager keeps to; a
// window that does not resize is bounded to its size.
static void SetNormalHints(const mwinX11Platform* platform, const mwinX11Window* window,
                           mwinWindowStyle style)
{
    uint32_t hints[HINT_FIELDS] = {0};
    bool fixed = (style & mwin_styleResizable) == 0;
    uint32_t minimumWidth = fixed ? window->width : window->minimumWidth;
    uint32_t minimumHeight = fixed ? window->height : window->minimumHeight;
    uint32_t maximumWidth = fixed ? window->width : window->maximumWidth;
    uint32_t maximumHeight = fixed ? window->height : window->maximumHeight;
    if (minimumWidth > 0 || minimumHeight > 0)
    {
        hints[0] |= hintMinimumSize;
        hints[5] = minimumWidth;
        hints[6] = minimumHeight;
    }
    if (maximumWidth > 0 || maximumHeight > 0)
    {
        hints[0] |= hintMaximumSize;
        hints[7] = maximumWidth > 0 ? maximumWidth : INT16_MAX;
        hints[8] = maximumHeight > 0 ? maximumHeight : INT16_MAX;
    }
    if (window->aspectWidth > 0)
    {
        hints[0] |= hintAspect;
        hints[11] = hints[13] = window->aspectWidth;
        hints[12] = hints[14] = window->aspectHeight;
    }
    SetProperty(platform, window->window, XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, 32,
                HINT_FIELDS, hints);
}

// Asks the window manager for decorations or none.
// The window manager decorates a decorated window, and none of custom
// chrome, whose frame the program draws.
static bool IsDecorated(mwinWindowStyle style)
{
    return (style & mwin_styleDecorated) != 0 && (style & mwin_styleCustomChrome) == 0;
}

static void SetDecorated(const mwinX11Platform* platform, xcb_window_t window, bool decorated)
{
    // flags (decorations), functions, decorations, input mode, status.
    uint32_t hints[5] = {1u << 1, 0, decorated ? 1u : 0u, 0, 0};
    xcb_atom_t atom = platform->atoms[mwin_atomMotifWmHints];
    SetProperty(platform, window, atom, atom, 32, 5, hints);
}

static void ChangeState(const mwinX11Platform* platform, xcb_window_t window, uint32_t action,
                        xcb_atom_t first, xcb_atom_t second)
{
    const uint32_t data[5] = {action, first, second, 1, 0};
    mwinX11SendToRoot(platform, window, platform->atoms[mwin_atomNetWmState], data);
}

// The _NET_WM_STATE atoms of a mode, before a window is mapped.
static uint32_t StateAtoms(const mwinX11Platform* platform, mwinWindowMode mode, bool above,
                           xcb_atom_t atoms[4])
{
    uint32_t count = 0;
    if (mode == mwin_modeBorderlessFullscreen)
    {
        atoms[count++] = platform->atoms[mwin_atomNetWmStateFullscreen];
    }
    if (mode == mwin_modeMaximized)
    {
        atoms[count++] = platform->atoms[mwin_atomNetWmStateMaximizedVert];
        atoms[count++] = platform->atoms[mwin_atomNetWmStateMaximizedHorz];
    }
    if (above)
    {
        atoms[count++] = platform->atoms[mwin_atomNetWmStateAbove];
    }
    return count;
}

static bool IsPopup(const mwinX11Platform* platform, uint32_t slot)
{
    return platform->context->windows[slot].def.kind != mwin_windowNormal;
}

// The window's owner, or nullptr: live while the window is.
static const mwinX11Window* OwnerOf(const mwinX11Platform* platform, uint32_t slot)
{
    mwinWindowId owner = platform->context->windows[slot].def.owner;
    return owner.index1 != 0 ? &platform->windows[owner.index1 - 1] : nullptr;
}

// An owned window is transient for its owner's, a dialog of it or a
// popup of its kind.
static void SetOwnership(const mwinX11Platform* platform, const mwinWindow* core,
                         const mwinX11Window* window)
{
    const mwinX11Window* owner = OwnerOf(platform, window->slot);
    if (owner == nullptr)
    {
        return;
    }
    SetProperty(platform, window->window, XCB_ATOM_WM_TRANSIENT_FOR, XCB_ATOM_WINDOW, 32, 1,
                &owner->window);
    static const int types[] = {mwin_atomNetWmWindowTypeDialog, mwin_atomNetWmWindowTypePopupMenu,
                                mwin_atomNetWmWindowTypeTooltip};
    xcb_atom_t type = platform->atoms[types[core->def.kind]];
    SetProperty(platform, window->window, platform->atoms[mwin_atomNetWmWindowType], XCB_ATOM_ATOM,
                32, 1, &type);
}

// Sets what a window manager reads before it maps a window.
static void SetInitialProperties(const mwinX11Platform* platform, const mwinWindow* core,
                                 const mwinX11Window* window)
{
    xcb_atom_t protocols[2] = {platform->atoms[mwin_atomWmDeleteWindow],
                               platform->atoms[mwin_atomNetWmPing]};
    SetProperty(platform, window->window, platform->atoms[mwin_atomWmProtocols], XCB_ATOM_ATOM, 32,
                2, protocols);
    // Drags come by XDND 5 (x11_drop.c).
    uint32_t xdnd = 5;
    SetProperty(platform, window->window, platform->atoms[mwin_atomXdndAware], XCB_ATOM_ATOM, 32, 1,
                &xdnd);
    SetTitle(platform, window->window, core->title, core->titleLength);
    SetOwnership(platform, core, window);
    SetNormalHints(platform, window, core->def.style);
    if (!IsDecorated(core->def.style))
    {
        SetDecorated(platform, window->window, false);
    }
    xcb_atom_t states[4];
    uint32_t count = StateAtoms(platform, core->def.mode,
                                (core->def.style & mwin_styleAlwaysOnTop) != 0, states);
    if (count > 0)
    {
        SetProperty(platform, window->window, platform->atoms[mwin_atomNetWmState], XCB_ATOM_ATOM,
                    32, count, states);
    }
}

// What a new window is, as it is made: the X server made it at once.
static void Establish(mwinX11Platform* platform, mwinX11Window* window)
{
    mwinContext* context = platform->context;
    PostType(platform, window->slot, mwin_eventWindowCreated);
    PostMonitor(platform, window);
    mwinEvent scale = {.type = mwin_eventScaleChanged};
    scale.data.scale = (mwinScaleChange){platform->scale, context->windows[window->slot].def.size};
    Post(platform, window->slot, scale);
    PostSize(platform, window);
    // The window manager tells of another mode when it applies one.
    mwinEvent mode = {.type = mwin_eventModeChanged};
    mode.data.mode = mwin_modeWindowed;
    Post(platform, window->slot, mode);
    if (IsPopup(platform, window->slot))
    {
        // A popup is where it was put against its owner.
        mwinEvent moved = {.type = mwin_eventMoved};
        moved.data.position = context->windows[window->slot].def.position;
        Post(platform, window->slot, moved);
    }
}

// Places a popup against its owner's corner, in pixels, without the
// window manager: it is override-redirect.
static void PlacePopup(mwinX11Platform* platform, mwinX11Window* window, mwinPosition position)
{
    const mwinX11Window* owner = OwnerOf(platform, window->slot);
    window->offsetX = (int32_t)lroundf(position.x * platform->scale);
    window->offsetY = (int32_t)lroundf(position.y * platform->scale);
    window->x = owner->x + window->offsetX;
    window->y = owner->y + window->offsetY;
}

void mwinX11CreateWindow(mwinContext* context, uint32_t slot)
{
    mwinX11Platform* platform = PlatformOf(context);
    const mwinX11Api* api = &platform->api;
    const mwinWindow* core = &context->windows[slot];
    mwinX11Window* window = &platform->windows[slot];
    *window = (mwinX11Window){.platform = platform, .slot = slot, .monitor = -1, .modeRequest = -1};
    window->width = ToPixels(platform, core->def.size.width);
    window->height = ToPixels(platform, core->def.size.height);
    window->window = api->generateId(platform->connection);
    bool popup = IsPopup(platform, slot);
    if (popup)
    {
        PlacePopup(platform, window, core->def.position);
    }
    const uint32_t values[4] = {platform->screen->black_pixel, 0, popup,
                                XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY |
                                    XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_PROPERTY_CHANGE |
                                    XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
                                    XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                                    XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_ENTER_WINDOW |
                                    XCB_EVENT_MASK_LEAVE_WINDOW};
    xcb_generic_error_t* error = api->requestCheck(
        platform->connection,
        api->createWindowChecked(platform->connection, XCB_COPY_FROM_PARENT, window->window,
                                 platform->screen->root, (int16_t)window->x, (int16_t)window->y,
                                 (uint16_t)window->width, (uint16_t)window->height, 0,
                                 XCB_WINDOW_CLASS_INPUT_OUTPUT, platform->screen->root_visual,
                                 XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL |
                                     XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK,
                                 values));
    int32_t request =
        mwinFindActiveRequest(core, context->limits.requestsPerWindow, mwin_requestCreate);
    if (error != nullptr)
    {
        mwinReleaseSystemMemory(error);
        window->window = 0;
        mwinComplete(context, slot, (uint32_t)request, mwin_outcomeFailed);
        return;
    }
    SetInitialProperties(platform, core, window);
    mwinX11SelectPointer(platform, window->window);
    Establish(platform, window);
    if (core->def.visible)
    {
        api->mapWindow(platform->connection, window->window);
    }
    mwinComplete(context, slot, (uint32_t)request, mwin_outcomeDone);
}

void mwinX11DestroyWindow(mwinContext* context, uint32_t slot)
{
    mwinX11Platform* platform = PlatformOf(context);
    mwinX11Window* window = &platform->windows[slot];
    mwinX11CursorFocus(platform, slot, false);
    mwinX11ForgetPointer(platform, slot);
    if (window->window != 0)
    {
        platform->api.destroyWindow(platform->connection, window->window);
    }
    *window = (mwinX11Window){.monitor = -1, .modeRequest = -1};
}

// Asks the window manager for a mode, which _NET_WM_STATE answers.
static int RequestMode(mwinX11Platform* platform, mwinX11Window* window, uint32_t index,
                       mwinWindowMode mode)
{
    const xcb_atom_t* atoms = platform->atoms;
    if (!platform->windowManager)
    {
        return mwin_outcomeUnsupported;
    }
    if (mode == mwin_modeMinimized)
    {
        // ICCCM's IconicState.
        const uint32_t data[5] = {3, 0, 0, 0, 0};
        mwinX11SendToRoot(platform, window->window, atoms[mwin_atomWmChangeState], data);
    }
    else
    {
        bool fullscreen = mode == mwin_modeBorderlessFullscreen;
        bool maximized = mode == mwin_modeMaximized;
        ChangeState(platform, window->window, fullscreen ? stateAdd : stateRemove,
                    atoms[mwin_atomNetWmStateFullscreen], 0);
        ChangeState(platform, window->window, maximized ? stateAdd : stateRemove,
                    atoms[mwin_atomNetWmStateMaximizedVert],
                    atoms[mwin_atomNetWmStateMaximizedHorz]);
    }
    window->modeRequest = (int32_t)index;
    window->modeDeadlineNs = mwinMonotonicNow() + MODE_DEADLINE_NS;
    return -1;
}

static void Configure(const mwinX11Platform* platform, const mwinX11Window* window, uint16_t mask,
                      const uint32_t* values)
{
    platform->api.configureWindow(platform->connection, window->window, mask, values);
}

// Focus through the window manager, or directly without one or for a
// popup, which the window manager does not see.
static void Focus(const mwinX11Platform* platform, const mwinX11Window* window)
{
    if (platform->windowManager && !IsPopup(platform, window->slot))
    {
        const uint32_t data[5] = {1, XCB_CURRENT_TIME, 0, 0, 0};
        mwinX11SendToRoot(platform, window->window, platform->atoms[mwin_atomNetActiveWindow],
                          data);
        return;
    }
    platform->api.setInputFocus(platform->connection, XCB_INPUT_FOCUS_POINTER_ROOT, window->window,
                                XCB_CURRENT_TIME);
}

static void SetOpacity(const mwinX11Platform* platform, const mwinX11Window* window, float opacity)
{
    xcb_atom_t atom = platform->atoms[mwin_atomNetWmWindowOpacity];
    if (opacity >= 1.0f)
    {
        platform->api.deleteProperty(platform->connection, window->window, atom);
        return;
    }
    uint32_t value = (uint32_t)((double)opacity * 4294967295.0);
    SetProperty(platform, window->window, atom, XCB_ATOM_CARDINAL, 32, 1, &value);
}

static void SetStyle(mwinX11Platform* platform, mwinX11Window* window, mwinWindowStyle style)
{
    SetDecorated(platform, window->window, IsDecorated(style));
    SetNormalHints(platform, window, style);
    ChangeState(platform, window->window,
                (style & mwin_styleAlwaysOnTop) != 0 ? stateAdd : stateRemove,
                platform->atoms[mwin_atomNetWmStateAbove], 0);
}

// Carries out the requests that change the window's geometry.
static int CarryOutGeometry(mwinX11Platform* platform, mwinX11Window* window,
                            const mwinRequest* request)
{
    if (request->kind == mwin_requestSize)
    {
        const uint32_t size[2] = {ToPixels(platform, request->value.size.width),
                                  ToPixels(platform, request->value.size.height)};
        Configure(platform, window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, size);
        return mwin_outcomeDone;
    }
    if (request->kind == mwin_requestPosition && IsPopup(platform, window->slot))
    {
        PlacePopup(platform, window, request->value.position);
        const uint32_t place[2] = {(uint32_t)window->x, (uint32_t)window->y};
        Configure(platform, window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, place);
        // Its own configure event then finds it where it was put.
        mwinEvent moved = {.type = mwin_eventMoved};
        moved.data.position = request->value.position;
        Post(platform, window->slot, moved);
        return mwin_outcomeDone;
    }
    if (request->kind == mwin_requestPosition)
    {
        const uint32_t place[2] = {
            (uint32_t)(int32_t)lroundf(request->value.position.x * platform->scale),
            (uint32_t)(int32_t)lroundf(request->value.position.y * platform->scale)};
        Configure(platform, window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, place);
        return mwin_outcomeDone;
    }
    if (request->kind == mwin_requestSizeLimits)
    {
        mwinSize minimum = request->value.limits.minimum;
        mwinSize maximum = request->value.limits.maximum;
        window->minimumWidth = minimum.width > 0.0f ? ToPixels(platform, minimum.width) : 0;
        window->minimumHeight = minimum.height > 0.0f ? ToPixels(platform, minimum.height) : 0;
        window->maximumWidth = maximum.width > 0.0f ? ToPixels(platform, maximum.width) : 0;
        window->maximumHeight = maximum.height > 0.0f ? ToPixels(platform, maximum.height) : 0;
    }
    else
    {
        window->aspectWidth = request->value.aspect.width;
        window->aspectHeight = request->value.aspect.height;
    }
    SetNormalHints(platform, window, platform->context->windows[window->slot].state.style);
    return mwin_outcomeDone;
}

// Carries out a request: its outcome, or -1 for one the window manager
// answers later.
static int CarryOut(mwinX11Platform* platform, mwinX11Window* window, mwinWindow* core,
                    uint32_t index)
{
    const mwinRequest* request = &core->requests[index];
    const mwinX11Api* api = &platform->api;
    switch (request->kind)
    {
    case mwin_requestTitle:
        SetTitle(platform, window->window, core->pendingTitle, core->pendingTitleLength);
        memmove(core->title, core->pendingTitle, core->pendingTitleLength);
        core->titleLength = core->pendingTitleLength;
        return mwin_outcomeDone;
    case mwin_requestSize:
    case mwin_requestPosition:
    case mwin_requestSizeLimits:
    case mwin_requestAspectRatio:
        return CarryOutGeometry(platform, window, request);
    case mwin_requestMode:
        return RequestMode(platform, window, index, request->value.mode);
    case mwin_requestVisible:
        (request->value.visible ? api->mapWindow : api->unmapWindow)(platform->connection,
                                                                     window->window);
        return mwin_outcomeDone;
    case mwin_requestFocus:
        if (core->def.kind == mwin_windowTooltip)
        {
            return mwin_outcomeDenied;
        }
        Focus(platform, window);
        return mwin_outcomeDone;
    case mwin_requestStyle:
        SetStyle(platform, window, request->value.code);
        return mwin_outcomeDone;
    case mwin_requestOpacity:
        SetOpacity(platform, window, request->value.opacity);
        return mwin_outcomeDone;
    case mwin_requestCursorMode:
        return mwinX11SetCursorMode(platform, window->slot, request->value.code);
    case mwin_requestCursorShape:
        return mwinX11SetCursorShape(platform, window->slot, request->value.code);
    case mwin_requestTextInput:
        // Keys type text whether asked or not; there is no input method.
        return mwin_outcomeDone;
    case mwin_requestClipboardWrite:
        return mwinX11WriteClipboard(platform);
    case mwin_requestClipboardRead:
        return mwinX11ReadClipboard(platform);
    case mwin_requestOpenUrl:
        return mwinLinuxOpenUrl(&platform->services, window->slot, index);
    case mwin_requestRevealFile:
        return mwinLinuxRevealFile(&platform->services, window->slot, index);
    case mwin_requestKeepAwake:
        // The pump keeps the display awake from the windows' state.
        return mwinLinuxCanKeepAwake(&platform->services);
    case mwin_requestIcon:
        return mwinX11SetIcon(platform, window->window, request);
    case mwin_requestHitRegions:
        // Presses read them (x11_chrome.c).
        return mwin_outcomeDone;
    case mwin_requestFileDialog:
    {
        char parent[24];
        (void)snprintf(parent, sizeof(parent), "x11:%x", window->window);
        return mwinDialogsOpen(&platform->services.dialogs, window->slot, index, parent);
    }
    default:
        return mwin_outcomeUnsupported;
    }
}

void mwinX11Submit(mwinContext* context, uint32_t slot, uint32_t request)
{
    mwinX11Platform* platform = PlatformOf(context);
    int outcome = CarryOut(platform, &platform->windows[slot], &context->windows[slot], request);
    if (outcome >= 0)
    {
        mwinComplete(context, slot, request, (mwinOutcome)outcome);
    }
}

// The window's place on the desktop: a synthetic event from the window
// manager has it, and the X server is asked otherwise.
static void Locate(const mwinX11Platform* platform, mwinX11Window* window,
                   const xcb_configure_notify_event_t* event)
{
    const mwinX11Api* api = &platform->api;
    if ((event->response_type & 0x80) != 0)
    {
        window->x = event->x;
        window->y = event->y;
        return;
    }
    xcb_translate_coordinates_reply_t* reply = api->translateCoordinatesReply(
        platform->connection,
        api->translateCoordinates(platform->connection, window->window, platform->screen->root, 0,
                                  0),
        nullptr);
    if (reply != nullptr)
    {
        window->x = reply->dst_x;
        window->y = reply->dst_y;
        mwinReleaseSystemMemory(reply);
    }
}

// The popups of a window that moved keep their places against it.
static void MovePopups(const mwinX11Platform* platform, const mwinX11Window* window)
{
    const mwinContext* context = platform->context;
    mwinWindowId id = mwinWindowIdOf(context, window->slot);
    for (uint32_t slot = 0; slot < context->limits.windows; slot++)
    {
        const mwinX11Window* popup = &platform->windows[slot];
        mwinWindowId owner = context->windows[slot].def.owner;
        if (popup->window != 0 && IsPopup(platform, slot) && owner.index1 == id.index1 &&
            owner.generation == id.generation)
        {
            const uint32_t place[2] = {(uint32_t)(window->x + popup->offsetX),
                                       (uint32_t)(window->y + popup->offsetY)};
            Configure(platform, popup, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, place);
        }
    }
}

// A popup's place against its owner, reported when it changed.
static void PostOffset(mwinX11Platform* platform, mwinX11Window* window)
{
    const mwinX11Window* owner = OwnerOf(platform, window->slot);
    int32_t x = window->x - owner->x;
    int32_t y = window->y - owner->y;
    if (x == window->offsetX && y == window->offsetY)
    {
        return;
    }
    window->offsetX = x;
    window->offsetY = y;
    mwinEvent moved = {.type = mwin_eventMoved};
    moved.data.position = (mwinPosition){(float)x / platform->scale, (float)y / platform->scale};
    Post(platform, window->slot, moved);
}

static void OnConfigure(mwinX11Platform* platform, const xcb_configure_notify_event_t* event)
{
    int32_t slot = mwinX11SlotOf(platform, event->window);
    if (slot < 0)
    {
        return;
    }
    mwinX11Window* window = &platform->windows[slot];
    int32_t x = window->x;
    int32_t y = window->y;
    Locate(platform, window, event);
    if (event->width != window->width || event->height != window->height)
    {
        window->width = event->width;
        window->height = event->height;
        PostSize(platform, window);
    }
    if ((window->x != x || window->y != y) && IsPopup(platform, window->slot))
    {
        PostOffset(platform, window);
    }
    else if (window->x != x || window->y != y)
    {
        MovePopups(platform, window);
        mwinEvent moved = {.type = mwin_eventMoved};
        moved.data.position =
            (mwinPosition){(float)window->x / platform->scale, (float)window->y / platform->scale};
        Post(platform, window->slot, moved);
    }
    PostMonitor(platform, window);
}

// The mode _NET_WM_STATE gives a window.
static mwinWindowMode ReadMode(const mwinX11Platform* platform, const mwinX11Window* window)
{
    const mwinX11Api* api = &platform->api;
    const xcb_atom_t* atoms = platform->atoms;
    xcb_get_property_reply_t* reply =
        api->getPropertyReply(platform->connection,
                              api->getProperty(platform->connection, 0, window->window,
                                               atoms[mwin_atomNetWmState], XCB_ATOM_ATOM, 0, 64),
                              nullptr);
    if (reply == nullptr)
    {
        return mwin_modeWindowed;
    }
    const xcb_atom_t* states = api->getPropertyValue(reply);
    int count = api->getPropertyValueLength(reply) / (int)sizeof(xcb_atom_t);
    bool fullscreen = false;
    bool hidden = false;
    int maximized = 0;
    for (int i = 0; i < count; i++)
    {
        fullscreen |= states[i] == atoms[mwin_atomNetWmStateFullscreen];
        hidden |= states[i] == atoms[mwin_atomNetWmStateHidden];
        maximized += states[i] == atoms[mwin_atomNetWmStateMaximizedVert] ||
                     states[i] == atoms[mwin_atomNetWmStateMaximizedHorz];
    }
    mwinReleaseSystemMemory(reply);
    return hidden ? mwin_modeMinimized
                  : (fullscreen ? mwin_modeBorderlessFullscreen
                                : (maximized == 2 ? mwin_modeMaximized : mwin_modeWindowed));
}

// The window manager changed the window's state.
static void OnState(mwinX11Platform* platform, uint32_t slot)
{
    mwinX11Window* window = &platform->windows[slot];
    mwinWindow* core = &platform->context->windows[slot];
    mwinWindowMode mode = ReadMode(platform, window);
    if (mode != core->state.mode)
    {
        mwinEvent event = {.type = mwin_eventModeChanged};
        event.data.mode = mode;
        Post(platform, slot, event);
    }
    if (window->modeRequest >= 0 && core->requests[window->modeRequest].value.mode == mode)
    {
        mwinComplete(platform->context, slot, (uint32_t)window->modeRequest, mwin_outcomeDone);
        window->modeRequest = -1;
    }
}

static void OnClientMessage(mwinX11Platform* platform, const xcb_client_message_event_t* event)
{
    int32_t slot = mwinX11SlotOf(platform, event->window);
    if (slot < 0 || event->type != platform->atoms[mwin_atomWmProtocols])
    {
        return;
    }
    xcb_atom_t protocol = event->data.data32[0];
    if (protocol == platform->atoms[mwin_atomWmDeleteWindow])
    {
        PostType(platform, (uint32_t)slot, mwin_eventCloseRequested);
    }
    else if (protocol == platform->atoms[mwin_atomNetWmPing])
    {
        // The answer goes back to the root window.
        xcb_client_message_event_t pong = *event;
        pong.response_type = XCB_CLIENT_MESSAGE;
        pong.window = platform->screen->root;
        platform->api.sendEvent(platform->connection, 0, platform->screen->root,
                                XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                                    XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
                                (const char*)&pong);
    }
}

// Focus that moves inside the window or with a grab is not the window's.
static void OnFocus(mwinX11Platform* platform, const xcb_focus_in_event_t* event, bool gained)
{
    int32_t slot = mwinX11SlotOf(platform, event->event);
    if (slot < 0 || event->mode == XCB_NOTIFY_MODE_GRAB || event->mode == XCB_NOTIFY_MODE_UNGRAB ||
        event->detail == XCB_NOTIFY_DETAIL_POINTER)
    {
        return;
    }
    if (!gained)
    {
        mwinX11ForgetKeys(platform);
    }
    PostType(platform, (uint32_t)slot, gained ? mwin_eventFocusGained : mwin_eventFocusLost);
    mwinX11CursorFocus(platform, (uint32_t)slot, gained);
    if (!gained && platform->context->windows[slot].def.kind == mwin_windowMenu)
    {
        // A menu is dismissed when the keyboard goes elsewhere.
        PostType(platform, (uint32_t)slot, mwin_eventCloseRequested);
    }
}

// A menu takes the keyboard once it is mapped; the window manager does
// not give it.
static void OnMap(mwinX11Platform* platform, uint32_t slot, bool shown)
{
    PostType(platform, slot, shown ? mwin_eventShown : mwin_eventHidden);
    if (shown && platform->context->windows[slot].def.kind == mwin_windowMenu)
    {
        platform->api.setInputFocus(platform->connection, XCB_INPUT_FOCUS_PARENT,
                                    platform->windows[slot].window, XCB_CURRENT_TIME);
    }
}

bool mwinX11HandleWindowEvent(mwinX11Platform* platform, const xcb_generic_event_t* event)
{
    switch (event->response_type & 0x7F)
    {
    case XCB_CONFIGURE_NOTIFY:
        OnConfigure(platform, (const xcb_configure_notify_event_t*)event);
        return true;
    case XCB_MAP_NOTIFY:
    case XCB_UNMAP_NOTIFY:
    {
        const xcb_map_notify_event_t* map = (const xcb_map_notify_event_t*)event;
        int32_t slot = mwinX11SlotOf(platform, map->window);
        if (slot >= 0)
        {
            OnMap(platform, (uint32_t)slot, (event->response_type & 0x7F) == XCB_MAP_NOTIFY);
        }
        return true;
    }
    case XCB_FOCUS_IN:
    case XCB_FOCUS_OUT:
        OnFocus(platform, (const xcb_focus_in_event_t*)event,
                (event->response_type & 0x7F) == XCB_FOCUS_IN);
        return true;
    case XCB_CLIENT_MESSAGE:
        OnClientMessage(platform, (const xcb_client_message_event_t*)event);
        return true;
    case XCB_PROPERTY_NOTIFY:
    {
        const xcb_property_notify_event_t* property = (const xcb_property_notify_event_t*)event;
        int32_t slot = mwinX11SlotOf(platform, property->window);
        if (slot >= 0 && property->atom == platform->atoms[mwin_atomNetWmState])
        {
            OnState(platform, (uint32_t)slot);
        }
        return true;
    }
    default:
        return false;
    }
}

void mwinX11CheckDeadlines(mwinX11Platform* platform)
{
    uint64_t now = mwinMonotonicNow();
    for (uint32_t i = 0; i < platform->context->limits.windows; i++)
    {
        mwinX11Window* window = &platform->windows[i];
        if (window->modeRequest >= 0 && now >= window->modeDeadlineNs)
        {
            mwinComplete(platform->context, i, (uint32_t)window->modeRequest, mwin_outcomeDenied);
            window->modeRequest = -1;
        }
    }
}
