// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The event stream. The program drains it in frame with mwinNextEvent,
// which returns every record in the order it arrived, across windows.
// Records wait in storage per window, each with its own limit, so one
// busy window cannot push out another's records. Notifications that
// must be handled before the platform goes on (the application's
// lifecycle, a lost or restored surface) come before all others; when
// the platform waits for the program to handle one, the library runs a
// frame at once, from inside the platform's call, so the program sees it
// in time. The context's own notifications carry the null window id.
//
// A request's completion comes after the notifications the change
// caused: a size request is answered after mwin_eventResized.
//
// Input comes in four classes, each with its own storage per window.
// Discrete records (keys, text, buttons, touches and pen contacts
// beginning or ending) are never merged: when their storage is full the
// window gets mwin_eventInputStateReset instead, which also follows every
// loss of focus, after which no key or button counts as held. Motion,
// raw deltas and the wheel are delivered sample by sample while there is
// room, and merged into the newest waiting record of their kind when
// there is not; samples says how many a record stands for.
//
// Text in a record stays valid until the frame that drained it returns.
//
// Gamepads' records are the context's, in storage of their own: their
// buttons never merge, and a gamepad whose button records were lost
// gets mwin_eventInputStateReset; an axis moving when its storage is
// full merges into the newest waiting record of that axis.
//
// An input method composes text in place before it commits it. While a
// window accepts text (mwinRequestTextInput), mwin_eventImePreedit
// reports the text being composed, with its caret, its selection and
// styled segments; an empty preedit ends the composition. Committed text
// arrives as mwin_eventTextInput. While a composition runs, the keys it
// consumes produce no key records: a key that types a character and goes
// down during a composition is left out, and so is its release.

#ifndef MAUL_WINDOW_EVENT_H
#define MAUL_WINDOW_EVENT_H

