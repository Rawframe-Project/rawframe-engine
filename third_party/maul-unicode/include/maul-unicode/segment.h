// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text segmentation (UAX #29): the boundaries of grapheme clusters, the
// units a user perceives as one character, which caret movement,
// selection and backspace must never split; of words, for double-click
// selection, word movement and search; and of sentences. Line breaking
// (UAX #14): the positions where a line may, or must, end.
//
// An iterator walks UTF-8 text and reports each boundary after the start
// of the text, the last one being the end of the text; empty text has
// none. Offsets are in bytes from the start of the whole text. Each
// maximal ill-formed UTF-8 subpart counts as one U+FFFD. Word boundaries
// are the positions UAX #29 defines; between two of them may lie a word,
// a run of spaces or a punctuation mark, which the caller tells apart.
//
// Text may arrive in pieces. An iterator initialized with more text to
// follow stops with muni_needMoreText when the next boundary depends on
// text it has not seen; muniFeedSegmentIterator hands it the next piece,
// which may begin in the middle of a UTF-8 sequence. Some rules look
// ahead: a word boundary may wait for the next code point, and a
// sentence boundary after an abbreviation's period for the next letter.

#ifndef MAUL_UNICODE_SEGMENT_H
#define MAUL_UNICODE_SEGMENT_H

