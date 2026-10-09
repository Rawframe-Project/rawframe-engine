// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Tablets on Wayland (wayland_tablet.h).

#include "wayland_tablet.h"

#include "wayland_cursor.h"

#include <linux/input-event-codes.h>

// Pressure comes from 0 to 65535.
#define PRESSURE_RANGE 65535.0f

static mwinWaylandTool* ToolOf(void* data)
{
    return data;
}

static void PostPen(const mwinWaylandTool* tool, mwinEventType type, uint8_t button,
                    uint64_t timeNs)
{
    mwinEvent event = {0};
    event.type = type;
    event.timeNs = timeNs;
    event.data.pen = tool->pen;
    event.data.pen.button = button;
    mwinPost(tool->platform->context, (uint32_t)tool->focus, &event);
}

static void OnType(void* data, struct zwp_tablet_tool_v2* object, uint32_t type)
{
    (void)object;
    mwinWaylandTool* tool = ToolOf(data);
    tool->eraser = type == ZWP_TABLET_TOOL_V2_TYPE_ERASER;
    // Pucks and fingers post pens without tilt: nothing else reaches the
    // program from them. The type may come before the capabilities.
    tool->puck = type == ZWP_TABLET_TOOL_V2_TYPE_MOUSE || type == ZWP_TABLET_TOOL_V2_TYPE_LENS ||
                 type == ZWP_TABLET_TOOL_V2_TYPE_FINGER;
    tool->pen.flags = tool->eraser ? mwin_penEraser : 0;
}

static void OnHardwareSerial(void* data, struct zwp_tablet_tool_v2* object, uint32_t high,
                             uint32_t low)
{
    (void)data;
    (void)object;
    (void)high;
    (void)low;
}

static void OnCapability(void* data, struct zwp_tablet_tool_v2* object, uint32_t capability)
{
    (void)object;
    mwinWaylandTool* tool = ToolOf(data);
    tool->tilts = tool->tilts || capability == ZWP_TABLET_TOOL_V2_CAPABILITY_TILT;
    tool->presses = tool->presses || capability == ZWP_TABLET_TOOL_V2_CAPABILITY_PRESSURE;
}

static void OnDone(void* data, struct zwp_tablet_tool_v2* object)
{
    (void)data;
    (void)object;
}

// Lets go of a tool, which posts its lift first if it is down.
static void Drop(mwinWaylandTool* tool)
{
    const mwinWaylandApi* api = &tool->platform->api;
    if (tool->focus >= 0 && (tool->pen.flags & mwin_penContact) != 0)
    {
        tool->pen.flags &= (mwinPenFlags)~mwin_penContact;
        tool->pen.pressure = 0.0f;
        PostPen(tool, mwin_eventPenUp, 0, mwinMonotonicNow());
    }
    if (tool->shapeDevice != nullptr)
    {
        (void)mwinWlRequest(api, tool->shapeDevice, WP_CURSOR_SHAPE_DEVICE_V1_DESTROY, nullptr,
                            WL_MARSHAL_FLAG_DESTROY);
    }
    if (tool->themeSurface != nullptr)
    {
        (void)mwinWlRequest(api, tool->themeSurface, WL_SURFACE_DESTROY, nullptr,
                            WL_MARSHAL_FLAG_DESTROY);
    }
    (void)mwinWlRequest(api, tool->tool, ZWP_TABLET_TOOL_V2_DESTROY, nullptr,
                        WL_MARSHAL_FLAG_DESTROY);
    *tool = (mwinWaylandTool){.focus = -1};
}

static void OnRemoved(void* data, struct zwp_tablet_tool_v2* object)
{
    (void)object;
    Drop(ToolOf(data));
}

