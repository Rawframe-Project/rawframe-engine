// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Script runs: the stretches of text a shaper such as HarfBuzz shapes
// with one script. Characters used with many scripts, such as spaces,
// digits and punctuation (Script Common), and combining marks (Inherited)
// join the run around them; a character used with a few scripts
// (Script_Extensions, UAX #24) narrows the run to those it shares; a
// closing bracket takes the script of its opening bracket. Leading
// Common characters take the script of the first run.
//
// Like the segment iterators, a script iterator is a fixed-size struct
// the caller keeps, takes text in pieces cut anywhere, and asks for the
// next piece with muni_needMoreText.

#ifndef MAUL_UNICODE_SCRIPT_H
#define MAUL_UNICODE_SCRIPT_H

#include "maul-unicode/properties.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // An iterator over script runs. Its contents are private; it is plain
    // data the caller keeps anywhere and needs no cleanup.
    typedef struct muniScriptIterator
    {
        uint64_t opaque[28];
    } muniScriptIterator;

    // A script run: it ends at byte offset end, where the next one starts.
    typedef struct muniScriptRun
    {
        size_t end;
        muniScript script;
    } muniScriptRun;

    /// Starts an iterator over the script runs of a UTF-8 text or its
    /// first piece.
    ///
    /// @param iterator     The iterator to initialize.
    /// @param text         The text. May be NULL when length is 0.
    /// @param length       The number of bytes.
    /// @param moreFollows  true when more pieces of the text will be fed.
    /// @return `muni_success`, or `muni_errorInvalid` for a NULL iterator
    ///         or a NULL text with a nonzero length.
    /// @par Thread safety
    /// Safe from any thread; an iterator is used by one thread at a time.
    MUNI_NODISCARD MUNI_API muniResult muniInitScriptIterator(muniScriptIterator* iterator,
                                                              const char* text, size_t length,
                                                              bool moreFollows);

    /// Hands a script iterator the next piece of its text, after it
    /// returned muni_needMoreText.
    ///
    /// @param iterator     The iterator.
    /// @param text         The next piece. May be NULL when length is 0.
    /// @param length       The number of bytes.
    /// @param moreFollows  true when still more pieces will be fed.
    /// @return `muni_success`, or `muni_errorInvalid` for a NULL argument,
    ///         an iterator that did not ask for more text, or one
    ///         initialized without more text to follow.
    /// @par Thread safety
    /// Safe from any thread; an iterator is used by one thread at a time.
    MUNI_NODISCARD MUNI_API muniResult muniFeedScriptIterator(muniScriptIterator* iterator,
                                                              const char* text, size_t length,
                                                              bool moreFollows);

    /// Finds the next script run.
    ///
    /// @param iterator  The iterator.
    /// @param runOut    Receives the run: where it ends, as a byte offset
    ///                  from the start of the whole text, and its script.
    ///                  A text of Common and Inherited characters only is
    ///                  one run of Common.
    /// @return `muni_success` with a run; `muni_done` after the last one;
    ///         `muni_needMoreText` when the run's end or script depends on
    ///         text not yet fed; `muni_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Safe from any thread; an iterator is used by one thread at a time.
    MUNI_NODISCARD MUNI_API muniResult muniNextScriptRun(muniScriptIterator* iterator,
                                                         muniScriptRun* runOut);

    /// Writes the script runs of a whole UTF-8 text into a caller array.
    ///
    /// @param text      The text. May be NULL when length is 0.
    /// @param length    The number of bytes.
    /// @param runs      The output. May be NULL when capacity is 0.
    /// @param capacity  The number of runs the output can hold.
    /// @param countOut  Receives the number of runs, which may exceed
    ///                  capacity; the ones that fit are written.
    /// @return `muni_success`, `muni_errorCapacity` when they do not all
    ///         fit, or `muni_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_NODISCARD MUNI_API muniResult muniFindScriptRuns(const char* text, size_t length,
                                                          muniScriptRun* runs, size_t capacity,
                                                          size_t* countOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UNICODE_SCRIPT_H
