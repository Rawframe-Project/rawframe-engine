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
// in the context, where mwinRequest_GetClipboardText copies it out under the
// read's request until another read finds text; a read refused or too
// large leaves it. Asked with any other request, the getter answers
// mwin_errorStale, so a completion is never answered with another
// read's text. Text another program put there is
// checked before the program sees it: ill-formed UTF-8 has each maximal ill-formed subpart replaced
// with U+FFFD, and text past the clipboardBytes limit, after that, completes the read with
// mwin_outcomeTooLarge. An empty clipboard, or one without text, reads as empty text.
//
// What the program wrote stays in the context while the platform may
// ask for it, until the next write or the context's end.
//
// Beside text, the clipboard takes data by MIME type, its bytes passed
// through as they are: an image as image/png the program
// encodes itself. A write offers up to MWIN_MAX_CLIPBOARD_ITEMS items, one
// of which may be text/plain, offered as the platform's text and read
// with the text calls; a text write replaces the data, a data write the
// text. X11 and Wayland also keep the primary selection, the text last
// selected, apart from the clipboard: mwinWindow_RequestPrimaryWrite and
// mwinWindow_RequestPrimaryRead, unsupported elsewhere.

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
    /// @param requestOut Receives the request's id. May be NULL. The null
    ///                   request on failure.
    /// @return `mwin_success`; `mwin_errorCapacity` for text past the
    ///         clipboardBytes limit, when the context cannot hold it, or
    ///         when the window has its limit of requests in flight;
    ///         `mwin_errorStale` for a window that no longer exists;
    ///         `mwin_errorInvalid` for a NULL context or text that is not
    ///         UTF-8.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinWindow_RequestClipboardWrite(mwinContext* context,
                                                                        mwinWindowId window,
                                                                        const char* text,
                                                                        size_t length,
                                                                        mwinRequestId* requestOut);

    /// Asks for the clipboard's text; when the request completes with
    /// mwin_outcomeDone, mwinRequest_GetClipboardText has it.
    ///
    /// @param context    The context.
    /// @param window     The window asking, which should have focus.
    /// @param requestOut Receives the request's id. May be NULL. The null
    ///                   request on failure.
    /// @return `mwin_success`; `mwin_errorCapacity` when the window has its
    ///         limit of requests in flight; `mwin_errorStale` for a window
    ///         that no longer exists; `mwin_errorInvalid` for a NULL
    ///         context.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinWindow_RequestClipboardRead(mwinContext* context,
                                                                       mwinWindowId window,
                                                                       mwinRequestId* requestOut);

    /// Copies out the text a clipboard read found, while it is the text
    /// the last read found: the read completed with mwin_outcomeDone, and
    /// no read since (of any window) found other text.
    ///
    /// @param context    The context.
    /// @param request    The read, from its completion record.
    /// @param bufferOut  Receives the text in UTF-8, not NUL-terminated.
    ///                   May be NULL when capacity is 0. Zeroed over
    ///                   capacity on failure, so a buffer too short holds
    ///                   none of it.
    /// @param capacity   The bytes bufferOut holds.
    /// @param lengthOut  Receives the text's length in bytes. On
    ///                   `mwin_errorCapacity`, the bytes needed; 0 on any
    ///                   other failure.
    /// @return `mwin_success`; `mwin_errorCapacity` when the text does not
    ///         fit; `mwin_errorStale` for a request that is no read
    ///         answered done, or whose text a later read replaced;
    ///         `mwin_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequest_GetClipboardText(const mwinContext* context,
                                                                    mwinRequestId request,
                                                                    char* bufferOut,
                                                                    size_t capacity,
                                                                    size_t* lengthOut);