#include "maul-unicode/base.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // An iterator over grapheme cluster, word or sentence boundaries or
    // line break opportunities. Its contents are private; it is plain data
    // the caller keeps anywhere, usually on the stack, and needs no
    // cleanup.
    typedef struct muniSegmentIterator
    {
        uint64_t opaque[16];
    } muniSegmentIterator;

    // A caller's word segmenter for Thai, Lao, Khmer and Burmese, which
    // write words without spaces (Line_Break=SA). It receives a maximal
    // run of SA text as UTF-8 and a byte offset from inside it, and
    // returns the byte offset of the first break between words after
    // from, or length when there is none. It must return the same answer
    // for the same run and offset each time it is asked.
    typedef size_t (*muniComplexBreakFn)(void* context, const char* run, size_t length,
                                         size_t from);

    /// Starts an iterator over the grapheme cluster boundaries of a UTF-8 text or
    /// its first piece.
    ///
    /// @param iterator     The iterator to initialize.
    /// @param text         The text. May be NULL when length is 0.
    /// @param length       The number of bytes.
    /// @param moreFollows  true when more pieces of the text will be fed.
    /// @return `muni_success`, or `muni_errorInvalid` for a NULL iterator
    ///         or a NULL text with a nonzero length.
    /// @par Thread safety
    /// Safe from any thread; an iterator is used by one thread at a time.
    MUNI_NODISCARD MUNI_API muniResult muniInitGraphemeIterator(muniSegmentIterator* iterator,
                                                                const char* text, size_t length,
                                                                bool moreFollows);

    /// Starts an iterator over the word boundaries of a UTF-8 text or
    /// its first piece.
    ///
    /// @param iterator     The iterator to initialize.
    /// @param text         The text. May be NULL when length is 0.
    /// @param length       The number of bytes.
    /// @param moreFollows  true when more pieces of the text will be fed.
    /// @return `muni_success`, or `muni_errorInvalid` for a NULL iterator
    ///         or a NULL text with a nonzero length.
    /// @par Thread safety
    /// Safe from any thread; an iterator is used by one thread at a time.
    MUNI_NODISCARD MUNI_API muniResult muniInitWordIterator(muniSegmentIterator* iterator,
                                                            const char* text, size_t length,
                                                            bool moreFollows);

    /// Starts an iterator over the sentence boundaries of a UTF-8 text or
    /// its first piece.
    ///
    /// @param iterator     The iterator to initialize.
    /// @param text         The text. May be NULL when length is 0.
    /// @param length       The number of bytes.
    /// @param moreFollows  true when more pieces of the text will be fed.
    /// @return `muni_success`, or `muni_errorInvalid` for a NULL iterator
    ///         or a NULL text with a nonzero length.
    /// @par Thread safety
    /// Safe from any thread; an iterator is used by one thread at a time.
    MUNI_NODISCARD MUNI_API muniResult muniInitSentenceIterator(muniSegmentIterator* iterator,
                                                                const char* text, size_t length,
                                                                bool moreFollows);

    /// Starts an iterator over the line break opportunities of a UTF-8
    /// text or its first piece, with the default rules of UAX #14.
    /// Thai, Lao, Khmer and Burmese letters (class SA) are resolved as rule
    /// LB1 says without a dictionary, with no opportunities between them,
    /// unless muniSetComplexBreaker supplies a word segmenter.
    ///
    /// @param iterator     The iterator to initialize.
    /// @param text         The text. May be NULL when length is 0.
    /// @param length       The number of bytes.
    /// @param moreFollows  true when more pieces of the text will be fed.
    /// @return `muni_success`, or `muni_errorInvalid` for a NULL iterator
    ///         or a NULL text with a nonzero length.
    /// @par Thread safety
    /// Safe from any thread; an iterator is used by one thread at a time.
    MUNI_NODISCARD MUNI_API muniResult muniInitLineIterator(muniSegmentIterator* iterator,
                                                            const char* text, size_t length,
                                                            bool moreFollows);

    /// Sets the word segmenter a line or word iterator hands runs of SA
    /// text to. Inside such a run its breaks replace the default ones:
    /// line breaking then allows a break between words, and word
    /// segmentation finds whole words instead of single letters. A break
    /// before a combining mark is ignored. A run lies within one piece of
    /// text; a piece boundary inside a run hands the run over in parts.
    /// Without a segmenter, SA text follows rule LB1 of UAX #14 and the
    /// default word rules.
    ///
    /// @param iterator  A line or word iterator not yet asked for a break.
    /// @param breaker   The segmenter, or NULL for the default rules.
    /// @param context   Passed to the segmenter unchanged.
    /// @return `muni_success`, or `muni_errorInvalid` for a NULL iterator,
    ///         another kind of iterator or one already started.
    /// @par Thread safety
    /// Safe from any thread; an iterator is used by one thread at a time,
    /// and calls the segmenter on that thread.
    MUNI_NODISCARD MUNI_API muniResult muniSetComplexBreaker(muniSegmentIterator* iterator,
                                                             muniComplexBreakFn breaker,
                                                             void* context);

    /// Hands an iterator the next piece of its text, after it returned
    /// muni_needMoreText. The iterator keeps no pointer to earlier pieces.
    ///
    /// @param iterator     The iterator.
    /// @param text         The next piece. May be NULL when length is 0.
    /// @param length       The number of bytes.
    /// @param moreFollows  true when still more pieces will be fed.
    /// @return `muni_success`, or `muni_errorInvalid` for a NULL argument,
    ///         an iterator never initialized, one initialized without more
    ///         text to follow, or a piece fed before the iterator asked.
    /// @par Thread safety
    /// Safe from any thread; an iterator is used by one thread at a time.
    MUNI_NODISCARD MUNI_API muniResult muniFeedSegmentIterator(muniSegmentIterator* iterator,
                                                               const char* text, size_t length,
                                                               bool moreFollows);

    /// Finds the next boundary.
    ///
    /// @param iterator   The iterator.
    /// @param offsetOut  Receives the boundary's byte offset from the start
    ///                   of the whole text.
    /// @return `muni_success` with a boundary; `muni_done` after the last
    ///         one; `muni_needMoreText` when the next boundary depends on
    ///         text not yet fed; `muni_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Safe from any thread; an iterator is used by one thread at a time.
    MUNI_NODISCARD MUNI_API muniResult muniNextSegmentBreak(muniSegmentIterator* iterator,
                                                            size_t* offsetOut);

    /// Finds the next line break opportunity of a line iterator, and
    /// whether the line must end there: after a mandatory break character
    /// (BK, CR, LF or NL) and at the end of the text.
    ///
    /// @param iterator      An iterator from muniInitLineIterator.
    /// @param offsetOut     Receives the opportunity's byte offset from the
    ///                      start of the whole text.
    /// @param mandatoryOut  Receives true when the break is mandatory.
    /// @return As muniNextSegmentBreak; `muni_errorInvalid` also for an
    ///         iterator of another kind.
    /// @par Thread safety
    /// Safe from any thread; an iterator is used by one thread at a time.
    MUNI_NODISCARD MUNI_API muniResult muniNextLineBreak(muniSegmentIterator* iterator,
                                                         size_t* offsetOut, bool* mandatoryOut);

    /// Writes the grapheme cluster boundaries of a whole UTF-8 text into a caller
    /// array: every boundary after the start, the end included.
    ///
    /// @param text         The text. May be NULL when length is 0.
    /// @param length       The number of bytes.
    /// @param offsets      The output. May be NULL when capacity is 0.
    /// @param capacity     The number of offsets the output can hold.
    /// @param countOut     Receives the number of boundaries, which may
    ///                     exceed capacity; the ones that fit are written.
    /// @return `muni_success`, `muni_errorCapacity` when they do not all
    ///         fit, or `muni_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_NODISCARD MUNI_API muniResult muniFindGraphemeBreaks(const char* text, size_t length,
                                                              size_t* offsets, size_t capacity,
                                                              size_t* countOut);

    /// Writes the word boundaries of a whole UTF-8 text into a caller
    /// array: every boundary after the start, the end included.
    ///
    /// @param text         The text. May be NULL when length is 0.
    /// @param length       The number of bytes.
    /// @param offsets      The output. May be NULL when capacity is 0.
    /// @param capacity     The number of offsets the output can hold.
    /// @param countOut     Receives the number of boundaries, which may
    ///                     exceed capacity; the ones that fit are written.
    /// @return `muni_success`, `muni_errorCapacity` when they do not all
    ///         fit, or `muni_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_NODISCARD MUNI_API muniResult muniFindWordBreaks(const char* text, size_t length,
                                                          size_t* offsets, size_t capacity,
                                                          size_t* countOut);

    /// Writes the sentence boundaries of a whole UTF-8 text into a caller
    /// array: every boundary after the start, the end included.
    ///
    /// @param text         The text. May be NULL when length is 0.
    /// @param length       The number of bytes.
    /// @param offsets      The output. May be NULL when capacity is 0.
    /// @param capacity     The number of offsets the output can hold.
    /// @param countOut     Receives the number of boundaries, which may
    ///                     exceed capacity; the ones that fit are written.
    /// @return `muni_success`, `muni_errorCapacity` when they do not all
    ///         fit, or `muni_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_NODISCARD MUNI_API muniResult muniFindSentenceBreaks(const char* text, size_t length,
                                                              size_t* offsets, size_t capacity,
                                                              size_t* countOut);

    /// Writes the line break opportunities of a whole UTF-8 text into
    /// caller arrays: every opportunity after the start, the end included,
    /// and optionally whether each is mandatory.
    ///
    /// @param text         The text. May be NULL when length is 0.
    /// @param length       The number of bytes.
    /// @param offsets      The output. May be NULL when capacity is 0.
    /// @param mandatory    NULL, or an array of capacity flags that receive
    ///                     true for each mandatory break.
    /// @param capacity     The number of entries the outputs can hold.
    /// @param countOut     Receives the number of opportunities, which may
    ///                     exceed capacity; the ones that fit are written.
    /// @return `muni_success`, `muni_errorCapacity` when they do not all
    ///         fit, or `muni_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_NODISCARD MUNI_API muniResult muniFindLineBreaks(const char* text, size_t length,
                                                          size_t* offsets, bool* mandatory,
                                                          size_t capacity, size_t* countOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UNICODE_SEGMENT_H