static void OnProximityIn(void* data, struct zwp_tablet_tool_v2* object, uint32_t serial,
                          struct zwp_tablet_v2* tablet, struct wl_surface* surface)
{
    (void)object;
    (void)tablet;
    mwinWaylandTool* tool = ToolOf(data);
    // Near a frame's part, or another client's surface, it is near no
    // window.
    tool->focus = surface != nullptr ? mwinWaylandSlotOf(tool->platform, surface) : -1;
    tool->serial = serial;
    tool->near = true;
}

static void OnProximityOut(void* data, struct zwp_tablet_tool_v2* object)
{
    (void)object;
    ToolOf(data)->left = true;
}

static void OnDown(void* data, struct zwp_tablet_tool_v2* object, uint32_t serial)
{
    (void)object;
    mwinWaylandTool* tool = ToolOf(data);
    tool->down = true;
    tool->platform->inputSerial = serial;
}

static void OnUp(void* data, struct zwp_tablet_tool_v2* object)
{
    (void)object;
    ToolOf(data)->up = true;
}

static void OnMotion(void* data, struct zwp_tablet_tool_v2* object, wl_fixed_t x, wl_fixed_t y)
{
    (void)object;
    mwinWaylandTool* tool = ToolOf(data);
    tool->pen.position = (mwinPosition){(float)wl_fixed_to_double(x), (float)wl_fixed_to_double(y)};
    tool->moved = true;
}

static void OnPressure(void* data, struct zwp_tablet_tool_v2* object, uint32_t pressure)
{
    (void)object;
    mwinWaylandTool* tool = ToolOf(data);
    tool->pressure = (float)pressure / PRESSURE_RANGE;
    tool->moved = true;
}

static void OnDistance(void* data, struct zwp_tablet_tool_v2* object, uint32_t distance)
{
    (void)data;
    (void)object;
    (void)distance;
}

static void OnTilt(void* data, struct zwp_tablet_tool_v2* object, wl_fixed_t x, wl_fixed_t y)
{
    (void)object;
    mwinWaylandTool* tool = ToolOf(data);
    if (tool->tilts && !tool->puck)
    {
        tool->pen.tiltX = (float)wl_fixed_to_double(x);
        tool->pen.tiltY = (float)wl_fixed_to_double(y);
        tool->moved = true;
    }
}

static void OnRotation(void* data, struct zwp_tablet_tool_v2* object, wl_fixed_t degrees)
{
    (void)data;
    (void)object;
    (void)degrees;
}

static void OnSlider(void* data, struct zwp_tablet_tool_v2* object, int32_t position)
{
    (void)data;
    (void)object;
    (void)position;
}

static void OnWheel(void* data, struct zwp_tablet_tool_v2* object, wl_fixed_t degrees,
                    int32_t clicks)
{
    (void)data;
    (void)object;
    (void)degrees;
    (void)clicks;
}

// The lower barrel button is the pen's barrel; drivers bind the upper
// one to commands of their own.
static void OnButton(void* data, struct zwp_tablet_tool_v2* object, uint32_t serial,
                     uint32_t button, uint32_t state)
{
    (void)object;
    mwinWaylandTool* tool = ToolOf(data);
    if (button == BTN_STYLUS)
    {
        tool->barrel = state == ZWP_TABLET_TOOL_V2_BUTTON_STATE_PRESSED ? 1 : -1;
        tool->platform->inputSerial = serial;
    }
}

// The pen's pressure as posted: the measure while in contact, or full
// for a tool without one; 0 hovering.
static float PressureOf(const mwinWaylandTool* tool)
{
    if ((tool->pen.flags & mwin_penContact) == 0)
    {
        return 0.0f;
    }
    return tool->presses ? tool->pressure : 1.0f;
}

