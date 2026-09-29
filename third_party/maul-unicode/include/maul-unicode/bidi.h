// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Unicode Bidirectional Algorithm (UAX #9): the display order of text
// that mixes left-to-right scripts, such as Latin, with right-to-left
// ones, such as Arabic and Hebrew.
//
// muniResolveBidi takes a paragraph of UTF-8 and gives every byte an
// embedding level: even levels run left to right, odd ones right to left.
// A layout engine breaks the paragraph into lines, and for each line
// muniReorderBidiLine gives the runs of equal level in the order they
// appear on screen, left to right. Glyphs of an odd run are laid out
// right to left, and characters with a muniGetMirroringGlyph are drawn
// mirrored there, as rule L4 asks.
//
// Nothing allocates: the caller gives one level byte and one workspace
// byte per byte of text, and the fixed stacks of the algorithm (125
// embedding levels, 63 open brackets) live on the call stack.

#ifndef MAUL_UNICODE_BIDI_H
#define MAUL_UNICODE_BIDI_H

#include "maul-unicode/base.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // The direction of a paragraph.
    typedef uint8_t muniBidiDirection;

    enum
    {
        // From the first strong character outside isolates (rules P2 and
        // P3), left to right when there is none.
        muni_bidiAuto = 0,
        muni_bidiLeftToRight = 1,
        muni_bidiRightToLeft = 2,
    };

    // A run of one line in visual order: bytes from start, relative to the
    // line, with one level. An odd level displays right to left.
    typedef struct muniBidiRun
    {
        size_t start;
        size_t length;
        uint8_t level;
    } muniBidiRun;

    /// Resolves the embedding levels of the first paragraph of a UTF-8
    /// text (rules P1 to I2). The paragraph ends after the first paragraph
    /// separator, such as LF, CR LF or U+2029, or at the end of the text;
    /// call again on the rest for the next paragraph. Code points that rule
    /// X9 removes, such as U+202A to U+202E, take the level before them.
    ///
    /// @param text                The text. May be NULL when length is 0.
    /// @param length              The number of bytes.
    /// @param direction           The paragraph direction, or muni_bidiAuto.
    /// @param levels              Receives one level per byte of the
    ///                            paragraph; holds length bytes.
    /// @param workspace           Scratch memory of length bytes.
    /// @param paragraphLengthOut  Receives the paragraph's length in bytes.
    /// @param paragraphLevelOut   Receives the paragraph embedding level,
    ///                            0 or 1.
    /// @return `muni_success`, or `muni_errorInvalid` for a NULL argument or
    ///         an unknown direction.
    /// @par Thread safety
    /// Safe from any thread; the levels and workspace are used by one thread
    /// at a time.
    MUNI_NODISCARD MUNI_API muniResult muniResolveBidi(const char* text, size_t length,
                                                       muniBidiDirection direction, uint8_t* levels,
                                                       uint8_t* workspace,
                                                       size_t* paragraphLengthOut,
                                                       uint8_t* paragraphLevelOut);

    /// Orders one line of a resolved paragraph for display (rules L1 and
    /// L2): writes its runs from left to right. Trailing whitespace and
    /// isolate formatting characters, and those before a tab or paragraph
    /// separator, take the paragraph level first.
    ///
    /// @param line            The line's text within the paragraph.
    /// @param levels          The line's levels from muniResolveBidi.
    /// @param length          The line's length in bytes.
    /// @param paragraphLevel  The paragraph embedding level.
    /// @param runs            The output. May be NULL when capacity is 0.
    /// @param capacity        The number of runs the output can hold.
    /// @param countOut        Receives the number of runs.
    /// @return `muni_success`; `muni_errorCapacity` when the runs do not
    ///         fit, and then none is written; `muni_errorInvalid` for a
    ///         NULL argument.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_NODISCARD MUNI_API muniResult muniReorderBidiLine(const char* line, const uint8_t* levels,
                                                           size_t length, uint8_t paragraphLevel,
                                                           muniBidiRun* runs, size_t capacity,
                                                           size_t* countOut);

    /// Computes a visual order from levels (rule L2), for any units the
    /// caller orders: code points, clusters or glyphs of one line, each
    /// with its level after rule L1.
    ///
    /// @param levels             One level per unit, in logical order.
    /// @param count              The number of units.
    /// @param visualToLogicalOut Receives, for each position from left to
    ///                           right, the logical index of its unit.
    /// @return `muni_success`, or `muni_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_NODISCARD MUNI_API muniResult muniReorderBidiLevels(const uint8_t* levels, size_t count,
                                                             size_t* visualToLogicalOut);

    /// Inverts a visual-to-logical map into a logical-to-visual one, or
    /// the other way round.
    ///
    /// @param map         A permutation of 0 to count - 1.
    /// @param count       The number of entries.
    /// @param inverseOut  Receives the inverse permutation.
    /// @return `muni_success`, or `muni_errorInvalid` for a NULL argument or
    ///         an entry out of range.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_NODISCARD MUNI_API muniResult muniInvertBidiMap(const size_t* map, size_t count,
                                                         size_t* inverseOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UNICODE_BIDI_H
