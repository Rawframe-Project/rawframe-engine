// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Android's pointers as the contract's records (android_motion.h).

#include "android_motion.h"

#include <math.h>
#include <string.h>

#define DEGREES_PER_RADIAN 57.29577951308232f

static void Post(mwinContext* context, uint32_t slot, const mwinAndroidMotion* motion,
                 mwinEvent* event)
{
    event->timeNs = motion->timeNs;
    mwinPost(context, slot, event);
}

static float Unit(float value)
{
    return value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
}

// The touch of an Android pointer id, or null.
static mwinAndroidTouch* TouchOf(mwinAndroidPointers* pointers, int32_t id)
{
    for (size_t i = 0; i < MWIN_ANDROID_POINTERS; i++)
    {
        if (pointers->touches[i].down && pointers->touches[i].pointer == id)
        {
            return &pointers->touches[i];
        }
    }
    return nullptr;
}

static void PostTouch(mwinContext* context, uint32_t slot, const mwinAndroidMotion* motion,
                      mwinEventType type, const mwinAndroidTouch* touch, float pressure)
{
    mwinEvent event = {.type = type};
    event.data.touch = (mwinTouchEvent){touch->id, touch->position, pressure};
    Post(context, slot, motion, &event);
}

static void TouchDown(mwinContext* context, uint32_t slot, mwinAndroidPointers* pointers,
                      const mwinAndroidMotion* motion, const mwinAndroidPointer* pointer)
{
    for (size_t i = 0; i < MWIN_ANDROID_POINTERS; i++)
    {
        mwinAndroidTouch* touch = &pointers->touches[i];
        if (!touch->down)
        {
            pointers->nextTouch += 1;
            *touch = (mwinAndroidTouch){true, pointer->id, pointers->nextTouch, pointer->position};
            PostTouch(context, slot, motion, mwin_eventTouchDown, touch, Unit(pointer->pressure));
            return;
        }
    }
}

static void TouchUp(mwinContext* context, uint32_t slot, mwinAndroidPointers* pointers,
                    const mwinAndroidMotion* motion, const mwinAndroidPointer* pointer)
{
    mwinAndroidTouch* touch = TouchOf(pointers, pointer->id);
    if (touch != nullptr)
    {
        touch->position = pointer->position;
        PostTouch(context, slot, motion, mwin_eventTouchUp, touch, Unit(pointer->pressure));
        touch->down = false;
    }
}

// A finger moved, where it moved.
static void TouchMoved(mwinContext* context, uint32_t slot, mwinAndroidPointers* pointers,
                       const mwinAndroidMotion* motion, const mwinAndroidPointer* pointer)
{
    mwinAndroidTouch* touch = TouchOf(pointers, pointer->id);
    if (touch != nullptr &&
        (touch->position.x != pointer->position.x || touch->position.y != pointer->position.y))
    {
        touch->position = pointer->position;
        PostTouch(context, slot, motion, mwin_eventTouchMoved, touch, Unit(pointer->pressure));
    }
}

static void CancelTouches(mwinContext* context, uint32_t slot, mwinAndroidPointers* pointers,
                          const mwinAndroidMotion* motion)
{
    for (size_t i = 0; i < MWIN_ANDROID_POINTERS; i++)
    {
        mwinAndroidTouch* touch = &pointers->touches[i];
        if (touch->down)
        {
            PostTouch(context, slot, motion, mwin_eventTouchCancelled, touch, -1.0f);
            touch->down = false;
        }
    }
}

static void PostPen(mwinContext* context, uint32_t slot, const mwinAndroidPointers* pointers,
                    const mwinAndroidMotion* motion, const mwinAndroidPointer* pointer,
                    mwinEventType type, uint8_t button)
{
    // As Chromium turns Android's tilt and orientation into tilts toward
    // x and y.
    float across = sinf(pointer->tilt);
    float up = cosf(pointer->tilt);
    mwinPenFlags flags = (mwinPenFlags)((pointer->tool == mwin_androidEraser ? mwin_penEraser : 0) |
                                        (pointers->penContact ? mwin_penContact : 0) |
                                        (pointers->penBarrel ? mwin_penBarrel : 0));
    mwinEvent event = {.type = type};
    event.data.pen = (mwinPenEvent){
        .position = pointer->position,
        .pressure = pointers->penContact ? Unit(pointer->pressure) : 0.0f,
        .tiltX = atan2f(sinf(-pointer->orientation) * across, up) * DEGREES_PER_RADIAN,
        .tiltY = atan2f(cosf(-pointer->orientation) * across, up) * DEGREES_PER_RADIAN,
        .flags = flags,
        .button = button,
    };
    Post(context, slot, motion, &event);
}

