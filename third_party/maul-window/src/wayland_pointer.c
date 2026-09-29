// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Wayland pointer and touch screen.

#include "wayland_pointer.h"

#include "wayland_cursor.h"
#include "wayland_frame.h"

#include <linux/input-event-codes.h>
#include <math.h>

// Continuous wheel distance per detent.
#define UNITS_PER_DETENT 10.0

// What a pointer frame gathered for an axis.
enum
{
    axisContinuous = 1,
    axisDiscrete = 2,
    axisHigh = 4,
};

static mwinMouseButton ButtonOf(uint32_t evdev)
{
    switch (evdev)
    {
    case BTN_LEFT:
        return mwin_buttonLeft;
    case BTN_RIGHT:
        return mwin_buttonRight;
    case BTN_MIDDLE:
        return mwin_buttonMiddle;
    case BTN_SIDE:
    case BTN_BACK:
        return mwin_buttonBack;
    case BTN_EXTRA:
    case BTN_FORWARD:
        return mwin_buttonForward;
    default:
        return 0;
    }
}

static void PostPointer(mwinWaylandPlatform* platform, mwinEventType type, mwinMouseButton button,
                        uint64_t timeNs)
{
    const mwinWaylandPointer* pointer = &platform->pointer;
    mwinEvent event = {0};
    event.type = type;
    event.timeNs = timeNs;
    event.data.pointer = (mwinPointerEvent){pointer->position, platform->keyboard.xkb.modifiers,
                                            pointer->buttons, button, pointer->clicks.clicks};
    mwinPost(platform->context, (uint32_t)pointer->focus, &event);
}

// The detents an axis turned in the frame.
static float Detents(const mwinWaylandPointer* pointer, int axis)
{
    uint8_t kinds = pointer->axisKinds[axis];
    if ((kinds & axisHigh) != 0)
    {
        return (float)pointer->steps120[axis] / 120.0f;
    }
    if ((kinds & axisDiscrete) != 0)
    {
        return (float)pointer->steps[axis];
    }
    return (float)(pointer->distance[axis] / UNITS_PER_DETENT);
}

// Posts what the frame gathered.
static void Flush(mwinWaylandPlatform* platform)
{
    mwinWaylandPointer* pointer = &platform->pointer;
    if (pointer->focus >= 0 && pointer->moved)
    {
        PostPointer(platform, mwin_eventCursorMoved, 0, pointer->timeNs);
    }
    pointer->moved = false;
    if (pointer->axisKinds[0] == 0 && pointer->axisKinds[1] == 0)
    {
        return;
    }
    // Wayland counts down and right as positive.
    mwinWheelEvent wheel = {Detents(pointer, 1), -Detents(pointer, 0)};
    if (pointer->focus >= 0 && (wheel.x != 0.0f || wheel.y != 0.0f))
    {
        mwinEvent event = {0};
        event.type = mwin_eventWheel;
        event.timeNs = pointer->timeNs;
        event.data.wheel = wheel;
        mwinPost(platform->context, (uint32_t)pointer->focus, &event);
    }
    for (int axis = 0; axis < 2; axis++)
    {
        pointer->steps120[axis] = 0;
        pointer->steps[axis] = 0;
        pointer->distance[axis] = 0.0;
        pointer->axisKinds[axis] = 0;
    }
}

// A pointer older than version 5 sends no frames: each event is one.
static void FlushWithoutFrames(mwinWaylandPlatform* platform)
{
    if (mwinWlVersion(&platform->api, platform->pointer.pointer) < WL_POINTER_FRAME_SINCE_VERSION)
    {
        Flush(platform);
    }
}

static mwinPosition PositionOf(wl_fixed_t x, wl_fixed_t y)
{
    return (mwinPosition){(float)wl_fixed_to_double(x), (float)wl_fixed_to_double(y)};
}

