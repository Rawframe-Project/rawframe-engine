// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Identifiers and pattern syntax (UAX #31): which characters may start
// and continue a name in a programming or markup language, a file name
// or a user name, and which are syntax or white space that a pattern
// language may give meaning to without ever confusing it with a name.

#ifndef MAUL_UNICODE_IDENTIFIER_H
#define MAUL_UNICODE_IDENTIFIER_H

#include "maul-unicode/encoding.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// Returns whether a code point may start an identifier (XID_Start).
    ///
    /// @param codePoint  Any value.
    /// @return true for XID_Start.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API bool muniIsIdentifierStart(uint32_t codePoint);

    /// Returns whether a code point may continue an identifier
    /// (XID_Continue, which includes XID_Start).
    ///
    /// @param codePoint  Any value.
    /// @return true for XID_Continue.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API bool muniIsIdentifierContinue(uint32_t codePoint);

    /// Returns whether a code point is Pattern_Syntax: punctuation and
    /// symbols reserved for syntax, never part of an identifier.
    ///
    /// @param codePoint  Any value.
    /// @return true for Pattern_Syntax.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API bool muniIsPatternSyntax(uint32_t codePoint);

    /// Returns whether a code point is Pattern_White_Space.
    ///
    /// @param codePoint  Any value.
    /// @return true for Pattern_White_Space.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API bool muniIsPatternWhiteSpace(uint32_t codePoint);

    /// Checks that UTF-8 text is a default identifier (UAX #31 R1): an
    /// XID_Start code point, then XID_Continue code points.
    ///
    /// @param text    The text. May be NULL when length is 0.
    /// @param length  The number of bytes.
    /// @return `muni_success` and the length; `muni_errorIdentifier` and
    ///         the offset of the first code point that breaks the rule (0
    ///         for empty text); the first UTF-8 error and its offset;
    ///         `muni_errorInvalid` for a NULL text with a nonzero length.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_NODISCARD MUNI_API muniTextResult muniCheckIdentifier(const char* text, size_t length);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UNICODE_IDENTIFIER_H
