// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Normalization (UAX #15): the four normal forms, which make text that
// looks the same compare the same. NFD takes characters apart ("e" and a
// combining acute accent for "é") and NFC puts them back together; NFKD
// and NFKC also fold compatibility characters, such as ligatures and
// full-width letters, into their plain counterparts.
//
// Normalization writes into a caller buffer and allocates nothing. It
// reorders at most 32 code points at once: a character followed by more
// combining marks than that is refused with muni_errorLimit rather than
// buffered without bound (real text carries a handful; UAX #15's
// stream-safe text carries at most 30).

#ifndef MAUL_UNICODE_NORMALIZE_H
#define MAUL_UNICODE_NORMALIZE_H

#include "maul-unicode/encoding.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // A normal form.
    typedef uint8_t muniNormalForm;

    enum
    {
        muni_nfc = 0,
        muni_nfd = 1,
        muni_nfkc = 2,
        muni_nfkd = 3,
    };

    // The answer of a quick check (UAX #15 section 9).
    typedef uint8_t muniQuickCheck;

    enum
    {
        // The text is in the form.
        muni_quickCheckYes = 0,
        // The text is not in the form.
        muni_quickCheckNo = 1,
        // Only normalizing tells: the text holds characters that may
        // combine with what precedes them.
        muni_quickCheckMaybe = 2,
    };

    /// Normalizes UTF-8 text into a normal form.
    ///
    /// @param text        The text. May be NULL when length is 0.
    /// @param length      The number of bytes.
    /// @param form        muni_nfc, muni_nfd, muni_nfkc or muni_nfkd.
    /// @param mode        What to do with ill-formed input.
    /// @param output      The output. May be NULL when capacity is 0.
    /// @param capacity    The number of bytes output can hold.
    /// @param neededOut   Receives the number of bytes the whole result
    ///                    needs, which may exceed capacity. On another
    ///                    error, the bytes before it.
    /// @return `muni_success` and the length; `muni_errorCapacity` when
    ///         the result does not fit (the bytes that fit are written); in
    ///         strict mode the first UTF-8 error and its offset;
    ///         `muni_errorLimit` and the offset of the code point that
    ///         makes a run of combining marks too long;
    ///         `muni_errorInvalid` for a NULL argument or an unknown form.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_NODISCARD MUNI_API muniTextResult muniNormalize(const char* text, size_t length,
                                                         muniNormalForm form, muniConvertMode mode,
                                                         char* output, size_t capacity,
                                                         size_t* neededOut);

    /// Tells quickly whether UTF-8 text is in a normal form, without
    /// normalizing it.
    ///
    /// @param text       The text. May be NULL when length is 0.
    /// @param length     The number of bytes.
    /// @param form       muni_nfc, muni_nfd, muni_nfkc or muni_nfkd.
    /// @param answerOut  Receives muni_quickCheckYes, muni_quickCheckNo or
    ///                   muni_quickCheckMaybe.
    /// @return `muni_success` and the length; the first UTF-8 error and
    ///         its offset; `muni_errorInvalid` for a NULL argument or an
    ///         unknown form.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_NODISCARD MUNI_API muniTextResult muniCheckNormalization(const char* text, size_t length,
                                                                  muniNormalForm form,
                                                                  muniQuickCheck* answerOut);

    /// Returns the canonical decomposition of a code point one level deep,
    /// as shapers such as HarfBuzz ask for it: two code points, or one
    /// with the second 0. Hangul syllables split into LV + T or L + V.
    ///
    /// @param codePoint  Any value.
    /// @param firstOut   Receives the first code point.
    /// @param secondOut  Receives the second code point, or 0.
    /// @return true when the code point decomposes; the outputs are left
    ///         alone when it does not, or when either is NULL.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API bool muniDecomposePair(uint32_t codePoint, uint32_t* firstOut, uint32_t* secondOut);

    /// Returns the primary composite of two code points (UAX #15 D114),
    /// Hangul syllables included.
    ///
    /// @param first   The first code point.
    /// @param second  The second code point.
    /// @return The composite, or 0 when the pair does not compose.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API uint32_t muniComposePair(uint32_t first, uint32_t second);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UNICODE_NORMALIZE_H
