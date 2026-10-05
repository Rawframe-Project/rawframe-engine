// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a game or an editor asks of the system around its windows:
// opening a web or mail address in the user's default program, showing
// a file in the file manager, keeping the display awake while a window
// shows, and a message box for an error before any window, or without
// one.
//
// The first three are requests of a window, answered by a completion
// like any other: the platform may ask the user, or answer later. An
// address is only http, https or mailto, and holds no spaces or
// control characters, since a platform may hand it to another program;
// a path is absolute. Both are copied at the call.

#ifndef MAUL_WINDOW_SERVICES_H
#define MAUL_WINDOW_SERVICES_H

#include "maul-window/window.h"

#ifdef __cplusplus
extern "C"
{
#endif

// The most bytes of an address or a path.
#define MWIN_ADDRESS_BYTES 4096

// The most bytes of a message box's title, and of its message.
#define MWIN_MESSAGE_TITLE_BYTES 512
#define MWIN_MESSAGE_BYTES       8192

    /// Asks the system to open a web or mail address in the user's
    /// default program.
    ///
    /// @param context    The context.
    /// @param window     The window asking.
    /// @param url        UTF-8, not NUL-terminated: http://, https:// or
    ///                   mailto: (in any case), then no spaces or control
    ///                   characters.
    /// @param length     Its bytes, at most MWIN_ADDRESS_BYTES.
    /// @param requestOut Receives the request's id. May be NULL.
    /// @return `mwin_success`; `mwin_errorCapacity` when the context
    ///         cannot hold the copy or the window has its limit of
    ///         requests in flight; `mwin_errorStale` for a window that no
    ///         longer exists; `mwin_errorInvalid` for a NULL context or
    ///         an address this does not open.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestOpenUrl(mwinContext* context, mwinWindowId window,
                                                          const char* url, size_t length,
                                                          mwinRequestId* requestOut);

    /// Asks the file manager to show a file, selected where it can.
    ///
    /// @param context    The context.
    /// @param window     The window asking.
    /// @param path       An absolute path in UTF-8, not NUL-terminated,
    ///                   without NULs.
    /// @param length     Its bytes, at most MWIN_ADDRESS_BYTES.
    /// @param requestOut Receives the request's id. May be NULL.
    /// @return As mwinRequestOpenUrl; `mwin_errorInvalid` also for a path
    ///         that is not absolute.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestRevealFile(mwinContext* context,
                                                             mwinWindowId window, const char* path,
                                                             size_t length,
                                                             mwinRequestId* requestOut);

    /// Asks the system to keep the display awake, with no screensaver or
    /// dimming, while the window shows; or no longer. The window's state
    /// says whether it does (mwinWindowState's awake).
    ///
    /// @param context    The context.
    /// @param window     The window.
    /// @param awake      true to keep the display awake.
    /// @param requestOut Receives the request's id. May be NULL.
    /// @return `mwin_success`; `mwin_errorCapacity` when the window has its
    ///         limit of requests in flight; `mwin_errorStale` for a window
    ///         that no longer exists; `mwin_errorInvalid` for a NULL
    ///         context.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestKeepAwake(mwinContext* context,
                                                            mwinWindowId window, bool awake,
                                                            mwinRequestId* requestOut);

    // How a message box looks.
    typedef uint8_t mwinMessageKind;

    enum
    {
        mwin_messageInfo = 0,
        mwin_messageWarning = 1,
        mwin_messageError = 2,
    };

    // The buttons a message box offers.
    typedef uint8_t mwinMessageButtons;

    enum
    {
        mwin_buttonsOk = 0,
        mwin_buttonsOkCancel = 1,
        mwin_buttonsYesNo = 2,
    };

    // A message box. Build it with mwinDefaultMessageBoxDef.
    typedef struct mwinMessageBoxDef
    {
        uint32_t cookie;
        // UTF-8 without NULs, not NUL-terminated.
        const char* title;
        size_t titleLength;
        const char* message;
        size_t messageLength;
        mwinMessageKind kind;
        mwinMessageButtons buttons;
    } mwinMessageBoxDef;

    /// Returns the default message box def: no title or message, an
    /// error with an OK button.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MWIN_API mwinMessageBoxDef mwinDefaultMessageBoxDef(void);

    /// Shows a message box and waits for the user. It needs no context,
    /// so a program can report an error that stops it from starting. On
    /// Linux it runs zenity or kdialog, whichever is there. On iOS it
    /// needs a scene in the foreground, which the application does not
    /// show yet in the program's init: there it fails. Android has none
    /// a program could wait for: it forbids waiting on the main thread.
    ///
    /// @param def          The message box.
    /// @param acceptedOut  Receives true for OK or Yes, false for Cancel,
    ///                     No or a closed box. May be NULL.
    /// @return `mwin_success`; `mwin_errorUnsupported` where the platform
    ///         has no message box (Android, Linux without zenity or
    ///         kdialog, the test backend alone); `mwin_errorPlatform` when it failed;
    ///         `mwin_errorInvalid` for a NULL or invalid def, or text that
    ///         is not UTF-8, holds a NUL or passes its limit.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinShowMessageBox(const mwinMessageBoxDef* def,
                                                          bool* acceptedOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_WINDOW_SERVICES_H
