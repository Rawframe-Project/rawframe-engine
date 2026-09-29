// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Windows. A window is created at once as an id; the platform makes it
// real later and says so with mwin_eventWindowCreated. Every change to a
// window is a request (family record 0018): the call returns a request
// id, and exactly one mwin_eventRequestCompleted answers it, after the
// notifications the change caused. A later request of the same kind on
// the same window supersedes an earlier one still in flight.
//
// Nothing closes a window by itself. The platform's close button sends
// mwin_eventCloseRequested; the program destroys the window, or does not.
//
// Sizes come in three kinds: the logical size (the unit the program lays
// out in), the size in pixels, and the scale between them. Each has its
// own notification.

#ifndef MAUL_WINDOW_WINDOW_H
#define MAUL_WINDOW_WINDOW_H

#include "maul-window/context.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // A size in logical units.
    typedef struct mwinSize
    {
        float width;
        float height;
    } mwinSize;

    // A size in pixels.
    typedef struct mwinPixelSize
    {
        uint32_t width;
        uint32_t height;
    } mwinPixelSize;

    // A position in logical units on the desktop.
    typedef struct mwinPosition
    {
        float x;
        float y;
    } mwinPosition;

    // Distances in logical units from each edge of the window's area.
    typedef struct mwinInsets
    {
        float top;
        float right;
        float bottom;
        float left;
    } mwinInsets;

    // A rectangle in the window's logical units.
    typedef struct mwinRect
    {
        float x;
        float y;
        float width;
        float height;
    } mwinRect;

    // How a window occupies the screen. There is no exclusive fullscreen.
    typedef uint8_t mwinWindowMode;

    enum
    {
        mwin_modeWindowed = 0,
        // The whole monitor, without decorations, as a normal window.
        mwin_modeBorderlessFullscreen = 1,
        mwin_modeMinimized = 2,
        mwin_modeMaximized = 3,
    };

    // How a window looks and behaves beyond its size and mode.
    typedef uint8_t mwinWindowStyle;

    enum
    {
        mwin_styleResizable = 1,
        mwin_styleDecorated = 2,
        mwin_styleAlwaysOnTop = 4,
        // The program draws the window's frame: the platform draws no
        // title bar or border, over mwin_styleDecorated, but keeps what
        // its frame does (shadows, snapping, animations) where it can.
        // Hit regions say where the caption and edges are.
        mwin_styleCustomChrome = 8,
    };

    // What a window is to the others.
    typedef uint8_t mwinWindowKind;

    enum
    {
        // A window of its own; with an owner, a dialog of it, kept above
        // it and gone with it.
        mwin_windowNormal = 0,
        // A popup menu torn out of its owner: undecorated, placed against
        // its owner, taking the keyboard.
        mwin_windowMenu = 1,
        // A tooltip: as a menu, but never taking the keyboard.
        mwin_windowTooltip = 2,
    };

