// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Pens on macOS (macos.h). A tablet's pen drives mouse events with the
// tablet point subtype; those become pen records and no mouse record,
// as on the other platforms: the tip's press and release are the pen's
// down and up, a drag moves it touching, a move hovering. Its pressure
// is the event's, from 0 to 1; its tilt, from -1 to 1 along each axis,
// is scaled to 90 degrees, AppKit's upward y turned down. The barrel is
// the lower side button of the event's button mask, whose press or
// release is a pen button record; the upper side button, which drivers
// give to their own commands, is left out. Proximity events tell which
// end is near the tablet: the eraser's sets mwin_penEraser until the
// pen leaves.

#include "macos.h"

// A tablet event's buttons (AppKit's pen button masks).
enum
{
    penTip = 1,
    penLowerSide = 2,
};

// The mouse event types that carry a subtype; asking another type for
// one raises.
static bool HasSubtype(NSEventType type)
{
    switch (type)
    {
    case NSEventTypeLeftMouseDown:
    case NSEventTypeLeftMouseUp:
    case NSEventTypeRightMouseDown:
    case NSEventTypeRightMouseUp:
    case NSEventTypeOtherMouseDown:
    case NSEventTypeOtherMouseUp:
    case NSEventTypeMouseMoved:
    case NSEventTypeLeftMouseDragged:
    case NSEventTypeRightMouseDragged:
    case NSEventTypeOtherMouseDragged:
        return true;
    default:
        return false;
    }
}

static bool IsPen(NSEvent* event)
{
    return event.type == NSEventTypeTabletPoint ||
           (HasSubtype(event.type) && event.subtype == NSEventSubtypeTabletPoint);
}

// Whether the tip touches after the event: a press puts it down, a
// release lifts it, and the rest keep it as it was.
static bool ContactAfter(NSEvent* event, bool before)
{
    switch (event.type)
    {
    case NSEventTypeLeftMouseDown:
        return true;
    case NSEventTypeLeftMouseUp:
        return false;
    case NSEventTypeTabletPoint:
        return (event.buttonMask & penTip) != 0;
    default:
        return before;
    }
}

static void Post(mwinMacPlatform* platform, uint32_t slot, mwinEventType type,
                 const mwinPenEvent* pen)
{
    mwinEvent event = {.type = type, .timeNs = mwinMacNow()};
    event.data.pen = *pen;
    mwinPost(platform->context, slot, &event);
}

bool mwinMacTakePen(mwinMacPlatform* platform, uint32_t slot, NSEvent* event)
{
    if (!IsPen(event))
    {
        return false;
    }
    mwinMacWindow* window = &platform->windows[slot];
    bool contact = ContactAfter(event, (window->penFlags & mwin_penContact) != 0);
    mwinPenFlags flags = platform->penEraser ? mwin_penEraser : 0;
    flags |= contact ? mwin_penContact : 0;
    flags |= (event.buttonMask & penLowerSide) != 0 ? mwin_penBarrel : 0;
    NSPoint point = [window->view convertPoint:event.locationInWindow fromView:nil];
    NSPoint tilt = event.tilt;
    mwinPenEvent pen = {{(float)point.x, (float)point.y},
                        contact ? event.pressure : 0.0f,
                        (float)(tilt.x * 90.0),
                        (float)(-tilt.y * 90.0),
                        flags,
                        0};
    if (((flags ^ window->penFlags) & mwin_penBarrel) != 0)
    {
        pen.button = 1;
        Post(platform, slot,
             (flags & mwin_penBarrel) != 0 ? mwin_eventPenButtonDown : mwin_eventPenButtonUp, &pen);
        pen.button = 0;
    }
    mwinPenFlags touched = (flags ^ window->penFlags) & mwin_penContact;
    window->penFlags = flags;
    Post(platform, slot,
         touched == 0 ? mwin_eventPenMoved : (contact ? mwin_eventPenDown : mwin_eventPenUp), &pen);
    return true;
}

void mwinMacPenProximity(mwinMacPlatform* platform, NSEvent* event)
{
    platform->penEraser =
        event.enteringProximity && event.pointingDeviceType == NSPointingDeviceTypeEraser;
}
