// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Security mechanisms (UTS #39) for names people read, such as user names
// and identifiers: whether two strings can pass for each other, and how
// far a string mixes scripts.
//
// Two strings are confusable when their skeletons are equal: "paypal"
// written with a Cyrillic "а" has the skeleton of the Latin one. A
// skeleton is not text to show; store it next to a name, compare it as
// bytes, and recompute it when the library's Unicode version changes.
// Skeletons follow the reading order bidi gives each string, so the
// caller supplies bidi's workspace, and nothing allocates.
//
// The restriction level grades how a string mixes scripts, from ASCII
// only to anything, under the General Security Profile: a string with a
// character the profile does not allow is unrestricted. The level does
// not check the string's syntax; an identifier should also pass
// muniCheckIdentifier.

#ifndef MAUL_UNICODE_SECURITY_H
#define MAUL_UNICODE_SECURITY_H

#include "maul-unicode/bidi.h"
#include "maul-unicode/encoding.h"
#include "maul-unicode/properties.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // A restriction level (UTS #39 section 5.2); a lower one is stricter.
    typedef uint8_t muniRestrictionLevel;

    enum
    {
        // ASCII characters only.
        muni_restrictionAsciiOnly = 1,
        // One script, with Common and Inherited characters.
        muni_restrictionSingleScript = 2,
        // Latin with Han, Hiragana and Katakana; with Han and Bopomofo;
        // or with Han and Hangul.
        muni_restrictionHighlyRestrictive = 3,
        // Latin with one other Recommended script, but Cyrillic or Greek.
        muni_restrictionModeratelyRestrictive = 4,
        // Any scripts.
        muni_restrictionMinimallyRestrictive = 5,
        // Characters outside the General Security Profile.
        muni_restrictionUnrestricted = 6,
    };

    /// Tells whether a code point has Identifier_Status Allowed, the
    /// General Security Profile for identifiers.
    ///
    /// @param codePoint  Any value.
    /// @return true when the profile allows the code point.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API bool muniIsIdentifierAllowed(uint32_t codePoint);

    /// Computes the skeleton of UTF-8 text as displayed in a paragraph of
    /// a direction: bidiSkeleton of UTS #39 section 4. The skeleton of
    /// muni_bidiLeftToRight is the one UTS #39 calls skeleton;
    /// muni_bidiAuto takes the direction from the first strong character.
    /// Text without right-to-left characters has the same skeleton in
    /// both automatic and left-to-right paragraphs.
    ///
    /// @param text        The text. May be NULL when length is 0.
    /// @param length      The number of bytes.
    /// @param direction   The paragraph direction.
    /// @param workspace   Scratch memory of 2 * length bytes.
    /// @param output      The output. May be NULL when capacity is 0.
    /// @param capacity    The number of bytes output can hold.
    /// @param neededOut   Receives the number of bytes the whole skeleton
    ///                    needs, which may exceed capacity.
    /// @return `muni_success` and the length; `muni_errorCapacity` when
    ///         the skeleton does not fit (the bytes that fit are
    ///         written); the first UTF-8 error and its offset;
    ///         `muni_errorLimit` when a run of combining marks outgrows
    ///         32, and the offset of the code point being read then;
    ///         `muni_errorInvalid` for a NULL argument or an unknown
    ///         direction.
    /// @par Thread safety
    /// Safe from any thread; the workspace and output are used by one thread
    /// at a time.
    MUNI_NODISCARD MUNI_API muniTextResult muniGetSkeleton(const char* text, size_t length,
                                                           muniBidiDirection direction,
                                                           uint8_t* workspace, char* output,
                                                           size_t capacity, size_t* neededOut);

    /// Writes the resolved script set of UTF-8 text (UTS #39 section
    /// 5.1): the scripts every character can belong to, counting Han
    /// text as also Japanese (Jpan), Korean (Kore) and Han with Bopomofo
    /// (Hanb) or Latin (Hntl), Hiragana and Katakana as Japanese, Hangul
    /// as Korean, Bopomofo as Hanb and Latin as Hntl. No script means the
    /// text mixes scripts. Text of Common and Inherited characters alone,
    /// which fits any script, gives the single script Zyyy.
    ///
    /// @param text      The text. May be NULL when length is 0.
    /// @param length    The number of bytes.
    /// @param scripts   The output, in ascending tag order. May be NULL
    ///                  when capacity is 0.
    /// @param capacity  The number of scripts the output can hold; 64 is
    ///                  always enough.
    /// @param countOut  Receives the number of scripts.
    /// @return `muni_success` and the length; `muni_errorCapacity` when
    ///         the scripts do not fit, and then none is written; the
    ///         first UTF-8 error and its offset; `muni_errorInvalid` for
    ///         a NULL argument.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_NODISCARD MUNI_API muniTextResult muniGetResolvedScripts(const char* text, size_t length,
                                                                  muniScript* scripts,
                                                                  size_t capacity,
                                                                  size_t* countOut);

    /// Grades how UTF-8 text mixes scripts (UTS #39 section 5.2).
    ///
    /// @param text     The text. May be NULL when length is 0.
    /// @param length   The number of bytes.
    /// @param levelOut Receives one of the muni_restriction values.
    /// @return `muni_success` and the length; the first UTF-8 error and
    ///         its offset; `muni_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_NODISCARD MUNI_API muniTextResult muniGetRestrictionLevel(const char* text, size_t length,
                                                                   muniRestrictionLevel* levelOut);

    /// Tells whether UTF-8 text holds digits of more than one decimal
    /// system (UTS #39 section 5.3), such as ASCII and Bengali digits,
    /// some of which look alike.
    ///
    /// @param text     The text. May be NULL when length is 0.
    /// @param length   The number of bytes.
    /// @param mixedOut Receives true when the digits mix systems.
    /// @return `muni_success` and the length; the first UTF-8 error and
    ///         its offset; `muni_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_NODISCARD MUNI_API muniTextResult muniCheckMixedNumbers(const char* text, size_t length,
                                                                 bool* mixedOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UNICODE_SECURITY_H