#define MWIN_CANVAS_SELECTOR_BYTES 256

    // How a window is made. Build it with mwinDefaultWindowDef.
    typedef struct mwinWindowDef
    {
        uint32_t cookie;
        // UTF-8, at most the context's titleBytes. May be NULL when
        // titleLength is 0.
        const char* title;
        size_t titleLength;
        // Logical size of the area the program draws in.
        mwinSize size;
        mwinWindowMode mode;
        bool visible;
        mwinWindowStyle style;
        // The web: the CSS selector of a canvas on the page to use, UTF-8,
        // at most MWIN_CANVAS_SELECTOR_BYTES; empty for a canvas the
        // library makes. Only read while the window is created; other
        // platforms ignore it.
        const char* canvas;
        size_t canvasLength;
        // The window this one belongs to, or the null id for none. An
        // owned window stays above its owner and is destroyed with it; a
        // popup needs one.
        mwinWindowId owner;
        mwinWindowKind kind;
        // Where a popup's top left goes, in logical units from the top
        // left of its owner's client area; its state's position is the
        // same. Other windows ignore it.
        mwinPosition position;
    } mwinWindowDef;

    // What a window is, as far as the program has been told: the values
    // of the notifications delivered so far.
    typedef struct mwinWindowState
    {
        mwinSize size;
        mwinPixelSize pixelSize;
        float scale;
        mwinPosition position;
        mwinWindowMode mode;
        // The platform has made the window (mwin_eventWindowCreated).
        bool created;
        bool visible;
        bool focused;
        bool occluded;
        // Between mwin_eventSurfaceLost and mwin_eventSurfaceRestored.
        bool surfaceLost;
        // The monitor that shows most of the window, null before the
        // platform says.
        mwinMonitorId monitor;
        // What notches, rounded corners and overscan keep from view.
        mwinInsets safeArea;
        // The part of the window an on-screen keyboard covers, empty while
        // none shows.
        mwinRect virtualKeyboard;
        // The window accepts text (mwinRequestTextInput answered done).
        bool textInput;
        // An input method composes (a preedit that is not empty).
        bool composing;
        // The window keeps the display awake (mwinRequestKeepAwake answered
        // done).
        bool awake;
        mwinWindowStyle style;
        // From 0 (clear) to 1 (opaque).
        float opacity;
        // Counts the surfaces the window has had: one more at creation and
        // at each mwin_eventSurfaceRestored.
        uint32_t surfaceGeneration;
    } mwinWindowState;

    /// Returns the default window def: 1,280 by 720 logical units,
    /// windowed, visible, resizable and decorated, not always on top, with
    /// no title.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MWIN_API mwinWindowDef mwinDefaultWindowDef(void);

    /// Creates a window. Its id is valid at once; mwin_eventWindowCreated
    /// follows when the platform has made it, then the completion of the
    /// request.
    ///
    /// @param context    The context.
    /// @param def        The window: a valid cookie, a positive size, a
    ///                   UTF-8 title within the titleBytes limit, a UTF-8
    ///                   canvas selector within
    ///                   MWIN_CANVAS_SELECTOR_BYTES; a popup windowed, with
    ///                   an owner and a finite position.
    /// @param windowOut  Receives the window's id.
    /// @param requestOut Receives the id of the creation request. May be
    ///                   NULL.
    /// @return `mwin_success`; `mwin_errorCapacity` when the context has
    ///         its limit of windows; `mwin_errorStale` for an owner that no
    ///         longer exists; `mwin_errorInvalid` for a NULL argument or an
    ///         invalid def.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinCreateWindow(mwinContext* context,
                                                        const mwinWindowDef* def,
                                                        mwinWindowId* windowOut,
                                                        mwinRequestId* requestOut);

    /// Destroys a window at once: its id becomes stale, its requests in
    /// flight complete as cancelled, and mwin_eventWindowDestroyed follows.
    /// Notifications of the window not yet drained are dropped. The
    /// windows it owns are destroyed first, theirs before them.
    ///
    /// @param context  The context.
    /// @param window   The window.
    /// @return `mwin_success`; `mwin_errorStale` for a window that no
    ///         longer exists; `mwin_errorInvalid` for a NULL context.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinDestroyWindow(mwinContext* context, mwinWindowId window);

    /// Reads what the program has been told about a window.
    ///
    /// @param context   The context.
    /// @param window    The window.
    /// @param stateOut  Receives the state.
    /// @return `mwin_success`; `mwin_errorStale` for a window that no
    ///         longer exists; `mwin_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinGetWindowState(const mwinContext* context,
                                                          mwinWindowId window,
                                                          mwinWindowState* stateOut);

    /// Asks for a new title.
    ///
    /// @param context     The context.
    /// @param window      The window.
    /// @param title       UTF-8. May be NULL when length is 0.
    /// @param length      The number of bytes, at most the titleBytes limit.
    /// @param requestOut  Receives the request's id. May be NULL.
    /// @return `mwin_success`; `mwin_errorStale` for a window that no
    ///         longer exists; `mwin_errorCapacity` when the window has its
    ///         limit of requests in flight or the title is too long;
    ///         `mwin_errorInvalid` for a NULL context or a title that is
    ///         not UTF-8.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestTitle(mwinContext* context, mwinWindowId window,
                                                        const char* title, size_t length,
                                                        mwinRequestId* requestOut);

    /// Asks for a new logical size of the area the program draws in.
    /// mwin_eventResized and mwin_eventPixelSizeChanged report what the
    /// platform chose, which may differ.
    ///
    /// @param context     The context.
    /// @param window      The window.
    /// @param size        A positive, finite size.
    /// @param requestOut  Receives the request's id. May be NULL.
    /// @return As mwinRequestTitle, with `mwin_errorInvalid` for a size
    ///         that is not positive and finite.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestSize(mwinContext* context, mwinWindowId window,
                                                       mwinSize size, mwinRequestId* requestOut);

    /// Asks to move a window. Wayland does not let programs place windows,
    /// and answers mwin_outcomeUnsupported, except popups. A popup's
    /// position is from the top left of its owner's client area.
    ///
    /// @param context     The context.
    /// @param window      The window.
    /// @param position    A finite position.
    /// @param requestOut  Receives the request's id. May be NULL.
    /// @return As mwinRequestTitle, with `mwin_errorInvalid` for a
    ///         position that is not finite.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestPosition(mwinContext* context,
                                                           mwinWindowId window,
                                                           mwinPosition position,
                                                           mwinRequestId* requestOut);

    /// Asks for a mode: windowed, borderless fullscreen, minimized or
    /// maximized. A popup, always windowed, answers
    /// mwin_outcomeUnsupported.
    ///
    /// @param context     The context.
    /// @param window      The window.
    /// @param mode        One of the mwin_mode values.
    /// @param requestOut  Receives the request's id. May be NULL.
    /// @return As mwinRequestTitle, with `mwin_errorInvalid` for an
    ///         unknown mode.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestMode(mwinContext* context, mwinWindowId window,
                                                       mwinWindowMode mode,
                                                       mwinRequestId* requestOut);

    /// Asks to show or hide a window.
    ///
    /// @param context     The context.
    /// @param window      The window.
    /// @param visible     true to show it, false to hide it.
    /// @param requestOut  Receives the request's id. May be NULL.
    /// @return As mwinRequestTitle.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestVisible(mwinContext* context, mwinWindowId window,
                                                          bool visible, mwinRequestId* requestOut);

    /// Asks for limits on the logical size the user can give the window;
    /// a zero width or height leaves that side free.
    ///
    /// @param context     The context.
    /// @param window      The window.
    /// @param minimum     The smallest size, zero or positive and finite.
    /// @param maximum     The largest size, zero or at least minimum.
    /// @param requestOut  Receives the request's id. May be NULL.
    /// @return As mwinRequestTitle, with `mwin_errorInvalid` for limits
    ///         that are not finite, negative, or crossed.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestSizeLimits(mwinContext* context,
                                                             mwinWindowId window, mwinSize minimum,
                                                             mwinSize maximum,
                                                             mwinRequestId* requestOut);

    /// Asks the platform to keep the window's width to height at a ratio
    /// while the user resizes it; 0 by 0 lifts the constraint.
    ///
    /// @param context     The context.
    /// @param window      The window.
    /// @param width       The ratio's width.
    /// @param height      The ratio's height; both zero or both positive.
    /// @param requestOut  Receives the request's id. May be NULL.
    /// @return As mwinRequestTitle, with `mwin_errorInvalid` for a ratio
    ///         with one side zero.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestAspectRatio(mwinContext* context,
                                                              mwinWindowId window, uint32_t width,
                                                              uint32_t height,
                                                              mwinRequestId* requestOut);

    /// Asks for a style: resizable, decorated, always on top. A popup,
    /// undecorated and above its owner, answers mwin_outcomeUnsupported.
    ///
    /// @param context     The context.
    /// @param window      The window.
    /// @param style       mwin_style flags.
    /// @param requestOut  Receives the request's id. May be NULL.
    /// @return As mwinRequestTitle, with `mwin_errorInvalid` for unknown
    ///         flags.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestStyle(mwinContext* context, mwinWindowId window,
                                                        mwinWindowStyle style,
                                                        mwinRequestId* requestOut);

    /// Asks for the window's opacity, where the platform can blend windows;
    /// others answer mwin_outcomeUnsupported.
    ///
    /// @param context     The context.
    /// @param window      The window.
    /// @param opacity     From 0 to 1.
    /// @param requestOut  Receives the request's id. May be NULL.
    /// @return As mwinRequestTitle, with `mwin_errorInvalid` for an
    ///         opacity outside 0 to 1.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestOpacity(mwinContext* context, mwinWindowId window,
                                                          float opacity, mwinRequestId* requestOut);

