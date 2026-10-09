// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Keyboard, mouse, touch and pen. A key has two names: its code, the
// physical key by its place on a keyboard (the USB HID usage, so "the
// key right of Tab" is mwin_codeKeyQ on every layout), and its key, what
// the current layout makes of it. Text is not keys: typed characters
// arrive as mwin_eventTextInput, which also carries what an input method
// composed.
//
// The cursor's position arrives as mwin_eventCursorMoved in the window's
// logical units; relative motion from the device arrives separately as
// mwin_eventRawPointerDelta, unscaled, and keeps coming while the cursor
// is captured.

#ifndef MAUL_WINDOW_INPUT_H
#define MAUL_WINDOW_INPUT_H

#include "maul-window/window.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // A physical key: its USB HID keyboard usage, named as the W3C
    // KeyboardEvent.code values name it.
    typedef uint16_t mwinKeyCode;

    enum
    {
        mwin_codeUnknown = 0,
        mwin_codeKeyA = 4,
        mwin_codeKeyB = 5,
        mwin_codeKeyC = 6,
        mwin_codeKeyD = 7,
        mwin_codeKeyE = 8,
        mwin_codeKeyF = 9,
        mwin_codeKeyG = 10,
        mwin_codeKeyH = 11,
        mwin_codeKeyI = 12,
        mwin_codeKeyJ = 13,
        mwin_codeKeyK = 14,
        mwin_codeKeyL = 15,
        mwin_codeKeyM = 16,
        mwin_codeKeyN = 17,
        mwin_codeKeyO = 18,
        mwin_codeKeyP = 19,
        mwin_codeKeyQ = 20,
        mwin_codeKeyR = 21,
        mwin_codeKeyS = 22,
        mwin_codeKeyT = 23,
        mwin_codeKeyU = 24,
        mwin_codeKeyV = 25,
        mwin_codeKeyW = 26,
        mwin_codeKeyX = 27,
        mwin_codeKeyY = 28,
        mwin_codeKeyZ = 29,
        mwin_codeDigit1 = 30,
        mwin_codeDigit2 = 31,
        mwin_codeDigit3 = 32,
        mwin_codeDigit4 = 33,
        mwin_codeDigit5 = 34,
        mwin_codeDigit6 = 35,
        mwin_codeDigit7 = 36,
        mwin_codeDigit8 = 37,
        mwin_codeDigit9 = 38,
        mwin_codeDigit0 = 39,
        mwin_codeEnter = 40,
        mwin_codeEscape = 41,
        mwin_codeBackspace = 42,
        mwin_codeTab = 43,
        mwin_codeSpace = 44,
        mwin_codeMinus = 45,
        mwin_codeEqual = 46,
        mwin_codeBracketLeft = 47,
        mwin_codeBracketRight = 48,
        mwin_codeBackslash = 49,
        // The key left of Enter on ISO keyboards, which HID tells apart
        // from Backslash.
        mwin_codeIntlHash = 50,
        mwin_codeSemicolon = 51,
        mwin_codeQuote = 52,
        mwin_codeBackquote = 53,
        mwin_codeComma = 54,
        mwin_codePeriod = 55,
        mwin_codeSlash = 56,
        mwin_codeCapsLock = 57,
        mwin_codeF1 = 58,
        mwin_codeF2 = 59,
        mwin_codeF3 = 60,
        mwin_codeF4 = 61,
        mwin_codeF5 = 62,
        mwin_codeF6 = 63,
        mwin_codeF7 = 64,
        mwin_codeF8 = 65,
        mwin_codeF9 = 66,
        mwin_codeF10 = 67,
        mwin_codeF11 = 68,
        mwin_codeF12 = 69,
        mwin_codePrintScreen = 70,
        mwin_codeScrollLock = 71,
        mwin_codePause = 72,
        mwin_codeInsert = 73,
        mwin_codeHome = 74,
        mwin_codePageUp = 75,
        mwin_codeDelete = 76,
        mwin_codeEnd = 77,
        mwin_codePageDown = 78,
        mwin_codeArrowRight = 79,
        mwin_codeArrowLeft = 80,
        mwin_codeArrowDown = 81,
        mwin_codeArrowUp = 82,
        mwin_codeNumLock = 83,
        mwin_codeNumpadDivide = 84,
        mwin_codeNumpadMultiply = 85,
        mwin_codeNumpadSubtract = 86,
        mwin_codeNumpadAdd = 87,
        mwin_codeNumpadEnter = 88,
        mwin_codeNumpad1 = 89,
        mwin_codeNumpad2 = 90,
        mwin_codeNumpad3 = 91,
        mwin_codeNumpad4 = 92,
        mwin_codeNumpad5 = 93,
        mwin_codeNumpad6 = 94,
        mwin_codeNumpad7 = 95,
        mwin_codeNumpad8 = 96,
        mwin_codeNumpad9 = 97,
        mwin_codeNumpad0 = 98,
        mwin_codeNumpadDecimal = 99,
        // The extra key right of left Shift on ISO keyboards.
        mwin_codeIntlBackslash = 100,
        mwin_codeContextMenu = 101,
        mwin_codeNumpadEqual = 103,
        mwin_codeF13 = 104,
        mwin_codeF14 = 105,
        mwin_codeF15 = 106,
        mwin_codeF16 = 107,
        mwin_codeF17 = 108,
        mwin_codeF18 = 109,
        mwin_codeF19 = 110,
        mwin_codeF20 = 111,
        mwin_codeF21 = 112,
        mwin_codeF22 = 113,
        mwin_codeF23 = 114,
        mwin_codeF24 = 115,
        mwin_codeNumpadComma = 133,
        mwin_codeIntlRo = 135,
        mwin_codeKanaMode = 136,
        mwin_codeIntlYen = 137,
        mwin_codeConvert = 138,
        mwin_codeNonConvert = 139,
        mwin_codeLang1 = 144,
        mwin_codeLang2 = 145,
        mwin_codeControlLeft = 224,
        mwin_codeShiftLeft = 225,
        mwin_codeAltLeft = 226,
        mwin_codeMetaLeft = 227,
        mwin_codeControlRight = 228,
        mwin_codeShiftRight = 229,
        mwin_codeAltRight = 230,
        mwin_codeMetaRight = 231,
    };

    // What a key means under the current layout: the Unicode code point it
    // types without modifiers (a lower-case letter for a letter key), or,
    // for a key that types nothing, MWIN_KEY_NAMED with its code in the low
    // bits; 0 when the layout gives it no meaning.
    typedef uint32_t mwinKey;