static void Pen(mwinContext* context, uint32_t slot, mwinAndroidPointers* pointers,
                const mwinAndroidMotion* motion, const mwinAndroidPointer* pointer)
{
    bool barrel = (motion->buttons & mwin_androidStylusPrimary) != 0;
    if (barrel != pointers->penBarrel)
    {
        pointers->penBarrel = barrel;
        PostPen(context, slot, pointers, motion, pointer,
                barrel ? mwin_eventPenButtonDown : mwin_eventPenButtonUp, 1);
    }
    switch (motion->action)
    {
    case mwin_androidDown:
    case mwin_androidPointerDown:
        pointers->penContact = true;
        PostPen(context, slot, pointers, motion, pointer, mwin_eventPenDown, 0);
        break;
    case mwin_androidUp:
    case mwin_androidPointerUp:
    case mwin_androidCancel:
        if (pointers->penContact)
        {
            pointers->penContact = false;
            PostPen(context, slot, pointers, motion, pointer, mwin_eventPenUp, 0);
        }
        break;
    case mwin_androidMove:
    case mwin_androidHoverEnter:
    case mwin_androidHoverMove:
        PostPen(context, slot, pointers, motion, pointer, mwin_eventPenMoved, 0);
        break;
    default:
        break;
    }
}

// A touch screen's sample: each pointer by its tool, the one an action
// is about for a press or a release, every one for a move.
static void Screen(mwinContext* context, uint32_t slot, mwinAndroidPointers* pointers,
                   const mwinAndroidMotion* motion)
{
    if (motion->action == mwin_androidCancel)
    {
        CancelTouches(context, slot, pointers, motion);
    }
    bool every = motion->action == mwin_androidMove || motion->action == mwin_androidCancel;
    for (uint32_t i = 0; i < motion->count; i++)
    {
        const mwinAndroidPointer* pointer = &motion->pointers[i];
        bool pen = pointer->tool == mwin_androidStylus || pointer->tool == mwin_androidEraser;
        if (!every && i != motion->index)
        {
            continue;
        }
        if (pen)
        {
            Pen(context, slot, pointers, motion, pointer);
            continue;
        }
        switch (motion->action)
        {
        case mwin_androidDown:
        case mwin_androidPointerDown:
            TouchDown(context, slot, pointers, motion, pointer);
            break;
        case mwin_androidUp:
        case mwin_androidPointerUp:
            TouchUp(context, slot, pointers, motion, pointer);
            break;
        case mwin_androidMove:
            TouchMoved(context, slot, pointers, motion, pointer);
            break;
        default:
            break;
        }
    }
}

// The contract's button of one of Android's, or 0.
static mwinMouseButton ButtonOf(uint32_t button)
{
    switch (button)
    {
    case mwin_androidPrimary:
        return mwin_buttonLeft;
    case mwin_androidSecondary:
        return mwin_buttonRight;
    case mwin_androidTertiary:
        return mwin_buttonMiddle;
    case mwin_androidBack:
        return mwin_buttonBack;
    case mwin_androidForward:
        return mwin_buttonForward;
    default:
        return 0;
    }
}

static void PostCursor(mwinContext* context, uint32_t slot, const mwinAndroidPointers* pointers,
                       const mwinAndroidMotion* motion, mwinEventType type, mwinMouseButton button,
                       uint8_t clicks)
{
    mwinEvent event = {.type = type};
    event.data.pointer = (mwinPointerEvent){motion->pointers[0].position, motion->modifiers,
                                            pointers->buttons, button, clicks};
    Post(context, slot, motion, &event);
}

