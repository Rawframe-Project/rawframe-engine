// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The base of the Maul Unicode API: the library and Unicode versions,
// the export and attribute macros, and the result codes every fallible
// function returns.

#ifndef MAUL_UNICODE_BASE_H
#define MAUL_UNICODE_BASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The library version. CMake reads it from here.
#define MUNI_VERSION_MAJOR 0
#define MUNI_VERSION_MINOR 2
#define MUNI_VERSION_PATCH 1

// The one Unicode version every table in the library comes from.
#define MUNI_UNICODE_VERSION_MAJOR 18
#define MUNI_UNICODE_VERSION_MINOR 0
#define MUNI_UNICODE_VERSION_PATCH 0

// MUNI_API marks the public functions: dllexport or dllimport in a
// shared Windows build (maul_unicode_EXPORTS is defined while building
// the library), default visibility in a shared build elsewhere.
#if defined(MAUL_UNICODE_SHARED) && defined(_WIN32)
#if defined(maul_unicode_EXPORTS)
#define MUNI_API __declspec(dllexport) extern
#else
#define MUNI_API __declspec(dllimport) extern
#endif
#elif defined(MAUL_UNICODE_SHARED) && (defined(__GNUC__) || defined(__clang__))
#define MUNI_API __attribute__((visibility("default"))) extern
#else
#define MUNI_API extern
#endif

// MUNI_NODISCARD marks a function whose result must be read: every
// function that returns a status. The attribute is standard in C23 and
// C++17 and left out for older dialects. MSVC keeps __cplusplus at
// 199711L unless /Zc:__cplusplus is given, so _MSVC_LANG is read too.
#if (defined(__cplusplus) && __cplusplus >= 201703L) ||                                            \
    (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
#define MUNI_NODISCARD [[nodiscard]]
#elif !defined(__cplusplus) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#define MUNI_NODISCARD [[nodiscard]]
#else
#define MUNI_NODISCARD
#endif

    // The status a fallible function returns. Zero is success, positive
    // values are outcomes that are not errors, negative values are errors.
    // The type has a fixed width so that result structs have one layout in
    // C and C++.
    typedef int32_t muniResult;

    enum
    {
        // The call did what was asked.
        muni_success = 0,
        // An iterator has reported its last result.
        muni_done = 1,
        // An iterator needs the next piece of its text before it can
        // answer.
        muni_needMoreText = 2,
        // An argument is invalid: a null pointer where one is required, a
        // value out of range.
        muni_errorInvalid = -1,
        // A caller buffer or a named limit is too small for the result.
        muni_errorCapacity = -2,
        // UTF-8: a byte that cannot start a sequence (a continuation byte,
        // or 0xF8 to 0xFF).
        muni_errorUtf8Lead = -3,
        // UTF-8: a lead byte not followed by the continuation bytes it
        // needs.
        muni_errorUtf8Continuation = -4,
        // UTF-8: a sequence cut short by the end of the input. When text
        // arrives in pieces, the next piece may complete it.
        muni_errorUtf8Truncated = -5,
        // UTF-8: an overlong form, a code point encoded in more bytes
        // than it needs.
        muni_errorUtf8Overlong = -6,
        // UTF-8: an encoded surrogate, U+D800 to U+DFFF.
        muni_errorUtf8Surrogate = -7,
        // UTF-8: a value past U+10FFFF.
        muni_errorUtf8TooLarge = -8,
        // UTF-16: a surrogate that is not half of a pair.
        muni_errorUtf16Surrogate = -9,
        // The input goes past a documented limit of the algorithm, such as
        // the number of combining marks normalization reorders at once.
        muni_errorLimit = -10,
        // The text is not an identifier (UAX #31).
        muni_errorIdentifier = -11,
        // UTF-32: a value that is no Unicode scalar value, a surrogate or
        // one past U+10FFFF.
        muni_errorUtf32Value = -12,
    };

    // A library or Unicode version: major, minor and patch.
    typedef struct muniVersion
    {
        uint16_t major;
        uint16_t minor;
        uint16_t patch;
    } muniVersion;

    /// Returns the version of the library that was linked, which may differ
    /// from the MUNI_VERSION macros a program was compiled with.
    ///
    /// @return The library version.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API muniVersion muniGetVersion(void);

    /// Returns the Unicode version every table in the library comes from.
    ///
    /// @return The Unicode version, 18.0.0 for this release.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API muniVersion muniGetUnicodeVersion(void);

    /// Returns the name of a result code, for diagnostics.
    ///
    /// @param result  Any value; an unknown one is named as such.
    /// @return A static, NUL-terminated string such as "muni_errorCapacity".
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_API const char* muniResultName(muniResult result);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UNICODE_BASE_H
