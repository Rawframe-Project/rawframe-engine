// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The X11 backend: the connection, the atoms, the scale and the pump. A
// context opens XCB for itself and connects to the display DISPLAY
// names. The scale comes from Xft.dpi in the root window's resources, as
// desktops set it; RandR 1.5 lists the monitors and tells of their
// changes. The pump never waits: it sends what the program asked for and
// handles every event that has arrived.

#include "allocator.h"
#include "backend.h"
#include "core.h"
#include "x11.h"
#include "x11_api.h"
#include "x11_clipboard.h"
#include "x11_cursor.h"
#include "x11_drop.h"
#include "x11_input.h"
#include "x11_output.h"
#include "x11_window.h"

#include <string.h>

// The pixels per inch of a scale of 1.
#define BASE_DPI 96.0f

static const char* const s_atomNames[MWIN_X11_ATOMS] = {
    "WM_PROTOCOLS",
    "WM_DELETE_WINDOW",
    "WM_STATE",
    "WM_CHANGE_STATE",
    "_NET_WM_PING",
    "_NET_WM_NAME",
    "_NET_WM_PID",
    "UTF8_STRING",
    "_NET_WM_STATE",
    "_NET_WM_STATE_FULLSCREEN",
    "_NET_WM_STATE_MAXIMIZED_VERT",
    "_NET_WM_STATE_MAXIMIZED_HORZ",
    "_NET_WM_STATE_ABOVE",
    "_NET_WM_STATE_HIDDEN",
    "_NET_WM_WINDOW_OPACITY",
    "_NET_WM_ICON",
    "_NET_ACTIVE_WINDOW",
    "_NET_SUPPORTING_WM_CHECK",
    "_MOTIF_WM_HINTS",
    "CLIPBOARD",
    "TARGETS",
    "TIMESTAMP",
    "INCR",
    "text/plain;charset=utf-8",
    "_MAUL_SELECTION",
    "XdndAware",
    "XdndEnter",
    "XdndPosition",
    "XdndStatus",
    "XdndLeave",
    "XdndDrop",
    "XdndFinished",
    "XdndSelection",
    "XdndTypeList",
    "XdndActionCopy",
    "text/uri-list",
    "_NET_WM_WINDOW_TYPE",
    "_NET_WM_WINDOW_TYPE_DIALOG",
    "_NET_WM_WINDOW_TYPE_POPUP_MENU",
    "_NET_WM_WINDOW_TYPE_TOOLTIP",
    "_NET_WM_MOVERESIZE",
};

static mwinX11Platform* PlatformOf(const mwinContext* context)
{
    return (mwinX11Platform*)context->backendData;
}

static size_t PlatformBytes(const mwinContext* context)
{
    const mwinLimits* limits = &context->limits;
    return sizeof(mwinX11Platform) + limits->windows * sizeof(mwinX11Window) +
           limits->monitors * sizeof(mwinX11Output);
}

// Interns every atom, the requests sent together before any reply.
static bool InternAtoms(mwinX11Platform* platform)
{
    const mwinX11Api* api = &platform->api;
    xcb_intern_atom_cookie_t cookies[MWIN_X11_ATOMS];
    for (int i = 0; i < MWIN_X11_ATOMS; i++)
    {
        cookies[i] = api->internAtom(platform->connection, 0, (uint16_t)strlen(s_atomNames[i]),
                                     s_atomNames[i]);
    }
    bool interned = true;
    for (int i = 0; i < MWIN_X11_ATOMS; i++)
    {
        xcb_intern_atom_reply_t* reply =
            api->internAtomReply(platform->connection, cookies[i], nullptr);
        interned = interned && reply != nullptr;
        platform->atoms[i] = reply != nullptr ? reply->atom : XCB_ATOM_NONE;
        mwinReleaseSystemMemory(reply);
    }
    return interned;
}

// A property of the root window, or NULL; the caller releases it.
static xcb_get_property_reply_t* RootProperty(const mwinX11Platform* platform, xcb_atom_t property,
                                              xcb_atom_t type, uint32_t longs)
{
    const mwinX11Api* api = &platform->api;
    return api->getPropertyReply(
        platform->connection,
        api->getProperty(platform->connection, 0, platform->screen->root, property, type, 0, longs),
        nullptr);
}

