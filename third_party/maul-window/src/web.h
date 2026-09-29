// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the web backend keeps. The page's side lives in JavaScript, one
// object per context: the canvases, their observers and listeners, and a
// queue of what the browser reported, drained at the start of each
// frame. This side keeps per window slot what the program was told.

#ifndef MAUL_WINDOW_SRC_WEB_H
#define MAUL_WINDOW_SRC_WEB_H

#include "clicks.h"
#include "core.h"
#include "web_pad.h"

// A canvas's selector as the native handles give it: "#" and its id.
#define MWIN_WEB_SELECTOR_BYTES 128

// The most of navigator.languages read at once.
#define MWIN_WEB_LOCALE_BYTES 512

// A window: its canvas, and its size, pixels and scale as last posted.
typedef struct mwinWebWindow
{
    char selector[MWIN_WEB_SELECTOR_BYTES];
    uint32_t selectorLength;
    // The selector of the host of the program's accessibility elements:
    // the canvas's and "-accessibility" (web_page.c).
    char host[MWIN_WEB_SELECTOR_BYTES];
    uint32_t hostLength;
    // The page opened the window's canvas: a failed or unsupported
    // creation leaves nothing to close.
    bool open;
    mwinSize size;
    mwinPixelSize pixels;
    float scale;
    bool fullscreen;
    // The cursor the program asked for, the buttons held, the last
    // press, and the pen's flags at its last record.
    mwinCursorMode cursorMode;
    mwinCursorShape cursorShape;
    uint8_t buttons;
    mwinClickCounter clicks;
    mwinPenFlags penFlags;
} mwinWebWindow;

typedef struct mwinWebPlatform
{
    mwinContext* context;
    // One per window slot.
    mwinWebWindow* windows;
    // The screen's monitor slot, or -1.
    int32_t monitor;
    // The page's devicePixelRatio.
    float scale;
    // A string of the page's as UTF-8: textBytesPerWindow and its
    // terminator.
    char* text;
    // The page went away (hidden, frozen or left) and has not come back.
    bool suspended;
    // The page keeps the screen awake (web_services.c).
    bool awake;
    // The gamepads, with the gamepad component.
    mwinWebPads pads;
} mwinWebPlatform;

// What the page reports, one record at a time.
typedef enum mwinWebRecordKind
{
    mwin_webNone = 0,
    // A canvas's box: x, y its CSS size, z, w its pixels.
    mwin_webResized = 1,
    // devicePixelRatio changed: x.
    mwin_webScaled = 2,
    // A canvas gained (code 1) or lost focus.
    mwin_webFocus = 3,
    // A canvas became (code 1) or stopped being the fullscreen element.
    mwin_webFullscreen = 4,
    // A canvas's fullscreen request failed.
    mwin_webFullscreenFailed = 5,
    // The page was shown (code 1) or hidden.
    mwin_webVisibility = 6,
    // A preference the facts come from changed.
    mwin_webFacts = 7,
    // The preferred languages changed.
    mwin_webLocales = 8,
    // A key: code its key code, extra the modifiers, x 1 when pressed, y
    // 1 for a repeat, z what it means (0 for its name).
    mwin_webKey = 10,
    // A character typed: code.
    mwin_webText = 11,
    // The mouse: code an mwinWebMouse, x, y where, z the button, extra
    // the modifiers.
    mwin_webMouse = 12,
    // The wheel: x, y in detents, right and away from the user.
    mwin_webWheel = 13,
    // Locked motion: x, y.
    mwin_webMotion = 14,
    // A touch: code an mwinWebContact, extra its id, x, y, z pressure.
    mwin_webTouch = 15,
    // A pen: extra its mwinPenFlags, x, y, z pressure, w and v its tilt.
    mwin_webPen = 16,
    // The pointer was locked (code 1) or let go.
    mwin_webLock = 17,
    // A pointer lock request failed.
    mwin_webLockFailed = 18,
    // The keyboard layout changed.
    mwin_webLayout = 19,
    // Text an input method or an on-screen keyboard typed: its string.
    mwin_webCommit = 20,
    // A composition: its string, empty when it ends, and code its caret.
    mwin_webPreedit = 21,
    // A canvas left the document, or came back.
    mwin_webSurfaceLost = 22,
    mwin_webSurfaceRestored = 23,
    // A clipboard write or read ended: code its mwinOutcome; a read that
    // is done has its text waiting as bytes.
    mwin_webClipboardWritten = 24,
    mwin_webClipboardRead = 25,
    // A drag over a canvas: code 0 entered, 1 moved, 2 left; x, y where,
    // z its mwinDragContents.
    mwin_webDrag = 26,
    // A drop: x, y where; its names and text wait in the page.
    mwin_webDropped = 27,
} mwinWebRecordKind;

typedef enum mwinWebMouse
{
    mwin_webMoved = 0,
    mwin_webEntered = 1,
    mwin_webLeft = 2,
    mwin_webPressed = 3,
    mwin_webReleased = 4,
} mwinWebMouse;

typedef enum mwinWebContact
{
    mwin_webDown = 0,
    mwin_webMove = 1,
    mwin_webUp = 2,
    mwin_webCancel = 3,
} mwinWebContact;

// A record: its kind, the window's slot or -1, and what the kind says
// in a code, a second integer and up to six numbers.
typedef struct mwinWebRecord
{
    int32_t kind;
    int32_t slot;
    int32_t code;
    int32_t extra;
    float x;
    float y;
    float z;
    float w;
    float v;
    float u;
    // The event's time on performance.now(), in milliseconds.
    double timeMs;
} mwinWebRecord;

// Milliseconds on performance.now() in nanoseconds.
static inline uint64_t mwinWebNanoseconds(double milliseconds)
{
    return milliseconds > 0.0 ? (uint64_t)(milliseconds * 1000000.0) : 0;
}

#endif // MAUL_WINDOW_SRC_WEB_H