#define MWIN_KEY_NAMED 0x40000000u

    // The modifier keys held, and the lock keys on.
    typedef uint16_t mwinModifiers;

    enum
    {
        mwin_modShift = 1,
        mwin_modControl = 2,
        mwin_modAlt = 4,
        // The Windows, Command or Super key.
        mwin_modMeta = 8,
        mwin_modCapsLock = 16,
        mwin_modNumLock = 32,
    };

    // Whether a chord, a key with modifiers, reaches the program
    // (mwinGetKeyReach).
    typedef uint8_t mwinKeyReach;

    enum
    {
        // Its records arrive and nothing else acts on it, as far as the
        // library knows.
        mwin_keyReachDelivered = 0,
        // Its records arrive and the platform acts on it too: a browser's
        // shortcut, Alt+F4 closing a window on Windows.
        mwin_keyReachShared = 1,
        // The desktop's own shortcuts, which the user may change, may take
        // it: no record arrives when they do.
        mwin_keyReachUncertain = 2,
        // The platform always takes it: no record ever arrives.
        mwin_keyReachNever = 3,
    };

    // A mouse button.
    typedef uint8_t mwinMouseButton;

    enum
    {
        mwin_buttonLeft = 1,
        mwin_buttonRight = 2,
        mwin_buttonMiddle = 3,
        mwin_buttonBack = 4,
        mwin_buttonForward = 5,
    };

    // How the cursor behaves over a window.
    typedef uint8_t mwinCursorMode;

    enum
    {
        mwin_cursorVisible = 0,
        mwin_cursorHidden = 1,
        // Locked in place and hidden; raw deltas keep coming.
        mwin_cursorCaptured = 2,
        // Kept inside the window.
        mwin_cursorConfined = 3,
        mwin_cursorConfinedHidden = 4,
    };

    // The system's cursor images.
    typedef uint8_t mwinCursorShape;

    enum
    {
        mwin_shapeDefault = 0,
        mwin_shapeText = 1,
        mwin_shapePointer = 2,
        mwin_shapeCrosshair = 3,
        mwin_shapeMove = 4,
        mwin_shapeResizeEastWest = 5,
        mwin_shapeResizeNorthSouth = 6,
        mwin_shapeResizeNortheastSouthwest = 7,
        mwin_shapeResizeNorthwestSoutheast = 8,
        mwin_shapeNotAllowed = 9,
        mwin_shapeWait = 10,
        mwin_shapeProgress = 11,
    };

