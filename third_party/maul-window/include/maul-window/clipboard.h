// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The clipboard, as UTF-8 text. Writing and reading are requests of a
// window, answered by a completion like any other: the browser's
// clipboard answers later and may ask the user first, and on Wayland
// only a focused window may use it. A later request of the same kind
// on the same window supersedes a waiting one, so two reads in one
// frame paste once unless the program counts the superseded read.
//
// A read that completes with mwin_outcomeDone leaves the text it found
// in the context, where mwinGetClipboardText copies it out until the
// next read is done; a read refused or too large leaves it. Text another program put there is
// checked before the program sees it: ill-formed UTF-8 has each maximal ill-formed subpart replaced
// with U+FFFD, and text past the clipboardBytes limit, after that, completes the read with
// mwin_outcomeTooLarge. An empty clipboard, or one without text, reads as empty text.
//
// What the program wrote stays in the context while the platform may
// ask for it, until the next write or the context's end.

#ifndef MAUL_WINDOW_CLIPBOARD_H
#define MAUL_WINDOW_CLIPBOARD_H

#include "maul-window/window.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// Asks to put text on the clipboard.
    ///
    /// @param context    The context.
    /// @param window     The window asking, which should have focus.
    /// @param text       UTF-8, not NUL-terminated; copied before the call
    ///                   returns. May be NULL when length is 0.
    /// @param length     Its bytes.
    /// @param requestOut Receives the request's id. May be NULL.
    /// @return `mwin_success`; `mwin_errorCapacity` for text past the
    ///         clipboardBytes limit, when the context cannot hold it, or
    ///         when the window has its limit of requests in flight;
    ///         `mwin_errorStale` for a window that no longer exists;
    ///         `mwin_errorInvalid` for a NULL context or text that is not
    ///         UTF-8.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestClipboardWrite(mwinContext* context,
                                                                 mwinWindowId window,
                                                                 const char* text, size_t length,
                                                                 mwinRequestId* requestOut);

    /// Asks for the clipboard's text; when the request completes with
    /// mwin_outcomeDone, mwinGetClipboardText has it.
    ///
    /// @param context    The context.
    /// @param window     The window asking, which should have focus.
    /// @param requestOut Receives the request's id. May be NULL.
    /// @return `mwin_success`; `mwin_errorCapacity` when the window has its
    ///         limit of requests in flight; `mwin_errorStale` for a window
    ///         that no longer exists; `mwin_errorInvalid` for a NULL
    ///         context.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestClipboardRead(mwinContext* context,
                                                                mwinWindowId window,
                                                                mwinRequestId* requestOut);

    /// Copies out the text found by the last clipboard read that completed
    /// with mwin_outcomeDone; empty before any.
    ///
    /// @param context    The context.
    /// @param buffer     Receives the text in UTF-8, not NUL-terminated.
    ///                   May be NULL when capacity is 0.
    /// @param capacity   The bytes buffer holds.
    /// @param lengthOut  Receives the text's length in bytes.
    /// @return `mwin_success`; `mwin_errorCapacity` when the text does not
    ///         fit (the bytes that fit are written); `mwin_errorInvalid`
    ///         for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinGetClipboardText(const mwinContext* context,
                                                            char* buffer, size_t capacity,
                                                            size_t* lengthOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_WINDOW_CLIPBOARD_H