#include "maul-window/gamepad.h"
#include "maul-window/input.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // What a record reports.
    typedef uint16_t mwinEventType;

    enum
    {
        mwin_eventNone = 0,
        // The platform made the window.
        mwin_eventWindowCreated = 1,
        // The user asked to close the window; nothing closes by itself.
        mwin_eventCloseRequested = 2,
        // The window was destroyed; its id is stale.
        mwin_eventWindowDestroyed = 3,
        // The logical size changed (data.size).
        mwin_eventResized = 4,
        // The size in pixels changed (data.pixelSize).
        mwin_eventPixelSizeChanged = 5,
        // The scale changed (data.scale), with the size the platform
        // suggests for it.
        mwin_eventScaleChanged = 6,
        // The window moved (data.position).
        mwin_eventMoved = 7,
        mwin_eventFocusGained = 8,
        mwin_eventFocusLost = 9,
        // Nothing of the window can be seen, or it can be again.
        mwin_eventOccluded = 10,
        mwin_eventRevealed = 11,
        // The mode changed (data.mode).
        mwin_eventModeChanged = 12,
        mwin_eventShown = 13,
        mwin_eventHidden = 14,
        // A request was answered (data.completion).
        mwin_eventRequestCompleted = 15,
        // Forget every key and button held: focus was lost or input was.
        // About a gamepad (the null window, data.gamepad), read its state
        // again (mwinGetGamepadState).
        mwin_eventInputStateReset = 16,
        // A key went down or up (data.key).
        mwin_eventKeyDown = 17,
        mwin_eventKeyUp = 18,
        // Text was typed or composed (data.text), validated UTF-8.
        mwin_eventTextInput = 19,
        // The cursor moved over the window (data.pointer).
        mwin_eventCursorMoved = 20,
        mwin_eventCursorEntered = 21,
        mwin_eventCursorLeft = 22,
        // A mouse button went down or up (data.pointer).
        mwin_eventButtonDown = 23,
        mwin_eventButtonUp = 24,
        // The wheel turned (data.wheel).
        mwin_eventWheel = 25,
        // The pointing device moved while the window holds the cursor
        // captured, unscaled (data.delta).
        mwin_eventRawPointerDelta = 26,
        // A touch began, moved, ended or was taken by the system
        // (data.touch).
        mwin_eventTouchDown = 27,
        mwin_eventTouchMoved = 28,
        mwin_eventTouchUp = 29,
        mwin_eventTouchCancelled = 30,
        // A pen moved, touched down, lifted, or a pen button changed
        // (data.pen).
        mwin_eventPenMoved = 31,
        mwin_eventPenDown = 32,
        mwin_eventPenUp = 33,
        mwin_eventPenButtonDown = 34,
        mwin_eventPenButtonUp = 35,
        // The application is about to stop running: save what must
        // survive now. Every window's input state resets.
        mwin_eventSuspending = 36,
        // The application stopped running; nothing is drawn.
        mwin_eventSuspended = 37,
        // The application is about to run again.
        mwin_eventResuming = 38,
        // The application runs again.
        mwin_eventResumed = 39,
        // The window's surface is gone (a rotation, the application in the
        // background, a hidden canvas): stop drawing to it.
        mwin_eventSurfaceLost = 40,
        // The window has a surface again.
        mwin_eventSurfaceRestored = 41,
        // A monitor was connected, disconnected, or its facts changed
        // (data.monitor).
        mwin_eventMonitorAdded = 42,
        mwin_eventMonitorRemoved = 43,
        mwin_eventMonitorChanged = 44,
        // The window moved to another monitor (data.monitor).
        mwin_eventDisplayChanged = 45,
        // The window's safe area changed (data.insets).
        mwin_eventSafeAreaChanged = 46,
        // An on-screen keyboard now covers data.rect of the window.
        mwin_eventVirtualKeyboardChanged = 47,
        // The system's look changed: its theme, accent color, reduced
        // motion or text scale (mwinGetSystemFacts).
        mwin_eventThemeChanged = 48,
        // The power source or power mode changed (mwinGetSystemFacts).
        mwin_eventPowerChanged = 49,
        // The user's preferred locales changed (mwinGetPreferredLocales).
        mwin_eventLocaleChanged = 50,
        // The keyboard layout changed (mwinGetKeyboardLayout,
        // mwinMapKeyCode).
        mwin_eventKeyboardLayoutChanged = 51,
        // An input method's composition changed (data.preedit).
        mwin_eventImePreedit = 52,
        // A gamepad was connected, was disconnected, or its facts changed
        // (data.gamepad).
        mwin_eventGamepadAdded = 53,
        mwin_eventGamepadRemoved = 54,
        mwin_eventGamepadChanged = 55,
        // A gamepad's button was pressed or let go (data.gamepadButton).
        mwin_eventGamepadButtonDown = 56,
        mwin_eventGamepadButtonUp = 57,
        // A gamepad's axis moved (data.gamepadAxis).
        mwin_eventGamepadAxisMoved = 58,
        // Something dragged came over the window, moved over it, or left
        // it without a drop (data.drag).
        mwin_eventDragEntered = 59,
        mwin_eventDragMoved = 60,
        mwin_eventDragLeft = 61,
        // Something was dropped on the window (data.drop):
        // mwinGetDroppedFiles and mwinGetDroppedText have it.
        mwin_eventDropped = 62,
        // An accessibility client asked the window for its tree for the
        // first time: the program's adapter may start its updates. Once
        // a window.
        mwin_eventAccessibilityRequested = 63,
    };

    // The kind of a request.
    typedef uint8_t mwinRequestKind;

    enum
    {
        mwin_requestCreate = 0,
        mwin_requestTitle = 1,
        mwin_requestSize = 2,
        mwin_requestPosition = 3,
        mwin_requestMode = 4,
        mwin_requestVisible = 5,
        mwin_requestFocus = 6,
        mwin_requestCursorMode = 7,
        mwin_requestCursorShape = 8,
        mwin_requestVirtualKeyboard = 9,
        mwin_requestTextInput = 10,
        mwin_requestSizeLimits = 11,
        mwin_requestAspectRatio = 12,
        mwin_requestStyle = 13,
        mwin_requestOpacity = 14,
        mwin_requestClipboardWrite = 15,
        mwin_requestClipboardRead = 16,
        mwin_requestOpenUrl = 17,
        mwin_requestRevealFile = 18,
        mwin_requestKeepAwake = 19,
        mwin_requestFileDialog = 20,
        mwin_requestIcon = 21,
        mwin_requestHitRegions = 22,
        mwin_requestAccessibilityRoot = 23,
        mwin_requestCursorImage = 24,
        mwin_requestClipboardWriteData = 25,
        mwin_requestClipboardReadData = 26,
        mwin_requestPrimaryWrite = 27,
        mwin_requestPrimaryRead = 28,
    };

    // How a request ended.
    typedef uint8_t mwinOutcome;

    enum
    {
        // The platform did what was asked; the notifications before the
        // completion say what it chose.
        mwin_outcomeDone = 0,
        // The platform or this backend cannot do it.
        mwin_outcomeUnsupported = 1,
        // The platform or the user refused.
        mwin_outcomeDenied = 2,
        // A later request of the same kind on the same window replaced it.
        mwin_outcomeSuperseded = 3,
        // Its window was destroyed first, or the user closed a dialog
        // without choosing.
        mwin_outcomeCancelled = 4,
        // The platform failed.
        mwin_outcomeFailed = 5,
        // What the platform gave did not fit its limit: a clipboard read
        // past clipboardBytes, a dialog's choice past dialogFiles or
        // dialogBytes.
        mwin_outcomeTooLarge = 6,
    };

    // The answer to a request.
    typedef struct mwinCompletion
    {
        mwinRequestId request;
        mwinRequestKind kind;
        mwinOutcome outcome;
    } mwinCompletion;

    // A scale change and the logical size the platform suggests with it.
    typedef struct mwinScaleChange
    {
        float scale;
        mwinSize suggestedSize;
    } mwinScaleChange;

    // A key going down or up.
    typedef struct mwinKeyEvent
    {
        mwinKeyCode code;
        mwinModifiers modifiers;
        mwinKey key;
        // The platform repeats a held key.
        bool repeat;
    } mwinKeyEvent;

    // Typed or composed text, valid until the frame that drained it
    // returns.
    typedef struct mwinTextEvent
    {
        const char* text;
        uint32_t length;
    } mwinTextEvent;

    // The most segments a composition may have.
