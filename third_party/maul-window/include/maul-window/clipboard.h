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
//
// Beside text, the clipboard takes data by MIME type, its bytes passed
// through as they are (mwin-0029): an image as image/png the program
// encodes itself. A write offers up to MWIN_CLIPBOARD_ITEMS items, one
// of which may be text/plain, offered as the platform's text and read
// with the text calls; a text write replaces the data, a data write the
// text. X11 and Wayland also keep the primary selection, the text last
// selected, apart from the clipboard: mwinRequestPrimaryWrite and
// mwinRequestPrimaryRead, unsupported elsewhere.

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

#define MWIN_CLIPBOARD_ITEMS 4
#define MWIN_CLIPBOARD_MIME  63

    // An item of a clipboard data write: a MIME type and its bytes.
    typedef struct mwinClipboardItem
    {
        // ASCII from 1 to MWIN_CLIPBOARD_MIME bytes, a type and a subtype
        // ("image/png"), not NUL-terminated.
        const char* mime;
        size_t mimeLength;
        // May be NULL when length is 0. A text/plain item's are UTF-8.
        const void* bytes;
        size_t length;
    } mwinClipboardItem;

    /// Asks to put data on the clipboard, by MIME type, copied before the
    /// call returns.
    ///
    /// @param context     The context.
    /// @param window      The window asking, which should have focus.
    /// @param items       The items, each of a different type.
    /// @param count       Their number, 1 to MWIN_CLIPBOARD_ITEMS.
    /// @param requestOut  Receives the request's id. May be NULL.
    /// @return `mwin_success`; `mwin_errorCapacity` for bytes past the
    ///         clipboardBytes limit in all, when the context cannot hold
    ///         them, or when the window has its limit of requests in
    ///         flight; `mwin_errorStale` for a window that no longer
    ///         exists; `mwin_errorInvalid` for a NULL context or items, a
    ///         count outside 1 to MWIN_CLIPBOARD_ITEMS, a type that is not
    ///         a MIME type or comes twice, or a text/plain item that is not
    ///         UTF-8.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestClipboardWriteData(mwinContext* context,
                                                                     mwinWindowId window,
                                                                     const mwinClipboardItem* items,
                                                                     size_t count,
                                                                     mwinRequestId* requestOut);

    /// Asks for the clipboard's data of a MIME type; when the request
    /// completes with mwin_outcomeDone, mwinGetClipboardData has it. A
    /// clipboard without that type completes it with mwin_outcomeFailed;
    /// data past the clipboardBytes limit with mwin_outcomeTooLarge.
    ///
    /// @param context     The context.
    /// @param window      The window asking, which should have focus.
    /// @param mime        The type, as in mwinClipboardItem; not
    ///                    text/plain, which mwinRequestClipboardRead reads.
    /// @param mimeLength  Its bytes.
    /// @param requestOut  Receives the request's id. May be NULL.
    /// @return As mwinRequestClipboardRead, with `mwin_errorInvalid` for a
    ///         type that is not a MIME type, or text/plain.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestClipboardReadData(mwinContext* context,
                                                                    mwinWindowId window,
                                                                    const char* mime,
                                                                    size_t mimeLength,
                                                                    mwinRequestId* requestOut);

    /// Copies out the data found by the last data read that completed with
    /// mwin_outcomeDone; empty before any.
    ///
    /// @param context    The context.
    /// @param buffer     Receives the bytes. May be NULL when capacity is 0.
    /// @param capacity   The bytes buffer holds.
    /// @param lengthOut  Receives the data's length in bytes.
    /// @return As mwinGetClipboardText.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinGetClipboardData(const mwinContext* context,
                                                            void* buffer, size_t capacity,
                                                            size_t* lengthOut);

    /// Asks to make text the primary selection, where the platform has one
    /// (X11, Wayland); others answer mwin_outcomeUnsupported.
    ///
    /// @param context     The context.
    /// @param window      The window asking, which should have focus.
    /// @param text        UTF-8, not NUL-terminated; copied before the
    ///                    call returns. May be NULL when length is 0.
    /// @param length      Its bytes.
    /// @param requestOut  Receives the request's id. May be NULL.
    /// @return As mwinRequestClipboardWrite.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestPrimaryWrite(mwinContext* context,
                                                               mwinWindowId window,
                                                               const char* text, size_t length,
                                                               mwinRequestId* requestOut);

    /// Asks for the primary selection's text; when the request completes
    /// with mwin_outcomeDone, mwinGetPrimaryText has it, checked as the
    /// clipboard's is.
    ///
    /// @param context     The context.
    /// @param window      The window asking, which should have focus.
    /// @param requestOut  Receives the request's id. May be NULL.
    /// @return As mwinRequestClipboardRead.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestPrimaryRead(mwinContext* context,
                                                              mwinWindowId window,
                                                              mwinRequestId* requestOut);

    /// Copies out the text found by the last primary selection read that
    /// completed with mwin_outcomeDone; empty before any.
    ///
    /// @param context    The context.
    /// @param buffer     Receives the text in UTF-8, not NUL-terminated.
    ///                   May be NULL when capacity is 0.
    /// @param capacity   The bytes buffer holds.
    /// @param lengthOut  Receives the text's length in bytes.
    /// @return As mwinGetClipboardText.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinGetPrimaryText(const mwinContext* context, char* buffer,
                                                          size_t capacity, size_t* lengthOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_WINDOW_CLIPBOARD_H