// The changes of a frame hold together: the barrel button's change,
// then contact or motion, then leaving.
static void OnFrame(void* data, struct zwp_tablet_tool_v2* object, uint32_t time)
{
    (void)object;
    mwinWaylandTool* tool = ToolOf(data);
    uint64_t timeNs = mwinMonotonicFromMilliseconds(time);
    if (tool->near)
    {
        mwinWaylandShowToolCursor(tool->platform, tool);
    }
    if (tool->focus >= 0)
    {
        if (tool->barrel != 0)
        {
            tool->pen.flags = tool->barrel > 0 ? tool->pen.flags | mwin_penBarrel
                                               : tool->pen.flags & (mwinPenFlags)~mwin_penBarrel;
            tool->pen.pressure = PressureOf(tool);
            PostPen(tool, tool->barrel > 0 ? mwin_eventPenButtonDown : mwin_eventPenButtonUp, 1,
                    timeNs);
        }
        if (tool->down)
        {
            tool->pen.flags |= mwin_penContact;
        }
        else if (tool->up)
        {
            tool->pen.flags &= (mwinPenFlags)~mwin_penContact;
        }
        tool->pen.pressure = PressureOf(tool);
        if (tool->down || tool->up)
        {
            PostPen(tool, tool->down ? mwin_eventPenDown : mwin_eventPenUp, 0, timeNs);
        }
        else if (tool->moved || tool->near)
        {
            PostPen(tool, mwin_eventPenMoved, 0, timeNs);
        }
        if (tool->left && (tool->pen.flags & mwin_penContact) != 0)
        {
            tool->pen.flags &= (mwinPenFlags)~mwin_penContact;
            tool->pen.pressure = 0.0f;
            PostPen(tool, mwin_eventPenUp, 0, timeNs);
        }
    }
    if (tool->left)
    {
        tool->focus = -1;
        tool->pen.flags &= (mwinPenFlags) ~(mwin_penContact | mwin_penBarrel);
    }
    tool->near = false;
    tool->left = false;
    tool->moved = false;
    tool->down = false;
    tool->up = false;
    tool->barrel = 0;
}

static const struct zwp_tablet_tool_v2_listener s_toolListener = {
    OnType,    OnHardwareSerial, OnHardwareSerial, OnCapability, OnDone,
    OnRemoved, OnProximityIn,    OnProximityOut,   OnDown,       OnUp,
    OnMotion,  OnPressure,       OnDistance,       OnTilt,       OnRotation,
    OnSlider,  OnWheel,          OnButton,         OnFrame,
};

static void OnTabletName(void* data, struct zwp_tablet_v2* tablet, const char* name)
{
    (void)data;
    (void)tablet;
    (void)name;
}

static void OnTabletId(void* data, struct zwp_tablet_v2* tablet, uint32_t vendor, uint32_t product)
{
    (void)data;
    (void)tablet;
    (void)vendor;
    (void)product;
}

static void OnTabletDone(void* data, struct zwp_tablet_v2* tablet)
{
    (void)data;
    (void)tablet;
}

// Lets go of a tablet the platform keeps.
static void DropTablet(mwinWaylandPlatform* platform, struct zwp_tablet_v2* tablet)
{
    for (int i = 0; i < MWIN_WAYLAND_TABLETS; i++)
    {
        if (platform->tablets.tablets[i] == tablet)
        {
            platform->tablets.tablets[i] = nullptr;
        }
    }
    (void)mwinWlRequest(&platform->api, tablet, ZWP_TABLET_V2_DESTROY, nullptr,
                        WL_MARSHAL_FLAG_DESTROY);
}

static void OnTabletRemoved(void* data, struct zwp_tablet_v2* tablet)
{
    DropTablet(data, tablet);
}

static const struct zwp_tablet_v2_listener s_tabletListener = {
    OnTabletName, OnTabletId, OnTabletName, OnTabletDone, OnTabletRemoved,
};

