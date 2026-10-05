// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// File dialogs: open one file or many, save one, or choose a folder,
// as requests of a window answered by a completion like any other. The
// window stays live while the dialog shows, and frames go on.
//
// A dialog chosen from completes with mwin_outcomeDone, and its paths
// wait under the request's id for mwinGetDialogFiles until the next
// dialog completes; one the user closes completes with
// mwin_outcomeCancelled; a choice past the dialogFiles or dialogBytes
// limits with mwin_outcomeTooLarge. A page names no files, so the web
// has no dialogs (mwin_outcomeUnsupported). Android's documents have no
// paths either: the documents an Android dialog opens are copied into
// the application's cache first, and their copies' paths answered;
// saving and choosing a folder are unsupported there.

#ifndef MAUL_WINDOW_DIALOG_H
#define MAUL_WINDOW_DIALOG_H

#include "maul-window/window.h"

#ifdef __cplusplus
extern "C"
{
#endif

// The most filters a dialog offers, and the most bytes of a dialog's
// title, of a filter's name and of its extensions.
#define MWIN_DIALOG_FILTERS      16
#define MWIN_DIALOG_TITLE_BYTES  512
#define MWIN_DIALOG_FILTER_BYTES 256

    // What a dialog chooses.
    typedef uint8_t mwinDialogKind;

    enum
    {
        // One file to open.
        mwin_dialogOpen = 0,
        // One file or more to open.
        mwin_dialogOpenMany = 1,
        // A file to save to, which may not exist yet.
        mwin_dialogSave = 2,
        // A folder.
        mwin_dialogFolder = 3,
    };

    // A kind of file a dialog offers: a name, and the extensions it
    // takes, without dots, separated by ';' ("png;jpg"); every platform
    // matches them in any case.
    typedef struct mwinFileFilter
    {
        // UTF-8 without NULs, not NUL-terminated.
        const char* name;
        size_t nameLength;
        const char* extensions;
        size_t extensionsLength;
    } mwinFileFilter;

    // A dialog. Build it with mwinDefaultFileDialogDef.
    typedef struct mwinFileDialogDef
    {
        uint32_t cookie;
        mwinDialogKind kind;
        // UTF-8 without NULs, not NUL-terminated; empty for the
        // platform's own.
        const char* title;
        size_t titleLength;
        // The absolute path of the folder it opens in; empty for the
        // platform's choice.
        const char* folder;
        size_t folderLength;
        // The name a save dialog offers; the others ignore it.
        const char* name;
        size_t nameLength;
        // The filters, the first chosen at first; none offers every file.
        // Folder dialogs ignore them.
        const mwinFileFilter* filters;
        uint32_t filterCount;
    } mwinFileDialogDef;

    /// Returns the default file dialog def: a dialog to open one file,
    /// with the platform's title and folder and no filters.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MWIN_API mwinFileDialogDef mwinDefaultFileDialogDef(void);

    /// Asks for a file dialog over a window. Its text is copied at the
    /// call.
    ///
    /// @param context    The context.
    /// @param window     The window the dialog belongs to.
    /// @param def        The dialog.
    /// @param requestOut Receives the request's id, which names its
    ///                   paths. May be NULL.
    /// @return `mwin_success`; `mwin_errorCapacity` when the context
    ///         cannot hold the copy or the window has its limit of
    ///         requests in flight; `mwin_errorStale` for a window that no
    ///         longer exists; `mwin_errorInvalid` for a NULL context or
    ///         def, an invalid def, text that is not UTF-8, holds a NUL or
    ///         passes its limit (the folder and name MWIN_ADDRESS_BYTES), a
    ///         folder that is not absolute, a filter without a name or
    ///         extensions, or an extension with a dot, '*' or '?'.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinRequestFileDialog(mwinContext* context,
                                                             mwinWindowId window,
                                                             const mwinFileDialogDef* def,
                                                             mwinRequestId* requestOut);

    /// Copies the paths a dialog chose out: absolute, UTF-8, each ended
    /// by a NUL.
    ///
    /// @param context   The context.
    /// @param request   The dialog's request, done.
    /// @param buffer    Receives the paths. May be NULL when capacity is 0.
    /// @param capacity  Its bytes.
    /// @param lengthOut Receives the bytes of the paths.
    /// @param countOut  Receives how many paths there are. May be NULL.
    /// @return `mwin_success`; `mwin_errorCapacity` with the bytes needed
    ///         when they do not fit, the buffer filled as far as it goes;
    ///         `mwin_errorStale` for a request that did not complete with
    ///         mwin_outcomeDone or whose paths a later dialog replaced;
    ///         `mwin_errorInvalid` for a NULL context or lengthOut.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinGetDialogFiles(const mwinContext* context,
                                                          mwinRequestId request, char* buffer,
                                                          size_t capacity, size_t* lengthOut,
                                                          uint32_t* countOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_WINDOW_DIALOG_H