// The images one cursor holds, and the most pixels of a side.
#define MWIN_CURSOR_IMAGES 4
#define MWIN_CURSOR_SIZE   128

    // A cursor made from images (mwin-0027): an id of the context's,
    // checked by generation.
    typedef struct mwinCursorId
    {
        uint32_t index1;
        uint32_t generation;
    } mwinCursorId;

    // A cursor's images. Build it with mwinDefaultCursorDef.
    typedef struct mwinCursorDef
    {
        uint32_t cookie;
        // The cursor at scale 1 first, then the same cursor at higher
        // scales, larger; each in the icon's form, at most
        // MWIN_CURSOR_SIZE a side. Only read during the call.
        const mwinIconImage* images;
        uint32_t imageCount;
        // The hotspot, in the first image's pixels from its top left.
        uint32_t hotspotX;
        uint32_t hotspotY;
    } mwinCursorDef;

    // What a text field takes, which picks an on-screen keyboard's layout.
    typedef uint8_t mwinInputPurpose;

    enum
    {
        mwin_purposeText = 0,
        mwin_purposeNumber = 1,
        mwin_purposeEmail = 2,
        mwin_purposePassword = 3,
        mwin_purposeUrl = 4,
    };

    /// Asks to show or hide the on-screen keyboard over a window, where
    /// the platform has one; others answer mwin_outcomeUnsupported.
    /// mwin_eventVirtualKeyboardChanged reports the part it covers.
    ///
    /// @param context     The context.
    /// @param window      The window.
    /// @param visible     true to show it, false to hide it.
    /// @param purpose     What the text field takes.
    /// @param requestOut  Receives the request's id. May be NULL.
    /// @return As mwinRequestTitle, with `mwin_errorInvalid` for an
    ///         unknown purpose.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestVirtualKeyboard(mwinContext* context,
                                                                  mwinWindowId window, bool visible,
                                                                  mwinInputPurpose purpose,
                                                                  mwinRequestId* requestOut);

    /// Asks a window to accept text, or to stop: while it does, input
    /// methods compose into it and mwin_eventImePreedit reports their
    /// compositions, and the caret rectangle tells the platform where to
    /// place the candidate window. Ask again to move the caret.
    ///
    /// @param context     The context.
    /// @param window      The window.
    /// @param enabled     true to accept text, false to stop.
    /// @param caret       Where the caret is, in the window's logical units.
    /// @param requestOut  Receives the request's id. May be NULL.
    /// @return As mwinRequestTitle, with `mwin_errorInvalid` for a caret
    ///         that is not finite or has a negative size.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestTextInput(mwinContext* context,
                                                            mwinWindowId window, bool enabled,
                                                            mwinRect caret,
                                                            mwinRequestId* requestOut);

    /// Asks for a cursor mode over a window.
    ///
    /// @param context     The context.
    /// @param window      The window.
    /// @param mode        One of the mwin_cursor values.
    /// @param requestOut  Receives the request's id. May be NULL.
    /// @return As mwinRequestTitle, with `mwin_errorInvalid` for an
    ///         unknown mode.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestCursorMode(mwinContext* context,
                                                             mwinWindowId window,
                                                             mwinCursorMode mode,
                                                             mwinRequestId* requestOut);

    /// Asks for one of the system's cursor images over a window.
    ///
    /// @param context     The context.
    /// @param window      The window.
    /// @param shape       One of the mwin_shape values.
    /// @param requestOut  Receives the request's id. May be NULL.
    /// @return As mwinRequestTitle, with `mwin_errorInvalid` for an
    ///         unknown shape.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestCursorShape(mwinContext* context,
                                                              mwinWindowId window,
                                                              mwinCursorShape shape,
                                                              mwinRequestId* requestOut);

    /// Returns the default cursor def: no images, the hotspot at 0, 0.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MWIN_API mwinCursorDef mwinDefaultCursorDef(void);

    /// Makes a cursor from images, to show over windows with
    /// mwinRequestCursorImage. Each window takes the image for its scale:
    /// the smallest at least the first image's size times the scale, else
    /// the largest, its hotspot scaled with it.
    ///
    /// @param context    The context.
    /// @param def        The images and hotspot.
    /// @param cursorOut  Receives the cursor's id.
    /// @return `mwin_success`; `mwin_errorInvalid` for a NULL argument, a
    ///         def without its cookie, no images or more than
    ///         MWIN_CURSOR_IMAGES, an image without pixels, with no width
    ///         or height or more than MWIN_CURSOR_SIZE, or a stride below
    ///         its width times 4, an image not wider than the one before,
    ///         or a hotspot outside the first image;
    ///         `mwin_errorCapacity` past the limit's cursors or when the
    ///         allocator fails.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinCreateCursor(mwinContext* context,
                                                        const mwinCursorDef* def,
                                                        mwinCursorId* cursorOut);

    /// Destroys a cursor. Windows showing it show the default shape.
    ///
    /// @param context  The context.
    /// @param cursor   The cursor.
    /// @return `mwin_success`; `mwin_errorInvalid` for a NULL context;
    ///         `mwin_errorStale` for an id that is not live.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinDestroyCursor(mwinContext* context, mwinCursorId cursor);

    /// Asks for a cursor made with mwinCreateCursor over a window, in place
    /// of a shape until a shape is asked for again. Platforms without image
    /// cursors (iOS) answer mwin_outcomeUnsupported.
    ///
    /// @param context     The context.
    /// @param window      The window.
    /// @param cursor      The cursor.
    /// @param requestOut  Receives the request's id. May be NULL.
    /// @return As mwinRequestTitle, with `mwin_errorStale` for a cursor
    ///         that is not live.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestCursorImage(mwinContext* context,
                                                              mwinWindowId window,
                                                              mwinCursorId cursor,
                                                              mwinRequestId* requestOut);

    /// Returns what a physical key means under the current keyboard
    /// layout, as a key record would carry it.
    ///
    /// @param context  The context.
    /// @param code     A physical key.
    /// @return The key; 0 for a NULL context, an unknown code or a key
    ///         the layout gives no meaning.
    /// @par Thread safety
    /// Main thread only.
    MWIN_API mwinKey mwinMapKeyCode(const mwinContext* context, mwinKeyCode code);

    /// Reads the name of the current keyboard layout, as the platform
    /// gives it, for showing which layout key labels come from.
    ///
    /// @param context    The context.
    /// @param buffer     Receives the name in UTF-8, not NUL-terminated.
    ///                   May be NULL when capacity is 0.
    /// @param capacity   The bytes buffer holds.
    /// @param lengthOut  Receives the name's length in bytes.
    /// @return `mwin_success`; `mwin_errorCapacity` when the name does not
    ///         fit (the bytes that fit are written); `mwin_errorInvalid`
    ///         for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinGetKeyboardLayout(const mwinContext* context,
                                                             char* buffer, size_t capacity,
                                                             size_t* lengthOut);

    /// Tells whether a chord reaches the program on the platform the
    /// context runs on, for a rebinding UI to refuse a chord that never
    /// arrives and warn about one the platform may take: Ctrl+W in
    /// Chromium, Alt+Tab on Windows and Command+Tab on Apple's systems
    /// never arrive; the desktop's configurable shortcuts are uncertain.
    /// The answer is the library's knowledge of the platform, not a
    /// promise: a desktop may keep chords nothing lists.
    ///
    /// @param context    The context.
    /// @param code       The key.
    /// @param modifiers  The modifiers held with it; the lock bits are
    ///                   ignored.
    /// @param reachOut   Receives the answer.
    /// @return `mwin_success`; `mwin_errorInvalid` for a NULL argument or
    ///         a code that is not a key.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinGetKeyReach(const mwinContext* context, mwinKeyCode code,
                                                       mwinModifiers modifiers,
                                                       mwinKeyReach* reachOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_WINDOW_INPUT_H