#define MWIN_MAX_CLIPBOARD_ITEMS 4
#define MWIN_MAX_CLIPBOARD_MIME  63

    // An item of a clipboard data write: a MIME type and its bytes.
    typedef struct mwinClipboardItem
    {
        // ASCII from 1 to MWIN_MAX_CLIPBOARD_MIME bytes, a type and a subtype
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
    /// @param count       Their number, 1 to MWIN_MAX_CLIPBOARD_ITEMS.
    /// @param requestOut  Receives the request's id. May be NULL. The null
    ///                    request on failure.
    /// @return `mwin_success`; `mwin_errorCapacity` for bytes past the
    ///         clipboardBytes limit in all, when the context cannot hold
    ///         them, or when the window has its limit of requests in
    ///         flight; `mwin_errorStale` for a window that no longer
    ///         exists; `mwin_errorLimit` for more than
    ///         MWIN_MAX_CLIPBOARD_ITEMS items or a type longer than
    ///         MWIN_MAX_CLIPBOARD_MIME; `mwin_errorInvalid` for a NULL context
    ///         or items, no items, a type that is not a MIME type or comes
    ///         twice, or a text/plain item that is not UTF-8.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinWindow_RequestClipboardWriteData(
        mwinContext* context, mwinWindowId window, const mwinClipboardItem* items, uint32_t count,
        mwinRequestId* requestOut);

    /// Asks for the clipboard's data of a MIME type; when the request
    /// completes with mwin_outcomeDone, mwinRequest_GetClipboardData has it. A
    /// clipboard without that type completes it with mwin_outcomeFailed;
    /// data past the clipboardBytes limit with mwin_outcomeTooLarge.
    ///
    /// @param context     The context.
    /// @param window      The window asking, which should have focus.
    /// @param mime        The type, as in mwinClipboardItem; not
    ///                    text/plain, which mwinWindow_RequestClipboardRead reads.
    /// @param mimeLength  Its bytes.
    /// @param requestOut  Receives the request's id. May be NULL. The null
    ///                    request on failure.
    /// @return As mwinWindow_RequestClipboardRead, with `mwin_errorLimit` for a
    ///         type longer than MWIN_MAX_CLIPBOARD_MIME, and
    ///         `mwin_errorInvalid` for a type that is not a MIME type, or
    ///         text/plain.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult
    mwinWindow_RequestClipboardReadData(mwinContext* context, mwinWindowId window, const char* mime,
                                        size_t mimeLength, mwinRequestId* requestOut);

    /// Copies out the data a data read found, as mwinRequest_GetClipboardText does
    /// the text.
    ///
    /// @param context    The context.
    /// @param request    The read, from its completion record.
    /// @param bufferOut  Receives the bytes. May be NULL when capacity is
    ///                   0. Zeroed over capacity on failure, so a buffer
    ///                   too short holds none of it.
    /// @param capacity   The bytes bufferOut holds.
    /// @param lengthOut  Receives the data's length in bytes. On
    ///                   `mwin_errorCapacity`, the bytes needed; 0 on any
    ///                   other failure.
    /// @return As mwinRequest_GetClipboardText.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequest_GetClipboardData(const mwinContext* context,
                                                                    mwinRequestId request,
                                                                    void* bufferOut,
                                                                    size_t capacity,
                                                                    size_t* lengthOut);

    /// Asks to make text the primary selection, where the platform has one
    /// (X11, Wayland); others answer mwin_outcomeUnsupported.
    ///
    /// @param context     The context.
    /// @param window      The window asking, which should have focus.
    /// @param text        UTF-8, not NUL-terminated; copied before the
    ///                    call returns. May be NULL when length is 0.
    /// @param length      Its bytes.
    /// @param requestOut  Receives the request's id. May be NULL. The null
    ///                    request on failure.
    /// @return As mwinWindow_RequestClipboardWrite.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinWindow_RequestPrimaryWrite(mwinContext* context,
                                                                      mwinWindowId window,
                                                                      const char* text,
                                                                      size_t length,
                                                                      mwinRequestId* requestOut);

    /// Asks for the primary selection's text; when the request completes
    /// with mwin_outcomeDone, mwinRequest_GetPrimaryText has it, checked as the
    /// clipboard's is.
    ///
    /// @param context     The context.
    /// @param window      The window asking, which should have focus.
    /// @param requestOut  Receives the request's id. May be NULL. The null
    ///                    request on failure.
    /// @return As mwinWindow_RequestClipboardRead.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinWindow_RequestPrimaryRead(mwinContext* context,
                                                                     mwinWindowId window,
                                                                     mwinRequestId* requestOut);

    /// Copies out the text a primary selection read found, as
    /// mwinRequest_GetClipboardText does the clipboard's.
    ///
    /// @param context    The context.
    /// @param request    The read, from its completion record.
    /// @param bufferOut  Receives the text in UTF-8, not NUL-terminated.
    ///                   May be NULL when capacity is 0. Zeroed over
    ///                   capacity on failure, so a buffer too short holds
    ///                   none of it.
    /// @param capacity   The bytes bufferOut holds.
    /// @param lengthOut  Receives the text's length in bytes. On
    ///                   `mwin_errorCapacity`, the bytes needed; 0 on any
    ///                   other failure.
    /// @return As mwinRequest_GetClipboardText.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequest_GetPrimaryText(const mwinContext* context,
                                                                  mwinRequestId request,
                                                                  char* bufferOut, size_t capacity,
                                                                  size_t* lengthOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_WINDOW_CLIPBOARD_H
