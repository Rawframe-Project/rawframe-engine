// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Touches, the Pencil and the pointer on iOS (ios.h), from the view's
// touch methods and two gesture recognizers. Places are in the view's
// points.
//
// - A finger is a touch, by the UITouch's address, stable over its life;
//   its pressure is its force where the device measures it, else -1.
// - The Pencil is a pen touching the screen: its force is its pressure,
//   its altitude and azimuth its tilt. It has no eraser end and no
//   barrel button; its hover is not reported.
// - A mouse or trackpad on an iPad is the cursor: its clicks come as
//   touches of the indirect pointer type, where the program's Info.plist
//   sets UIApplicationSupportsIndirectInputEvents (else as fingers), with
//   the buttons held in the event's button mask, whose bits are the
//   contract's; its motion without a button through a hover recognizer,
//   entering and leaving; its scrolling through a pan recognizer that
//   takes scroll events only, ten points to a detent, the sign as on
//   macOS.

#include "ios.h"

#include <math.h>

// Scrolling's points per detent, as on macOS.
#define POINTS_PER_DETENT 10.0

mwinModifiers mwinIOSModifiersOf(UIKeyModifierFlags flags)
{
    mwinModifiers modifiers = 0;
    modifiers |= (flags & UIKeyModifierShift) != 0 ? mwin_modShift : 0;
    modifiers |= (flags & UIKeyModifierControl) != 0 ? mwin_modControl : 0;
    modifiers |= (flags & UIKeyModifierAlternate) != 0 ? mwin_modAlt : 0;
    modifiers |= (flags & UIKeyModifierCommand) != 0 ? mwin_modMeta : 0;
    modifiers |= (flags & UIKeyModifierAlphaShift) != 0 ? mwin_modCapsLock : 0;
    modifiers |= (flags & UIKeyModifierNumericPad) != 0 ? mwin_modNumLock : 0;
    return modifiers;
}

static void Post(mwinIOSPlatform* platform, uint32_t slot, mwinEvent* event)
{
    event->timeNs = mwinIOSNow();
    mwinPost(platform->context, slot, event);
}

static mwinPosition PlaceOf(UITouch* touch, UIView* view)
{
    CGPoint point = [touch locationInView:view];
    return (mwinPosition){(float)point.x, (float)point.y};
}

// A touch's force from 0 to 1, or -1 where the device measures none.
static float PressureOf(UITouch* touch, bool measured)
{
    if (!measured || touch.maximumPossibleForce <= 0.0)
    {
        return -1.0f;
    }
    return (float)fmin(fmax(touch.force / touch.maximumPossibleForce, 0.0), 1.0);
}

static void OnFinger(mwinIOSPlatform* platform, uint32_t slot, UITouch* touch,
                     mwinIOSTouchPhase phase)
{
    static const mwinEventType types[] = {mwin_eventTouchDown, mwin_eventTouchMoved,
                                          mwin_eventTouchUp, mwin_eventTouchCancelled};
    UIView* view = platform->windows[slot].view;
    bool measured = view.traitCollection.forceTouchCapability == UIForceTouchCapabilityAvailable;
    mwinEvent event = {.type = types[phase]};
    event.data.touch = (mwinTouchEvent){(uint64_t)(uintptr_t)touch, PlaceOf(touch, view),
                                        PressureOf(touch, measured)};
    Post(platform, slot, &event);
}

// Degrees from upright toward positive x and y, from the Pencil's
// altitude over the screen and the azimuth its cap points along.
static void TiltOf(UITouch* touch, UIView* view, float* x, float* y)
{
    double altitude = fmax(tan(touch.altitudeAngle), 1e-6);
    double azimuth = [touch azimuthAngleInView:view];
    *x = (float)(atan(cos(azimuth) / altitude) * 180.0 / M_PI);
    *y = (float)(atan(sin(azimuth) / altitude) * 180.0 / M_PI);
}

static void OnPencil(mwinIOSPlatform* platform, uint32_t slot, UITouch* touch,
                     mwinIOSTouchPhase phase)
{
    static const mwinEventType types[] = {mwin_eventPenDown, mwin_eventPenMoved, mwin_eventPenUp,
                                          mwin_eventPenUp};
    UIView* view = platform->windows[slot].view;
    bool touching = phase == mwin_iosTouchBegan || phase == mwin_iosTouchMoved;
    mwinEvent event = {.type = types[phase]};
    mwinPenEvent* pen = &event.data.pen;
    pen->position = PlaceOf(touch, view);
    pen->pressure = touching ? fmaxf(PressureOf(touch, true), 0.0f) : 0.0f;
    TiltOf(touch, view, &pen->tiltX, &pen->tiltY);
    pen->flags = touching ? mwin_penContact : 0;
    Post(platform, slot, &event);
}

