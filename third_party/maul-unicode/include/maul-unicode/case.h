// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Case mapping and case folding (Unicode chapter 3.13): lowercase,
// uppercase and titlecase, and folding for comparing text without
// regard to case.
//
// Per code point, the simple mappings give one code point for one. Over
// text, the full mappings may change the length ("ß" uppercases to
// "SS"), a final sigma lowercases to "ς", and titlecasing starts each
// word (UAX #29) with a capital. Turkish and Azerbaijani, which pair a
// dotted and a dotless i, take muni_caseTurkic; Lithuanian, which keeps
// the dot of an i under other accents, takes muni_caseLithuanian.

#ifndef MAUL_UNICODE_CASE_H
#define MAUL_UNICODE_CASE_H

#include "maul-unicode/encoding.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // A case operation over text.
    typedef uint8_t muniCaseOperation;

    enum
    {
        muni_caseLower = 0,
        muni_caseUpper = 1,
        // The first cased character of each word to titlecase, the rest
        // to lowercase.
        muni_caseTitle = 2,
        // Full case folding, for caseless comparison.
        muni_caseFold = 3,
    };

    // Language rules for case operations.
    typedef uint8_t muniCaseLanguage;

    enum
    {
        // The default rules, for every language but those below.
        muni_caseDefault = 0,
        // Turkish and Azerbaijani: "i" uppercases to "İ" and "I"
        // lowercases to "ı".
        muni_caseTurkic = 1,
        // Lithuanian: an i keeps its dot under other accents above, as
        // "i̇̀"; lowercasing writes the dot out and uppercasing drops it.
        muni_caseLithuanian = 2,
    };

    /// Returns the simple lowercase mapping of a code point.
    ///
    /// @param codePoint  Any value.
    /// @return The lowercase code point, or codePoint when it has none.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API uint32_t muniToLower(uint32_t codePoint);

    /// Returns the simple uppercase mapping of a code point.
    ///
    /// @param codePoint  Any value.
    /// @return The uppercase code point, or codePoint when it has none.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API uint32_t muniToUpper(uint32_t codePoint);

    /// Returns the simple titlecase mapping of a code point, which differs
    /// from the uppercase one for digraphs such as "ǆ", which titlecases
    /// to "ǅ".
    ///
    /// @param codePoint  Any value.
    /// @return The titlecase code point, or codePoint when it has none.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API uint32_t muniToTitle(uint32_t codePoint);

    /// Returns the simple case folding of a code point (statuses C and S
    /// of CaseFolding.txt).
    ///
    /// @param codePoint  Any value.
    /// @return The folded code point, or codePoint when it folds to itself.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API uint32_t muniFoldCase(uint32_t codePoint);

    /// Returns whether a code point is Cased (a letter with case, or one
    /// such as "ª" that counts as one).
    ///
    /// @param codePoint  Any value.
    /// @return true when it is Cased.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API bool muniIsCased(uint32_t codePoint);

    /// Applies a full case operation to UTF-8 text.
    ///
    /// @param text        The text. May be NULL when length is 0.
    /// @param length      The number of bytes.
    /// @param operation   muni_caseLower, muni_caseUpper, muni_caseTitle or
    ///                    muni_caseFold.
    /// @param language    muni_caseDefault, muni_caseTurkic or
    ///                    muni_caseLithuanian.
    /// @param mode        What to do with ill-formed input.
    /// @param output      The output. May be NULL when capacity is 0.
    /// @param capacity    The number of bytes output can hold.
    /// @param neededOut   Receives the number of bytes the whole result
    ///                    needs, which may exceed capacity. On another
    ///                    error, the bytes before it.
    /// @return `muni_success` and the length; `muni_errorCapacity` when
    ///         the result does not fit (the bytes that fit are written); in
    ///         strict mode the first UTF-8 error and its offset;
    ///         `muni_errorInvalid` for a NULL argument or an unknown
    ///         operation, language or mode.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_NODISCARD MUNI_API muniTextResult muniConvertCase(const char* text, size_t length,
                                                           muniCaseOperation operation,
                                                           muniCaseLanguage language,
                                                           muniConvertMode mode, char* output,
                                                           size_t capacity, size_t* neededOut);

    /// Maps UTF-8 text to NFKC_Casefold: compatibility forms to their
    /// plain selves, every case to one, default ignorables removed, and
    /// the result in NFC. Two strings match without regard to case or
    /// compatibility forms when their NFKC_Casefold texts are equal;
    /// UAX #31 uses it to compare identifiers. It needs both the case
    /// and the normalization components.
    ///
    /// @param text        The text. May be NULL when length is 0.
    /// @param length      The number of bytes.
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
    ///         makes a run of combining marks too long, as in
    ///         muniNormalize; `muni_errorInvalid` for a NULL argument or
    ///         an unknown mode.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_NODISCARD MUNI_API muniTextResult muniToNfkcCasefold(const char* text, size_t length,
                                                              muniConvertMode mode, char* output,
                                                              size_t capacity, size_t* neededOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UNICODE_CASE_H
