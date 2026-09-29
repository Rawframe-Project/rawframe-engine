// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// UTF-8, UTF-16 and UTF-32: validation of hostile input, decoding and
// encoding of single code points, and conversion between the three. A
// sequence is well formed exactly when the Unicode Standard's table of
// well-formed UTF-8 byte sequences (chapter 3) says so: no overlong
// forms, no surrogates, nothing past U+10FFFF.

#ifndef MAUL_UNICODE_ENCODING_H
#define MAUL_UNICODE_ENCODING_H

#include "maul-unicode/base.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // The result of validating or converting text: the status and, on a
    // failure, the offset in code units (bytes for UTF-8) where the
    // offending sequence starts. On success the offset is the input
    // length.
    typedef struct muniTextResult
    {
        muniResult status;
        size_t offset;
    } muniTextResult;

    // What a conversion does with ill-formed input.
    typedef uint8_t muniConvertMode;

    enum
    {
        // Stop at the first ill-formed sequence and report it.
        muni_convertStrict = 0,
        // Write U+FFFD for each maximal ill-formed subpart and go on, the
        // practice the Unicode Standard recommends (chapter 3).
        muni_convertReplace = 1,
    };

    /// Validates UTF-8 as hostile input.
    ///
    /// @param bytes   The text. May be NULL when length is 0.
    /// @param length  The number of bytes.
    /// @return `muni_success` and the length, or the first error and the
    ///         offset of its sequence: `muni_errorUtf8Lead`,
    ///         `muni_errorUtf8Continuation`, `muni_errorUtf8Truncated`,
    ///         `muni_errorUtf8Overlong`, `muni_errorUtf8Surrogate`,
    ///         `muni_errorUtf8TooLarge`, or `muni_errorInvalid` for a NULL
    ///         pointer with a nonzero length.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_NODISCARD MUNI_API muniTextResult muniValidateUtf8(const char* bytes, size_t length);

    /// Validates UTF-16: every surrogate must be half of a pair.
    ///
    /// @param units   The text. May be NULL when length is 0.
    /// @param length  The number of 16-bit code units.
    /// @return `muni_success` and the length, or `muni_errorUtf16Surrogate`
    ///         and the offset of the unpaired surrogate, or
    ///         `muni_errorInvalid` for a NULL pointer with a nonzero
    ///         length.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_NODISCARD MUNI_API muniTextResult muniValidateUtf16(const uint16_t* units, size_t length);

    /// Decodes the code point at the start of a UTF-8 text.
    ///
    /// @param bytes         The text. May be NULL when length is 0.
    /// @param length        The number of bytes available.
    /// @param codePointOut  Receives the code point, or U+FFFD on an error.
    /// @param sizeOut       Receives the bytes consumed: the sequence's
    ///                      length, or on an error the length of its
    ///                      maximal ill-formed subpart (at least 1).
    /// @return `muni_success`, one of the UTF-8 errors of
    ///         muniValidateUtf8, or `muni_errorInvalid` when length is 0 or
    ///         an out-parameter is NULL.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_NODISCARD MUNI_API muniResult muniDecodeUtf8(const char* bytes, size_t length,
                                                      uint32_t* codePointOut, size_t* sizeOut);

    /// Encodes one code point as UTF-8.
    ///
    /// @param codePoint  A Unicode scalar value.
    /// @param bytesOut   Receives 1 to 4 bytes; must hold 4.
    /// @param sizeOut    Receives the number of bytes written.
    /// @return `muni_success`, or `muni_errorInvalid` for a surrogate, a
    ///         value past U+10FFFF or a NULL out-parameter.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_NODISCARD MUNI_API muniResult muniEncodeUtf8(uint32_t codePoint, char* bytesOut,
                                                      size_t* sizeOut);

    /// Converts UTF-8 to UTF-16.
    ///
    /// @param bytes        The text. May be NULL when length is 0.
    /// @param length       The number of bytes.
    /// @param units        The output. May be NULL when capacity is 0.
    /// @param capacity     The number of 16-bit units units can hold.
    /// @param mode         What to do with ill-formed input.
    /// @param neededOut    Receives the number of units the whole
    ///                     conversion needs, which may exceed capacity. In
    ///                     strict mode, on an error, the units before it.
    /// @return `muni_success`; `muni_errorCapacity` when the output does
    ///         not fit (the units that fit are written); in strict mode
    ///         the first UTF-8 error and its offset; `muni_errorInvalid`
    ///         for a NULL pointer with a nonzero length.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_NODISCARD MUNI_API muniTextResult muniConvertUtf8ToUtf16(const char* bytes, size_t length,
                                                                  uint16_t* units, size_t capacity,
                                                                  muniConvertMode mode,
                                                                  size_t* neededOut);

    /// Converts UTF-16 to UTF-8.
    ///
    /// @param units        The text. May be NULL when length is 0.
    /// @param length       The number of 16-bit units.
    /// @param bytes        The output. May be NULL when capacity is 0.
    /// @param capacity     The number of bytes bytes can hold.
    /// @param mode         What to do with unpaired surrogates.
    /// @param neededOut    Receives the number of bytes the whole
    ///                     conversion needs, which may exceed capacity. In
    ///                     strict mode, on an error, the bytes before it.
    /// @return `muni_success`; `muni_errorCapacity` when the output does
    ///         not fit (the bytes that fit are written); in strict mode
    ///         `muni_errorUtf16Surrogate` and its offset;
    ///         `muni_errorInvalid` for a NULL pointer with a nonzero
    ///         length.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_NODISCARD MUNI_API muniTextResult muniConvertUtf16ToUtf8(const uint16_t* units,
                                                                  size_t length, char* bytes,
                                                                  size_t capacity,
                                                                  muniConvertMode mode,
                                                                  size_t* neededOut);

    /// Converts UTF-8 to UTF-32.
    ///
    /// @param bytes        The text. May be NULL when length is 0.
    /// @param length       The number of bytes.
    /// @param codePoints   The output. May be NULL when capacity is 0.
    /// @param capacity     The number of code points codePoints can hold.
    /// @param mode         What to do with ill-formed input.
    /// @param neededOut    Receives the number of code points the whole
    ///                     conversion needs, which may exceed capacity. In
    ///                     strict mode, on an error, the ones before it.
    /// @return `muni_success`; `muni_errorCapacity` when the output does
    ///         not fit (the code points that fit are written); in strict
    ///         mode the first UTF-8 error and its offset;
    ///         `muni_errorInvalid` for a NULL pointer with a nonzero
    ///         length.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_NODISCARD MUNI_API muniTextResult muniConvertUtf8ToUtf32(const char* bytes, size_t length,
                                                                  uint32_t* codePoints,
                                                                  size_t capacity,
                                                                  muniConvertMode mode,
                                                                  size_t* neededOut);

    /// Converts UTF-32 to UTF-8.
    ///
    /// @param codePoints   The text. May be NULL when length is 0.
    /// @param length       The number of code points.
    /// @param bytes        The output. May be NULL when capacity is 0.
    /// @param capacity     The number of bytes bytes can hold.
    /// @param mode         What to do with values that are no scalar
    ///                     value: surrogates and those past U+10FFFF.
    /// @param neededOut    Receives the number of bytes the whole
    ///                     conversion needs, which may exceed capacity. In
    ///                     strict mode, on an error, the bytes before it.
    /// @return `muni_success`; `muni_errorCapacity` when the output does
    ///         not fit (the bytes that fit are written); in strict mode
    ///         `muni_errorUtf32Value` and its offset; `muni_errorInvalid`
    ///         for a NULL pointer with a nonzero length.
    /// @par Thread safety
    /// Safe from any thread.
    MUNI_NODISCARD MUNI_API muniTextResult muniConvertUtf32ToUtf8(const uint32_t* codePoints,
                                                                  size_t length, char* bytes,
                                                                  size_t capacity,
                                                                  muniConvertMode mode,
                                                                  size_t* neededOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UNICODE_ENCODING_H
