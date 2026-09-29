// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Drag and drop onto windows. While something is dragged over a window
// the program hears mwin_eventDragEntered, mwin_eventDragMoved (merged
// when motion waits) and mwin_eventDragLeft, so it can show where a drop
// would land; a drop brings mwin_eventDropped instead of the leaving.
// Windows take every drag that carries files or text.
//
// A drop's files and text wait in the context under the drop's number
// until the next drop, where these functions copy them out. Files come
// as their paths in UTF-8, each ended by a NUL; on the web, where a page
// never sees paths, as their names. A path that is not UTF-8 is left
// out, since no program could name the file with it, and text from
// another program has each maximal ill-formed subpart replaced with
// U+FFFD. Files past the droppedFiles limit, or paths past dropBytes,
// are left out whole, and text past dropBytes is left out; the drop's
// record says when anything was. A path names a file; it grants no
// access the program did not already have.

#ifndef MAUL_WINDOW_DROP_H
#define MAUL_WINDOW_DROP_H

#include "maul-window/context.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// Copies out the paths of a drop's files, each ended by a NUL.
    ///
    /// @param context    The context.
    /// @param drop       The drop's number, from its record.
    /// @param buffer     Receives the paths. May be NULL when capacity is 0.
    /// @param capacity   The bytes buffer holds.
    /// @param lengthOut  Receives the paths' length in bytes, NULs
    ///                   included.
    /// @return `mwin_success`; `mwin_errorCapacity` when they do not fit
    ///         (the bytes that fit are written); `mwin_errorStale` for a
    ///         drop a later one replaced; `mwin_errorInvalid` for a NULL
    ///         argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinGetDroppedFiles(const mwinContext* context,
                                                           uint32_t drop, char* buffer,
                                                           size_t capacity, size_t* lengthOut);

    /// Copies out the text of a drop.
    ///
    /// @param context    The context.
    /// @param drop       The drop's number, from its record.
    /// @param buffer     Receives the text in UTF-8, not NUL-terminated.
    ///                   May be NULL when capacity is 0.
    /// @param capacity   The bytes buffer holds.
    /// @param lengthOut  Receives the text's length in bytes.
    /// @return As mwinGetDroppedFiles.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinGetDroppedText(const mwinContext* context, uint32_t drop,
                                                          char* buffer, size_t capacity,
                                                          size_t* lengthOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_WINDOW_DROP_H