static void OnEnter(void* data, struct wl_pointer* object, uint32_t serial,
                    struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y)
{
    (void)object;
    mwinWaylandPlatform* platform = data;
    mwinWaylandPointer* pointer = &platform->pointer;
    pointer->focus = mwinWaylandSlotOf(platform, surface);
    pointer->enterSerial = serial;
    pointer->position = PositionOf(x, y);
    pointer->buttons = 0;
    pointer->moved = false;
    // A frame's part is the backend's, not the program's.
    if (pointer->focus < 0 && mwinWaylandFrameEnter(platform, surface, pointer->position))
    {
        return;
    }
    if (pointer->focus >= 0)
    {
        PostPointer(platform, mwin_eventCursorEntered, 0, mwinMonotonicNow());
        mwinWaylandShowCursor(platform);
    }
}

static void OnLeave(void* data, struct wl_pointer* object, uint32_t serial,
                    struct wl_surface* surface)
{
    (void)object;
    (void)serial;
    (void)surface;
    mwinWaylandPlatform* platform = data;
    mwinWaylandPointer* pointer = &platform->pointer;
    if (pointer->framePart != nullptr)
    {
        mwinWaylandFrameLeave(platform);
        return;
    }
    Flush(platform);
    if (pointer->focus >= 0)
    {
        PostPointer(platform, mwin_eventCursorLeft, 0, mwinMonotonicNow());
        if (pointer->buttons != 0)
        {
            // Their releases will not come.
            mwinEvent reset = {0};
            reset.type = mwin_eventInputStateReset;
            reset.timeNs = mwinMonotonicNow();
            mwinPost(platform->context, (uint32_t)pointer->focus, &reset);
        }
    }
    pointer->focus = -1;
    pointer->buttons = 0;
}

static void OnMotion(void* data, struct wl_pointer* object, uint32_t time, wl_fixed_t x,
                     wl_fixed_t y)
{
    (void)object;
    mwinWaylandPlatform* platform = data;
    if (platform->pointer.framePart != nullptr)
    {
        mwinWaylandFrameMotion(platform, PositionOf(x, y));
        return;
    }
    platform->pointer.position = PositionOf(x, y);
    platform->pointer.moved = true;
    platform->pointer.timeNs = mwinMonotonicFromMilliseconds(time);
    FlushWithoutFrames(platform);
}

static void OnButton(void* data, struct wl_pointer* object, uint32_t serial, uint32_t time,
                     uint32_t evdev, uint32_t state)
{
    (void)object;
    mwinWaylandPlatform* platform = data;
    platform->inputSerial = serial;
    mwinWaylandPointer* pointer = &platform->pointer;
    if (pointer->framePart != nullptr)
    {
        mwinWaylandFrameButton(platform, serial, evdev, state == WL_POINTER_BUTTON_STATE_PRESSED,
                               mwinMonotonicFromMilliseconds(time));
        return;
    }
    mwinMouseButton button = ButtonOf(evdev);
    if (pointer->focus < 0 || button == 0)
    {
        return;
    }
    // Motion of the same frame comes first.
    Flush(platform);
    uint64_t timeNs = mwinMonotonicFromMilliseconds(time);
    uint8_t bit = (uint8_t)(1u << (button - 1));
    if (state == WL_POINTER_BUTTON_STATE_PRESSED)
    {
        uint8_t clicks = mwinCountClick(&pointer->clicks, button, pointer->position, timeNs);
        if (mwinWaylandPressChrome(platform, (uint32_t)pointer->focus, serial, evdev,
                                   pointer->position, clicks))
        {
            return;
        }
        pointer->buttons |= bit;
        PostPointer(platform, mwin_eventButtonDown, button, timeNs);
    }
    else if ((pointer->buttons & bit) == 0)
    {
        // The release of a press the compositor took.
        return;
    }
    else
    {
        pointer->buttons &= (uint8_t)~bit;
        PostPointer(platform, mwin_eventButtonUp, button, timeNs);
    }
}

static void OnAxis(void* data, struct wl_pointer* object, uint32_t time, uint32_t axis,
                   wl_fixed_t value)
{
    (void)object;
    mwinWaylandPlatform* platform = data;
    mwinWaylandPointer* pointer = &platform->pointer;
    if (axis > 1)
    {
        return;
    }
    pointer->distance[axis] += wl_fixed_to_double(value);
    pointer->axisKinds[axis] |= axisContinuous;
    pointer->timeNs = mwinMonotonicFromMilliseconds(time);
    FlushWithoutFrames(platform);
}

static void OnFrame(void* data, struct wl_pointer* object)
{
    (void)object;
    Flush(data);
}