// The number after "Xft.dpi:" in a resource string, or 0.
static float ParseDpi(const char* text, size_t length)
{
    static const char key[] = "Xft.dpi:";
    size_t keyLength = sizeof(key) - 1;
    for (size_t at = 0; at + keyLength <= length; at++)
    {
        if ((at > 0 && text[at - 1] != '\n') || memcmp(text + at, key, keyLength) != 0)
        {
            continue;
        }
        size_t i = at + keyLength;
        while (i < length && (text[i] == ' ' || text[i] == '\t'))
        {
            i++;
        }
        float value = 0.0f;
        // The weight of the next fraction digit, 0 before the point.
        float fraction = 0.0f;
        for (; i < length && ((text[i] >= '0' && text[i] <= '9') || text[i] == '.'); i++)
        {
            if (text[i] == '.')
            {
                if (fraction > 0.0f)
                {
                    break;
                }
                fraction = 0.1f;
                continue;
            }
            float digit = (float)(text[i] - '0');
            value = fraction > 0.0f ? value + digit * fraction : value * 10.0f + digit;
            fraction /= 10.0f;
        }
        return value;
    }
    return 0.0f;
}

// The scale desktops set through Xft.dpi, 1 where it is not set.
static float ReadScale(const mwinX11Platform* platform)
{
    xcb_get_property_reply_t* reply =
        RootProperty(platform, XCB_ATOM_RESOURCE_MANAGER, XCB_ATOM_STRING, 16384);
    float dpi = 0.0f;
    if (reply != nullptr)
    {
        dpi = ParseDpi(platform->api.getPropertyValue(reply),
                       (size_t)platform->api.getPropertyValueLength(reply));
        mwinReleaseSystemMemory(reply);
    }
    return dpi >= BASE_DPI / 4.0f && dpi <= BASE_DPI * 8.0f ? dpi / BASE_DPI : 1.0f;
}

// Whether an EWMH window manager runs: the root names its check window.
static bool HasWindowManager(const mwinX11Platform* platform)
{
    xcb_get_property_reply_t* reply =
        RootProperty(platform, platform->atoms[mwin_atomNetSupportingWmCheck], XCB_ATOM_WINDOW, 1);
    bool found = reply != nullptr && platform->api.getPropertyValueLength(reply) >= 4;
    mwinReleaseSystemMemory(reply);
    return found;
}

// Asks for RandR 1.5 and its screen change events.
static void StartRandr(mwinX11Platform* platform)
{
    const mwinX11Api* api = &platform->api;
    if (api->randrLibrary == nullptr)
    {
        return;
    }
    const xcb_query_extension_reply_t* extension =
        api->getExtensionData(platform->connection, api->randrId);
    if (extension == nullptr || !extension->present)
    {
        return;
    }
    xcb_randr_query_version_reply_t* version = api->randrQueryVersionReply(
        platform->connection, api->randrQueryVersion(platform->connection, 1, 5), nullptr);
    bool recent = version != nullptr && (version->major_version > 1 || version->minor_version >= 5);
    mwinReleaseSystemMemory(version);
    if (!recent)
    {
        return;
    }
    platform->randrEvent = extension->first_event;
    api->randrSelectInput(platform->connection, platform->screen->root,
                          XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE | XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE |
                              XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE);
}

// Connects to the display and learns what the backend needs of it.
static mwinResult Connect(mwinX11Platform* platform)
{
    mwinResult status = mwinLoadX11(&platform->api);
    if (status != mwin_success)
    {
        return status;
    }
    const mwinX11Api* api = &platform->api;
    int screenNumber = 0;
    platform->connection = api->connect(nullptr, &screenNumber);
    if (api->connectionHasError(platform->connection) != 0)
    {
        return mwin_errorPlatform;
    }
    xcb_screen_iterator_t screens = api->setupRootsIterator(api->getSetup(platform->connection));
    for (int i = 0; i < screenNumber && screens.rem > 0; i++)
    {
        api->screenNext(&screens);
    }
    platform->screen = screens.data;
    if (platform->screen == nullptr || !InternAtoms(platform))
    {
        return mwin_errorPlatform;
    }
    platform->windowManager = HasWindowManager(platform);
    platform->scale = ReadScale(platform);
    StartRandr(platform);
    mwinX11RefreshMonitors(platform);
    mwinX11StartKeyboard(platform);
    mwinX11StartRawMotion(platform);
    mwinX11StartCursors(platform);
    return mwin_success;
}

