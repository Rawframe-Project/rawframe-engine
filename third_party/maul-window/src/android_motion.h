// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Android's pointers as the contract's records (mwin-0026), with no
// platform type: the backend reads each motion event into samples
// (android_input.c), and these functions turn them into touches, the
// pen and the cursor, which tests drive on any platform.
//
// - A finger (or a tool Android does not know) on a touch screen is a
//   touch, whose id counts up from the first and is never reused, as
//   Android's pointer ids are.
// - A stylus or its eraser is the pen: hovering, touching, its barrel
//   button (Android's primary stylus button); tilt and orientation
//   become tilts toward x and y as Chromium computes them.
// - A mouse is the cursor. Its buttons come from Android's button press
//   and release, the buttons that changed being those whose state
//   differs from the last sample's (the NDK names the changed button only
//   from API 33); a press with no button state (an injected one) is the
//   primary button, and the pointer's going up releases what is still
//   held. Android ends hovering before every
//   press and begins it again after (crbug.com/715114), so hovering's
//   start and end tell nothing: the cursor enters at its first motion
//   over the window and is never said to leave. Its scrolling is the
//   wheel, in Android's detents.

#ifndef MAUL_WINDOW_SRC_ANDROID_MOTION_H
#define MAUL_WINDOW_SRC_ANDROID_MOTION_H

#include "clicks.h"
#include "core.h"

// The pointers a sample carries, as many as Android's motion events.
#define MWIN_ANDROID_POINTERS 16

// What a pointer is.
typedef enum mwinAndroidTool
{
    mwin_androidFinger,
    mwin_androidStylus,
    mwin_androidEraser,
    mwin_androidMouse,
} mwinAndroidTool;

// What a sample does, as Android's masked actions.
typedef enum mwinAndroidAction
{
    mwin_androidDown,
    mwin_androidUp,
    mwin_androidMove,
    mwin_androidCancel,
    mwin_androidPointerDown,
    mwin_androidPointerUp,
    mwin_androidHoverEnter,
    mwin_androidHoverMove,
    mwin_androidHoverExit,
    mwin_androidScroll,
    mwin_androidButtonPress,
    mwin_androidButtonRelease,
    mwin_androidOther,
} mwinAndroidAction;

// Android's buttons, by their values (AMOTION_EVENT_BUTTON_*).
enum
{
    mwin_androidPrimary = 1,
    mwin_androidSecondary = 2,
    mwin_androidTertiary = 4,
    mwin_androidBack = 8,
    mwin_androidForward = 16,
    mwin_androidStylusPrimary = 32,
};

// A pointer of a sample: Android's id, its tool, where it is in logical
// units, its pressure, and a stylus's tilt and orientation in radians.
typedef struct mwinAndroidPointer
{
    int32_t id;
    mwinAndroidTool tool;
    mwinPosition position;
    float pressure;
    float tilt;
    float orientation;
} mwinAndroidPointer;

// One sample of a motion event: its action and the pointer it is about,
// whether it comes from a mouse, Android's buttons held, its scrolling,
// the modifiers, its time and its pointers.
typedef struct mwinAndroidMotion
{
    mwinAndroidAction action;
    uint32_t index;
    bool mouse;
    uint32_t buttons;
    mwinWheelEvent scroll;
    mwinModifiers modifiers;
    uint64_t timeNs;
    uint32_t count;
    mwinAndroidPointer pointers[MWIN_ANDROID_POINTERS];
} mwinAndroidMotion;

// A touch down: Android's pointer id, its record id and where it was
// last told to be.
typedef struct mwinAndroidTouch
{
    bool down;
    int32_t pointer;
    uint64_t id;
    mwinPosition position;
} mwinAndroidTouch;

// What the window's pointers are doing: its touches and the id the next
// takes; whether the pen touches and its barrel is held; whether the
// cursor came over the window, the buttons it holds (bit b - 1 for
// button b) and Android's button state when last pressed or released,
// its quick clicks and the time they may be apart.
typedef struct mwinAndroidPointers
{
    mwinAndroidTouch touches[MWIN_ANDROID_POINTERS];
    uint64_t nextTouch;
    bool penContact;
    bool penBarrel;
    bool cursorInside;
    uint8_t buttons;
    uint32_t androidButtons;
    mwinClickCounter clicks;
    uint64_t doubleClickNs;
} mwinAndroidPointers;

// Posts what a sample tells of the window in a slot.
void mwinAndroidMotionSample(mwinContext* context, uint32_t slot, mwinAndroidPointers* pointers,
                             const mwinAndroidMotion* motion);

// Forgets the pointers (the application suspending): touches, the pen's
// contact, the cursor's buttons, its being over the window.
void mwinAndroidForgetPointers(mwinAndroidPointers* pointers);

#endif // MAUL_WINDOW_SRC_ANDROID_MOTION_H