static void OnAxisSource(void* data, struct wl_pointer* object, uint32_t source)
{
    (void)data;
    (void)object;
    (void)source;
}

static void OnAxisStop(void* data, struct wl_pointer* object, uint32_t time, uint32_t axis)
{
    (void)data;
    (void)object;
    (void)time;
    (void)axis;
}

static void OnAxisDiscrete(void* data, struct wl_pointer* object, uint32_t axis, int32_t steps)
{
    (void)object;
    mwinWaylandPointer* pointer = &((mwinWaylandPlatform*)data)->pointer;
    if (axis <= 1)
    {
        pointer->steps[axis] += steps;
        pointer->axisKinds[axis] |= axisDiscrete;
    }
}

static void OnAxisValue120(void* data, struct wl_pointer* object, uint32_t axis, int32_t steps)
{
    (void)object;
    mwinWaylandPointer* pointer = &((mwinWaylandPlatform*)data)->pointer;
    if (axis <= 1)
    {
        pointer->steps120[axis] += steps;
        pointer->axisKinds[axis] |= axisHigh;
    }
}

static void OnAxisRelativeDirection(void* data, struct wl_pointer* object, uint32_t axis,
                                    uint32_t direction)
{
    (void)data;
    (void)object;
    (void)axis;
    (void)direction;
}

static const struct wl_pointer_listener s_pointerListener = {
    OnEnter,
    OnLeave,
    OnMotion,
    OnButton,
    OnAxis,
    OnFrame,
    OnAxisSource,
    OnAxisStop,
    OnAxisDiscrete,
    OnAxisValue120,
    OnAxisRelativeDirection,
};

// Releases a device proxy with its release request, or destroys one too
// old to have it.
static void Release(const mwinWaylandApi* api, void* proxy, uint32_t opcode, uint32_t since)
{
    if (mwinWlVersion(api, proxy) >= since)
    {
        (void)mwinWlRequest(api, proxy, opcode, nullptr, WL_MARSHAL_FLAG_DESTROY);
    }
    else
    {
        api->proxyDestroy((struct wl_proxy*)proxy);
    }
}

void mwinWaylandAddPointer(mwinWaylandPlatform* platform)
{
    mwinWaylandPointer* pointer = &platform->pointer;
    *pointer = (mwinWaylandPointer){.focus = -1};
    pointer->pointer = mwinWlRequest(&platform->api, platform->seat, WL_SEAT_GET_POINTER,
                                     &wl_pointer_interface, 0);
    mwinWlListen(&platform->api, pointer->pointer, &s_pointerListener, platform);
    mwinWaylandAttachCursors(platform);
}

void mwinWaylandRemovePointer(mwinWaylandPlatform* platform)
{
    if (platform->pointer.pointer != nullptr)
    {
        mwinWaylandDetachCursors(platform);
        Release(&platform->api, platform->pointer.pointer, WL_POINTER_RELEASE,
                WL_POINTER_RELEASE_SINCE_VERSION);
    }
    platform->pointer = (mwinWaylandPointer){.focus = -1};
}

static void PostTouch(mwinWaylandPlatform* platform, mwinEventType type, int index, uint64_t timeNs)
{
    const mwinWaylandTouch* touch = &platform->touch;
    mwinEvent event = {0};
    event.type = type;
    event.timeNs = timeNs;
    event.data.touch = (mwinTouchEvent){(uint64_t)(uint32_t)touch->points[index].id,
                                        touch->points[index].position, -1.0f};
    mwinPost(platform->context, (uint32_t)touch->points[index].slot, &event);
}

// The index of an active touch point by its id, or -1.
static int FindPoint(const mwinWaylandTouch* touch, int32_t id)
{
    for (int i = 0; i < MWIN_WAYLAND_TOUCHES; i++)
    {
        if (touch->points[i].active && touch->points[i].id == id)
        {
            return i;
        }
    }
    return -1;
}