#define MWIN_MAX_PREEDIT_SEGMENTS 32

    // How a segment of a composition is shown.
    typedef uint8_t mwinPreeditStyle;

    enum
    {
        mwin_preeditPlain = 0,
        // Underlined: text still to convert.
        mwin_preeditUnderline = 1,
        // Highlighted: the part a conversion works on now.
        mwin_preeditTarget = 2,
        // Converted but not yet committed.
        mwin_preeditConverted = 3,
    };

    // A styled part of a composition, in bytes of its text.
    typedef struct mwinPreeditSegment
    {
        uint32_t start;
        uint32_t length;
        mwinPreeditStyle style;
    } mwinPreeditSegment;

    // An input method's composition. The text, its segments and the
    // offsets are in bytes, each on a character boundary within the text,
    // the selection in order and no segment empty; the caret is -1 where
    // the method hides it. The library fits what the platform hands it
    // (an offset inside a character, past the end, a selection backwards)
    // to that (mwin-0034). Valid until the frame that drained it returns.
    typedef struct mwinPreeditEvent
    {
        const char* text;
        uint32_t length;
        int32_t caret;
        uint32_t selectionStart;
        uint32_t selectionEnd;
        const mwinPreeditSegment* segments;
        uint32_t segmentCount;
    } mwinPreeditEvent;

    // The cursor: where it is, the buttons held, and for a button record
    // the button that changed and the count of quick clicks it completes.
    typedef struct mwinPointerEvent
    {
        mwinPosition position;
        mwinModifiers modifiers;
        // Bit b - 1 is set while button b is held.
        uint8_t buttons;
        mwinMouseButton button;
        uint8_t clicks;
    } mwinPointerEvent;

    // Wheel turns in detents, fractional for smooth wheels and touchpads;
    // positive y is away from the user, positive x to the right.
    typedef struct mwinWheelEvent
    {
        float x;
        float y;
    } mwinWheelEvent;

    // Relative motion in the device's own units, before any acceleration
    // the platform can leave out.
    typedef struct mwinDeltaEvent
    {
        float x;
        float y;
    } mwinDeltaEvent;

    // A touch, by an id stable from its down to its up or cancel.
    typedef struct mwinTouchEvent
    {
        uint64_t id;
        mwinPosition position;
        // From 0 to 1, or -1 where the platform does not measure it.
        float pressure;
    } mwinTouchEvent;

    // The pen's state.
    typedef uint8_t mwinPenFlags;

    enum
    {
        // The eraser end is in use.
        mwin_penEraser = 1,
        // The tip touches the surface; otherwise the pen hovers.
        mwin_penContact = 2,
        // The barrel button is held.
        mwin_penBarrel = 4,
    };

    // A pen, where the platform has one.
    typedef struct mwinPenEvent
    {
        mwinPosition position;
        // From 0 to 1.
        float pressure;
        // Degrees from upright, toward positive x and positive y.
        float tiltX;
        float tiltY;
        mwinPenFlags flags;
        // For a pen button record, the button: 1 for the barrel.
        uint8_t button;
    } mwinPenEvent;

    // What a drag carries.
    typedef uint8_t mwinDragContents;

    enum
    {
        mwin_dragFiles = 1,
        mwin_dragText = 2,
    };

    // A drag over a window.
    typedef struct mwinDragEvent
    {
        mwinPosition position;
        mwinDragContents contents;
    } mwinDragEvent;

    // A drop: its files and text wait under its number until the next
    // drop.
    typedef struct mwinDropEvent
    {
        mwinPosition position;
        uint32_t drop;
        // The files and the bytes of text it delivers.
        uint32_t fileCount;
        uint32_t textLength;
        // Files or text were left out: past the droppedFiles or dropBytes
        // limit, or a path that is not UTF-8.
        bool truncated;
    } mwinDropEvent;

    // One record of the stream.
    typedef struct mwinEvent
    {
        mwinEventType type;
        // How many platform samples the record stands for: 1, or more for
        // motion merged when its storage was full.
        uint16_t samples;
        // The window it is about.
        mwinWindowId window;
        // Nanoseconds on a monotonic clock, from the platform's own event
        // time where it has one.
        uint64_t timeNs;
        union
        {
            mwinSize size;
            mwinPixelSize pixelSize;
            mwinScaleChange scale;
            mwinPosition position;
            mwinWindowMode mode;
            mwinCompletion completion;
            mwinKeyEvent key;
            mwinTextEvent text;
            mwinPointerEvent pointer;
            mwinWheelEvent wheel;
            mwinDeltaEvent delta;
            mwinTouchEvent touch;
            mwinPenEvent pen;
            mwinMonitorId monitor;
            mwinInsets insets;
            mwinRect rect;
            mwinPreeditEvent preedit;
            mwinGamepadId gamepad;
            mwinGamepadButtonEvent gamepadButton;
            mwinGamepadAxisEvent gamepadAxis;
            mwinDragEvent drag;
            mwinDropEvent drop;
        } data;
    } mwinEvent;

    /// Takes the next record of the stream.
    ///
    /// @param context   The context.
    /// @param eventOut  Receives the record.
    /// @return `mwin_success`; `mwin_empty` when the stream is drained;
    ///         `mwin_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinNextEvent(mwinContext* context, mwinEvent* eventOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_WINDOW_EVENT_H