// The most images of a window's icon, and the most pixels on a side.
#define MWIN_ICON_IMAGES 4
#define MWIN_ICON_SIZE   256

    // An image of an icon: RGBA with straight alpha, 8 bits a channel,
    // rows from the top.
    typedef struct mwinIconImage
    {
        uint32_t width;
        uint32_t height;
        // Bytes from a row's start to the next's: at least width * 4.
        size_t stride;
        const uint8_t* pixels;
    } mwinIconImage;

// The most hit regions of a window.
#define MWIN_HIT_REGIONS 16

    // What a part of a window is to the platform.
    typedef uint8_t mwinHitKind;

    enum
    {
        // The program's: the pointer's records go to it.
        mwin_hitClient = 0,
        // Dragging moves the window; a double click maximizes or restores
        // it.
        mwin_hitCaption = 1,
        // Dragging resizes the window from an edge or a corner.
        mwin_hitLeft = 2,
        mwin_hitRight = 3,
        mwin_hitTop = 4,
        mwin_hitBottom = 5,
        mwin_hitTopLeft = 6,
        mwin_hitTopRight = 7,
        mwin_hitBottomLeft = 8,
        mwin_hitBottomRight = 9,
        // The program's buttons, whose pointer records go to it as the
        // client's do. Windows 11 shows its snap layouts over a maximize
        // button (mwinSystemFacts.snapLayouts).
        mwin_hitMinimize = 10,
        mwin_hitMaximize = 11,
        mwin_hitClose = 12,
    };

    // A part of a window's client area, in logical units from its top
    // left.
    typedef struct mwinHitRegion
    {
        mwinRect rect;
        mwinHitKind kind;
    } mwinHitRegion;

    /// Tells the platform what the parts of the window's client area are,
    /// replacing what it was told before: a later region over an earlier
    /// one, and the client everywhere else. A press on a caption or an
    /// edge moves or resizes the window through the platform, and is not
    /// reported. The regions are copied at the call. The web answers
    /// mwin_outcomeUnsupported.
    ///
    /// @param context     The context.
    /// @param window      The window.
    /// @param regions     The regions. May be NULL when count is 0.
    /// @param count       How many, at most MWIN_HIT_REGIONS; 0 for none.
    /// @param requestOut  Receives the request's id. May be NULL.
    /// @return As mwinRequestTitle, with `mwin_errorInvalid` for more
    ///         regions than MWIN_HIT_REGIONS, a region not finite, of a
    ///         negative size or of an unknown kind.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestHitRegions(mwinContext* context,
                                                             mwinWindowId window,
                                                             const mwinHitRegion* regions,
                                                             uint32_t count,
                                                             mwinRequestId* requestOut);

    /// Asks for the window's icon, as its title bar, the taskbar and the
    /// window switcher show it; each platform takes the images nearest
    /// the sizes it shows. The images are copied at the call. The web,
    /// whose page has one icon for every canvas, and Wayland compositors
    /// without toplevel icons answer mwin_outcomeUnsupported.
    ///
    /// @param context     The context.
    /// @param window      The window.
    /// @param images      The images, in any order. May be NULL when count
    ///                    is 0.
    /// @param count       How many, at most MWIN_ICON_IMAGES; 0 for the
    ///                    platform's own icon.
    /// @param requestOut  Receives the request's id. May be NULL.
    /// @return As mwinRequestTitle, with `mwin_errorCapacity` when the
    ///         context cannot hold the copy, and `mwin_errorInvalid` for
    ///         more images than MWIN_ICON_IMAGES, an image without pixels,
    ///         with no width or height or more than MWIN_ICON_SIZE, or a
    ///         stride shorter than its row.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestIcon(mwinContext* context, mwinWindowId window,
                                                       const mwinIconImage* images, uint32_t count,
                                                       mwinRequestId* requestOut);

    /// Asks for keyboard focus. Platforms may refuse to take focus from
    /// another program, and answer mwin_outcomeDenied.
    ///
    /// @param context     The context.
    /// @param window      The window.
    /// @param requestOut  Receives the request's id. May be NULL.
    /// @return As mwinRequestTitle.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestFocus(mwinContext* context, mwinWindowId window,
                                                        mwinRequestId* requestOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_WINDOW_WINDOW_H