static void PostPointer(mwinIOSPlatform* platform, uint32_t slot, mwinEventType type,
                        mwinPosition position, UIKeyModifierFlags flags, mwinMouseButton button,
                        uint8_t clicks)
{
    mwinEvent event = {.type = type};
    event.data.pointer = (mwinPointerEvent){position, mwinIOSModifiersOf(flags),
                                            platform->windows[slot].buttons, button, clicks};
    Post(platform, slot, &event);
}

// The buttons that changed since the last event, each a record; then
// the motion.
static void OnPointer(mwinIOSPlatform* platform, uint32_t slot, UITouch* touch, UIEvent* event,
                      mwinIOSTouchPhase phase)
{
    mwinIOSWindow* window = &platform->windows[slot];
    mwinPosition position = PlaceOf(touch, window->view);
    UIKeyModifierFlags flags = event.modifierFlags;
    uint8_t held = phase == mwin_iosTouchCancelled ? 0 : (uint8_t)(event.buttonMask & 0x1F);
    if (phase == mwin_iosTouchMoved)
    {
        PostPointer(platform, slot, mwin_eventCursorMoved, position, flags, 0, 0);
    }
    for (mwinMouseButton button = mwin_buttonLeft; button <= mwin_buttonForward; button++)
    {
        uint8_t bit = (uint8_t)(1u << (button - 1));
        if ((held & bit) == (window->buttons & bit))
        {
            continue;
        }
        window->buttons ^= bit;
        bool down = (held & bit) != 0;
        PostPointer(platform, slot, down ? mwin_eventButtonDown : mwin_eventButtonUp, position,
                    flags, button, down ? (uint8_t)touch.tapCount : 0);
    }
}

void mwinIOSTouches(mwinIOSPlatform* platform, uint32_t slot, NSSet<UITouch*>* touches,
                    UIEvent* event, mwinIOSTouchPhase phase)
{
    for (UITouch* touch in touches)
    {
        switch (touch.type)
        {
        case UITouchTypePencil:
            OnPencil(platform, slot, touch, phase);
            break;
        case UITouchTypeIndirectPointer:
            OnPointer(platform, slot, touch, event, phase);
            break;
        default:
            OnFinger(platform, slot, touch, phase);
            break;
        }
    }
}

void mwinIOSHover(mwinIOSPlatform* platform, uint32_t slot, UIHoverGestureRecognizer* hover)
{
    mwinIOSWindow* window = &platform->windows[slot];
    CGPoint point = [hover locationInView:window->view];
    mwinPosition position = {(float)point.x, (float)point.y};
    UIKeyModifierFlags flags = hover.modifierFlags;
    switch (hover.state)
    {
    case UIGestureRecognizerStateBegan:
        PostPointer(platform, slot, mwin_eventCursorEntered, position, flags, 0, 0);
        PostPointer(platform, slot, mwin_eventCursorMoved, position, flags, 0, 0);
        break;
    case UIGestureRecognizerStateChanged:
        PostPointer(platform, slot, mwin_eventCursorMoved, position, flags, 0, 0);
        break;
    case UIGestureRecognizerStateEnded:
    case UIGestureRecognizerStateCancelled:
        PostPointer(platform, slot, mwin_eventCursorLeft, position, flags, 0, 0);
        break;
    default:
        break;
    }
}

void mwinIOSScroll(mwinIOSPlatform* platform, uint32_t slot, UIPanGestureRecognizer* pan)
{
    UIView* view = platform->windows[slot].view;
    CGPoint moved = [pan translationInView:view];
    [pan setTranslation:CGPointZero inView:view];
    mwinWheelEvent wheel = {(float)(-moved.x / POINTS_PER_DETENT),
                            (float)(moved.y / POINTS_PER_DETENT)};
    if (wheel.x == 0.0f && wheel.y == 0.0f)
    {
        return;
    }
    mwinEvent event = {.type = mwin_eventWheel};
    event.data.wheel = wheel;
    Post(platform, slot, &event);
}