static void Press(mwinContext* context, uint32_t slot, mwinAndroidPointers* pointers,
                  const mwinAndroidMotion* motion, mwinMouseButton button)
{
    uint8_t bit = (uint8_t)(1u << (button - 1));
    if (button == 0 || (pointers->buttons & bit) != 0)
    {
        return;
    }
    pointers->buttons |= bit;
    uint8_t clicks =
        mwinCountClickWithin(&pointers->clicks, button, motion->pointers[0].position,
                             motion->timeNs, pointers->doubleClickNs, MWIN_DOUBLE_CLICK_DISTANCE);
    PostCursor(context, slot, pointers, motion, mwin_eventButtonDown, button, clicks);
}

static void Release(mwinContext* context, uint32_t slot, mwinAndroidPointers* pointers,
                    const mwinAndroidMotion* motion, mwinMouseButton button)
{
    uint8_t bit = (uint8_t)(1u << (button - 1));
    if (button == 0 || (pointers->buttons & bit) == 0)
    {
        return;
    }
    pointers->buttons &= (uint8_t)~bit;
    PostCursor(context, slot, pointers, motion, mwin_eventButtonUp, button, 0);
}

static void ReleaseAll(mwinContext* context, uint32_t slot, mwinAndroidPointers* pointers,
                       const mwinAndroidMotion* motion)
{
    for (uint32_t button = mwin_buttonLeft; button <= mwin_buttonForward; button++)
    {
        Release(context, slot, pointers, motion, (mwinMouseButton)button);
    }
}

// The buttons whose state changed since the last press or release.
static void Buttons(mwinContext* context, uint32_t slot, mwinAndroidPointers* pointers,
                    const mwinAndroidMotion* motion)
{
    uint32_t pressed = motion->buttons & ~pointers->androidButtons;
    uint32_t released = pointers->androidButtons & ~motion->buttons;
    pointers->androidButtons = motion->buttons;
    for (uint32_t button = mwin_androidPrimary; button <= mwin_androidForward; button <<= 1)
    {
        if ((released & button) != 0)
        {
            Release(context, slot, pointers, motion, ButtonOf(button));
        }
        if ((pressed & button) != 0)
        {
            Press(context, slot, pointers, motion, ButtonOf(button));
        }
    }
}

static void Cursor(mwinContext* context, uint32_t slot, mwinAndroidPointers* pointers,
                   const mwinAndroidMotion* motion)
{
    if (motion->count == 0)
    {
        return;
    }
    if (!pointers->cursorInside && motion->action != mwin_androidHoverExit)
    {
        pointers->cursorInside = true;
        PostCursor(context, slot, pointers, motion, mwin_eventCursorEntered, 0, 0);
    }
    switch (motion->action)
    {
    case mwin_androidMove:
    case mwin_androidHoverMove:
        PostCursor(context, slot, pointers, motion, mwin_eventCursorMoved, 0, 0);
        break;
    case mwin_androidButtonPress:
    case mwin_androidButtonRelease:
        Buttons(context, slot, pointers, motion);
        break;
    case mwin_androidDown:
        if (motion->buttons == 0)
        {
            Press(context, slot, pointers, motion, mwin_buttonLeft);
        }
        break;
    case mwin_androidUp:
    case mwin_androidCancel:
        pointers->androidButtons = 0;
        ReleaseAll(context, slot, pointers, motion);
        break;
    case mwin_androidScroll:
    {
        mwinEvent event = {.type = mwin_eventWheel};
        event.data.wheel = motion->scroll;
        Post(context, slot, motion, &event);
        break;
    }
    default:
        break;
    }
}

void mwinAndroidMotionSample(mwinContext* context, uint32_t slot, mwinAndroidPointers* pointers,
                             const mwinAndroidMotion* motion)
{
    const mwinAndroidPointer* first = motion->count > 0 ? &motion->pointers[0] : nullptr;
    bool pen = first != nullptr &&
               (first->tool == mwin_androidStylus || first->tool == mwin_androidEraser);
    if (motion->mouse && !pen)
    {
        Cursor(context, slot, pointers, motion);
    }
    else
    {
        Screen(context, slot, pointers, motion);
    }
}

void mwinAndroidForgetPointers(mwinAndroidPointers* pointers)
{
    memset(pointers->touches, 0, sizeof(pointers->touches));
    pointers->penContact = false;
    pointers->penBarrel = false;
    pointers->cursorInside = false;
    pointers->buttons = 0;
    pointers->androidButtons = 0;
}
