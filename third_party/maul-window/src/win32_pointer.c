// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32 touch and pen.

#include "win32_pointer.h"

// Windows' pressure runs from 0 to 1024.
#define PRESSURE_RANGE 1024.0f

static void Post(mwinWin32Window* window, const mwinEvent* event)
{
    mwinPost(window->platform->context, window->slot, event);
}

// A place on the screen in the window's logical units.
static mwinPosition PositionOf(const mwinWin32Window* window, POINT point)
{
    ScreenToClient(window->hwnd, &point);
    float scale = mwinWin32Scale(window->dpi);
    return (mwinPosition){(float)point.x / scale, (float)point.y / scale};
}

// The slot of a touch followed on the window, or -1. Id 0 finds a
// vacant slot.
static int TouchSlot(const mwinWin32Window* window, UINT32 id)
{
    for (int i = 0; i < MWIN_WIN32_TOUCHES; i++)
    {
        if (window->touches[i] == id)
        {
            return i;
        }
    }
    return -1;
}

static void PostTouch(mwinWin32Window* window, mwinEventType type, UINT32 id, mwinPosition position,
                      float pressure)
{
    mwinEvent event = {0};
    event.type = type;
    event.timeNs = mwinWin32Now();
    event.data.touch = (mwinTouchEvent){id, position, pressure};
    Post(window, &event);
}

static void OnTouch(mwinWin32Window* window, UINT message, UINT32 id)
{
    POINTER_TOUCH_INFO info;
    if (!GetPointerTouchInfo(id, &info))
    {
        return;
    }
    // Windows' ids start at 1; a touch past the ones followed is left
    // out, all its life.
    int slot = TouchSlot(window, message == WM_POINTERDOWN ? 0 : id);
    if (id == 0 || slot < 0)
    {
        return;
    }
    POINTER_FLAGS flags = info.pointerInfo.pointerFlags;
    mwinEventType type = mwin_eventTouchMoved;
    if (message == WM_POINTERDOWN)
    {
        type = mwin_eventTouchDown;
        window->touches[slot] = id;
    }
    else if (message == WM_POINTERUP)
    {
        type = (flags & POINTER_FLAG_CANCELED) != 0 ? mwin_eventTouchCancelled : mwin_eventTouchUp;
        window->touches[slot] = 0;
    }
    float pressure =
        (info.touchMask & TOUCH_MASK_PRESSURE) != 0 ? (float)info.pressure / PRESSURE_RANGE : -1.0f;
    PostTouch(window, type, id, PositionOf(window, info.pointerInfo.ptPixelLocation), pressure);
}

static mwinPenFlags PenFlagsOf(const POINTER_PEN_INFO* info)
{
    mwinPenFlags flags = 0;
    flags |= (info->penFlags & (PEN_FLAG_ERASER | PEN_FLAG_INVERTED)) != 0 ? mwin_penEraser : 0;
    flags |= (info->pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT) != 0 ? mwin_penContact : 0;
    flags |= (info->penFlags & PEN_FLAG_BARREL) != 0 ? mwin_penBarrel : 0;
    return flags;
}

static void PostPen(mwinWin32Window* window, mwinEventType type, const mwinPenEvent* pen)
{
    mwinEvent event = {0};
    event.type = type;
    event.timeNs = mwinWin32Now();
    event.data.pen = *pen;
    Post(window, &event);
}

// A pen: its barrel button's changes, then where it is.
static void OnPen(mwinWin32Window* window, UINT message, UINT32 id)
{
    POINTER_PEN_INFO info;
    if (!GetPointerPenInfo(id, &info))
    {
        return;
    }
    mwinPenFlags flags = PenFlagsOf(&info);
    bool contact = (flags & mwin_penContact) != 0;
    mwinPenEvent pen = {0};
    pen.position = PositionOf(window, info.pointerInfo.ptPixelLocation);
    // Without a measure, the pen presses fully or not at all.
    pen.pressure = (info.penMask & PEN_MASK_PRESSURE) != 0 ? (float)info.pressure / PRESSURE_RANGE
                                                           : (contact ? 1.0f : 0.0f);
    pen.tiltX = (info.penMask & PEN_MASK_TILT_X) != 0 ? (float)info.tiltX : 0.0f;
    pen.tiltY = (info.penMask & PEN_MASK_TILT_Y) != 0 ? (float)info.tiltY : 0.0f;
    pen.flags = flags;
    if (((flags ^ window->penFlags) & mwin_penBarrel) != 0)
    {
        pen.button = 1;
        PostPen(window,
                (flags & mwin_penBarrel) != 0 ? mwin_eventPenButtonDown : mwin_eventPenButtonUp,
                &pen);
        pen.button = 0;
    }
    window->penFlags = flags;
    mwinEventType type = message == WM_POINTERDOWN ? mwin_eventPenDown
                         : message == WM_POINTERUP ? mwin_eventPenUp
                                                   : mwin_eventPenMoved;
    PostPen(window, type, &pen);
}

bool mwinWin32HandlePointer(mwinWin32Window* window, UINT message, WPARAM wParam)
{
    if (message != WM_POINTERDOWN && message != WM_POINTERUPDATE && message != WM_POINTERUP &&
        message != WM_POINTERCAPTURECHANGED)
    {
        return false;
    }
    UINT32 id = GET_POINTERID_WPARAM(wParam);
    POINTER_INPUT_TYPE type = PT_POINTER;
    if (!GetPointerType(id, &type) || (type != PT_TOUCH && type != PT_PEN))
    {
        return false;
    }
    if (message == WM_POINTERCAPTURECHANGED)
    {
        // Windows took a touch still down: it will not come up here.
        int slot = type == PT_TOUCH && id != 0 ? TouchSlot(window, id) : -1;
        if (slot >= 0)
        {
            window->touches[slot] = 0;
            PostTouch(window, mwin_eventTouchCancelled, id, (mwinPosition){0.0f, 0.0f}, -1.0f);
        }
        return true;
    }
    if (type == PT_TOUCH)
    {
        OnTouch(window, message, id);
    }
    else
    {
        OnPen(window, message, id);
    }
    return true;
}