static void OnTouchDown(void* data, struct wl_touch* object, uint32_t serial, uint32_t time,
                        struct wl_surface* surface, int32_t id, wl_fixed_t x, wl_fixed_t y)
{
    (void)object;
    mwinWaylandPlatform* platform = data;
    platform->inputSerial = serial;
    mwinWaylandTouch* touch = &platform->touch;
    int32_t slot = mwinWaylandSlotOf(platform, surface);
    // A touch past the ones followed is left out, all its life.
    int index = -1;
    for (int i = 0; i < MWIN_WAYLAND_TOUCHES && index < 0; i++)
    {
        index = touch->points[i].active ? -1 : i;
    }
    if (index < 0 || slot < 0 || FindPoint(touch, id) >= 0)
    {
        return;
    }
    touch->points[index].id = id;
    touch->points[index].slot = slot;
    touch->points[index].position = PositionOf(x, y);
    touch->points[index].active = true;
    PostTouch(platform, mwin_eventTouchDown, index, mwinMonotonicFromMilliseconds(time));
}

static void OnTouchUp(void* data, struct wl_touch* object, uint32_t serial, uint32_t time,
                      int32_t id)
{
    (void)object;
    (void)serial;
    mwinWaylandPlatform* platform = data;
    int index = FindPoint(&platform->touch, id);
    if (index >= 0)
    {
        PostTouch(platform, mwin_eventTouchUp, index, mwinMonotonicFromMilliseconds(time));
        platform->touch.points[index].active = false;
    }
}

static void OnTouchMotion(void* data, struct wl_touch* object, uint32_t time, int32_t id,
                          wl_fixed_t x, wl_fixed_t y)
{
    (void)object;
    mwinWaylandPlatform* platform = data;
    int index = FindPoint(&platform->touch, id);
    if (index >= 0)
    {
        platform->touch.points[index].position = PositionOf(x, y);
        PostTouch(platform, mwin_eventTouchMoved, index, mwinMonotonicFromMilliseconds(time));
    }
}

static void OnTouchFrame(void* data, struct wl_touch* object)
{
    (void)data;
    (void)object;
}

static void OnTouchCancel(void* data, struct wl_touch* object)
{
    (void)object;
    mwinWaylandPlatform* platform = data;
    for (int i = 0; i < MWIN_WAYLAND_TOUCHES; i++)
    {
        if (platform->touch.points[i].active)
        {
            PostTouch(platform, mwin_eventTouchCancelled, i, mwinMonotonicNow());
            platform->touch.points[i].active = false;
        }
    }
}

static void OnTouchShape(void* data, struct wl_touch* object, int32_t id, wl_fixed_t major,
                         wl_fixed_t minor)
{
    (void)data;
    (void)object;
    (void)id;
    (void)major;
    (void)minor;
}

static void OnTouchOrientation(void* data, struct wl_touch* object, int32_t id,
                               wl_fixed_t orientation)
{
    (void)data;
    (void)object;
    (void)id;
    (void)orientation;
}

static const struct wl_touch_listener s_touchListener = {
    OnTouchDown,   OnTouchUp,    OnTouchMotion,      OnTouchFrame,
    OnTouchCancel, OnTouchShape, OnTouchOrientation,
};

void mwinWaylandAddTouch(mwinWaylandPlatform* platform)
{
    platform->touch = (mwinWaylandTouch){0};
    platform->touch.touch =
        mwinWlRequest(&platform->api, platform->seat, WL_SEAT_GET_TOUCH, &wl_touch_interface, 0);
    mwinWlListen(&platform->api, platform->touch.touch, &s_touchListener, platform);
}

void mwinWaylandRemoveTouch(mwinWaylandPlatform* platform)
{
    if (platform->touch.touch != nullptr)
    {
        Release(&platform->api, platform->touch.touch, WL_TOUCH_RELEASE,
                WL_TOUCH_RELEASE_SINCE_VERSION);
    }
    platform->touch = (mwinWaylandTouch){0};
}

void mwinWaylandForgetPointerFocus(mwinWaylandPlatform* platform, uint32_t slot)
{
    if (platform->pointer.focus == (int32_t)slot)
    {
        platform->pointer.focus = -1;
        platform->pointer.buttons = 0;
        platform->pointer.moved = false;
    }
    for (int i = 0; i < MWIN_WAYLAND_TOUCHES; i++)
    {
        if (platform->touch.points[i].active && platform->touch.points[i].slot == (int32_t)slot)
        {
            platform->touch.points[i].active = false;
        }
    }
}