// A tablet is kept, its tools told near surfaces only while it is; one
// past the most kept is let go of.
static void OnTabletAdded(void* data, struct zwp_tablet_seat_v2* seat, struct zwp_tablet_v2* tablet)
{
    (void)seat;
    mwinWaylandPlatform* platform = data;
    for (int i = 0; i < MWIN_WAYLAND_TABLETS; i++)
    {
        if (platform->tablets.tablets[i] == nullptr)
        {
            platform->tablets.tablets[i] = tablet;
            mwinWlListen(&platform->api, tablet, &s_tabletListener, platform);
            return;
        }
    }
    DropTablet(platform, tablet);
}

static void OnToolAdded(void* data, struct zwp_tablet_seat_v2* seat,
                        struct zwp_tablet_tool_v2* object)
{
    (void)seat;
    mwinWaylandPlatform* platform = data;
    for (int i = 0; i < MWIN_WAYLAND_TOOLS; i++)
    {
        mwinWaylandTool* tool = &platform->tablets.tools[i];
        if (tool->tool == nullptr)
        {
            *tool = (mwinWaylandTool){.tool = object, .platform = platform, .focus = -1};
            mwinWlListen(&platform->api, object, &s_toolListener, tool);
            return;
        }
    }
    // One past the most followed is let go of.
    (void)mwinWlRequest(&platform->api, object, ZWP_TABLET_TOOL_V2_DESTROY, nullptr,
                        WL_MARSHAL_FLAG_DESTROY);
}

// A pad's buttons, rings and strips are no pen's: let go of at once.
static void OnPadAdded(void* data, struct zwp_tablet_seat_v2* seat, struct zwp_tablet_pad_v2* pad)
{
    (void)seat;
    const mwinWaylandPlatform* platform = data;
    (void)mwinWlRequest(&platform->api, pad, ZWP_TABLET_PAD_V2_DESTROY, nullptr,
                        WL_MARSHAL_FLAG_DESTROY);
}

static const struct zwp_tablet_seat_v2_listener s_seatListener = {
    OnTabletAdded,
    OnToolAdded,
    OnPadAdded,
};

void mwinWaylandAttachTablets(mwinWaylandPlatform* platform)
{
    mwinWaylandTablets* tablets = &platform->tablets;
    if (tablets->manager == nullptr || platform->seat == nullptr || tablets->seat != nullptr)
    {
        return;
    }
    const mwinWaylandApi* api = &platform->api;
    tablets->seat = mwinWlCreateFor(api, tablets->manager, ZWP_TABLET_MANAGER_V2_GET_TABLET_SEAT,
                                    &zwp_tablet_seat_v2_interface, platform->seat);
    mwinWlListen(api, tablets->seat, &s_seatListener, platform);
}

void mwinWaylandReleaseTablets(mwinWaylandPlatform* platform)
{
    const mwinWaylandApi* api = &platform->api;
    mwinWaylandTablets* tablets = &platform->tablets;
    for (int i = 0; i < MWIN_WAYLAND_TOOLS; i++)
    {
        if (tablets->tools[i].tool != nullptr)
        {
            Drop(&tablets->tools[i]);
        }
    }
    for (int i = 0; i < MWIN_WAYLAND_TABLETS; i++)
    {
        if (tablets->tablets[i] != nullptr)
        {
            DropTablet(platform, tablets->tablets[i]);
        }
    }
    if (tablets->seat != nullptr)
    {
        (void)mwinWlRequest(api, tablets->seat, ZWP_TABLET_SEAT_V2_DESTROY, nullptr,
                            WL_MARSHAL_FLAG_DESTROY);
        tablets->seat = nullptr;
    }
}

void mwinWaylandForgetToolFocus(mwinWaylandPlatform* platform, uint32_t slot)
{
    for (int i = 0; i < MWIN_WAYLAND_TOOLS; i++)
    {
        mwinWaylandTool* tool = &platform->tablets.tools[i];
        if (tool->tool != nullptr && tool->focus == (int32_t)slot)
        {
            tool->focus = -1;
            tool->pen.flags &= (mwinPenFlags) ~(mwin_penContact | mwin_penBarrel);
        }
    }
}