static void Stop(mwinContext* context)
{
    mwinX11Platform* platform = PlatformOf(context);
    const mwinX11Api* api = &platform->api;
    if (platform->connection != nullptr)
    {
        for (uint32_t i = 0; i < context->limits.windows; i++)
        {
            if (platform->windows[i].window != 0)
            {
                api->destroyWindow(platform->connection, platform->windows[i].window);
            }
        }
        mwinX11StopCursors(platform);
        mwinX11StopClipboard(platform);
        (void)api->flush(platform->connection);
        // A failed connection is freed the same way.
        api->disconnect(platform->connection);
    }
    mwinX11StopKeyboard(platform);
    mwinLinuxServicesStop(&platform->services);
#ifdef MAUL_WINDOW_GAMEPAD
    mwinLinuxPadsStop(&platform->pads);
#endif
    mwinUnloadX11(&platform->api);
    mwinRelease(&context->allocator, platform, PlatformBytes(context), alignof(max_align_t));
    context->backendData = nullptr;
}

static mwinResult Start(mwinContext* context)
{
    unsigned char* block =
        mwinAllocate(&context->allocator, PlatformBytes(context), alignof(max_align_t));
    if (block == nullptr)
    {
        return mwin_errorCapacity;
    }
    memset(block, 0, PlatformBytes(context));
    mwinX11Platform* platform = (mwinX11Platform*)block;
    platform->windows = (mwinX11Window*)(block + sizeof(mwinX11Platform));
    platform->outputs = (mwinX11Output*)(block + sizeof(mwinX11Platform) +
                                         context->limits.windows * sizeof(mwinX11Window));
    for (uint32_t i = 0; i < context->limits.monitors; i++)
    {
        platform->outputs[i].monitor = -1;
    }
    platform->context = context;
    platform->scale = 1.0f;
    platform->pointer.focus = -1;
    platform->drag.slot = -1;
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

static void Dispatch(mwinX11Platform* platform, const xcb_generic_event_t* event)
{
    if (mwinX11HandleClipboardEvent(platform, event) || mwinX11HandleDropEvent(platform, event) ||
        mwinX11HandleWindowEvent(platform, event) || mwinX11HandleInputEvent(platform, event))
    {
        return;
    }
    uint8_t type = event->response_type & 0x7F;
    if (platform->randrEvent != 0 &&
        (type == platform->randrEvent + XCB_RANDR_SCREEN_CHANGE_NOTIFY ||
         type == platform->randrEvent + XCB_RANDR_NOTIFY))
    {
        mwinX11RefreshMonitors(platform);
    }
}

// Sends what waits, then handles every event that has arrived.
static void Pump(mwinContext* context)
{
    mwinX11Platform* platform = PlatformOf(context);
    const mwinX11Api* api = &platform->api;
    (void)api->flush(platform->connection);
    for (xcb_generic_event_t* event = api->pollForEvent(platform->connection); event != nullptr;
         event = api->pollForEvent(platform->connection))
    {
        Dispatch(platform, event);
        mwinReleaseSystemMemory(event);
    }
    mwinX11CheckDeadlines(platform);
    mwinX11CheckClipboard(platform, mwinMonotonicNow());
    mwinX11CheckDrop(platform, mwinMonotonicNow());
    mwinLinuxServicesPump(&platform->services, mwinMonotonicNow(), mwinWantsAwake(context));
#ifdef MAUL_WINDOW_GAMEPAD
    mwinLinuxPadsPump(&platform->pads);
#endif
    if (api->connectionHasError(platform->connection) != 0)
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
    return mwinX11MapKeyCode(PlatformOf(context), code);
}

static mwinResult KeyboardLayout(const mwinContext* context, char* buffer, size_t capacity,
                                 size_t* lengthOut)
{
    return mwinX11KeyboardLayout(PlatformOf(context), buffer, capacity, lengthOut);
}

static void NativeHandles(const mwinContext* context, uint32_t slot, mwinNativeHandles* out)
{
    const mwinX11Platform* platform = PlatformOf(context);
    out->platform = mwin_platformX11;
    out->handles.x11.connection = platform->connection;
    out->handles.x11.window = platform->windows[slot].window;
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

const mwinBackendOps mwinX11Backend = {
    Start,         Stop, Run,        mwinX11CreateWindow, mwinX11DestroyWindow,
    mwinX11Submit, Now,  MapKeyCode, KeyboardLayout,      NativeHandles,
    Rumble,
};
