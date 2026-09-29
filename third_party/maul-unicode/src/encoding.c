// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Validation, decoding, encoding and conversion of UTF-8, UTF-16 and
// UTF-32. Every loop advances by at least one code unit per step, so
// hostile input costs time proportional to its length and nothing more.

#include "encoding.h"

#include "maul-unicode/encoding.h"

#include <string.h>

static bool IsHighSurrogate(uint32_t unit)
{
    return unit >= 0xD800 && unit <= 0xDBFF;
}

static bool IsLowSurrogate(uint32_t unit)
{
    return unit >= 0xDC00 && unit <= 0xDFFF;
}

// The length of the run of ASCII bytes at the start of bytes, found eight
// bytes at a time.
static size_t AsciiPrefix(const uint8_t* bytes, size_t length)
{
    size_t offset = 0;
    while (length - offset >= 8)
    {
        uint64_t word;
        memcpy(&word, bytes + offset, sizeof(word));
        if ((word & 0x8080808080808080ull) != 0)
        {
            break;
        }
        offset += 8;
    }
    while (offset < length && bytes[offset] < 0x80)
    {
        offset += 1;
    }
    return offset;
}

// Validation runs a shift-based DFA over byte classes: a class's row
// holds, at the bit offset of each state, the offset of the next state,
// so each byte costs one shift, with no branch. Only a run that ends in
// an error goes through the exact decoder, which names the error.
enum
{
    Accept = 0,
    Reject = 6,
    Need1 = 12, // one continuation byte, 0x80 to 0xBF, is missing
    Need2 = 18,
    Need3 = 24,
    AfterE0 = 30, // a continuation from 0xA0 to 0xBF, then one more
    AfterEd = 36, // 0x80 to 0x9F, then one more
    AfterF0 = 42, // 0x90 to 0xBF, then two more
    AfterF4 = 48, // 0x80 to 0x8F, then two more
};

#define ROW(accept, need1, need2, need3, afterE0, afterEd, afterF0, afterF4)                       \
    ((uint64_t)(accept) << Accept | (uint64_t)Reject << Reject | (uint64_t)(need1) << Need1 |      \
     (uint64_t)(need2) << Need2 | (uint64_t)(need3) << Need3 | (uint64_t)(afterE0) << AfterE0 |    \
     (uint64_t)(afterEd) << AfterEd | (uint64_t)(afterF0) << AfterF0 |                             \
     (uint64_t)(afterF4) << AfterF4)
#define LEAD(next) ROW(next, Reject, Reject, Reject, Reject, Reject, Reject, Reject)

static const uint64_t s_rows[12] = {
    LEAD(Accept),                                                    // 0x00 to 0x7F
    ROW(Reject, Accept, Need1, Need2, Reject, Need1, Reject, Need2), // 0x80 to 0x8F
    ROW(Reject, Accept, Need1, Need2, Reject, Need1, Need2, Reject), // 0x90 to 0x9F
    ROW(Reject, Accept, Need1, Need2, Need1, Reject, Need2, Reject), // 0xA0 to 0xBF
    LEAD(Need1),                                                     // 0xC2 to 0xDF
    LEAD(AfterE0),                                                   // 0xE0
    LEAD(Need2),                                                     // 0xE1 to 0xEC, 0xEE, 0xEF
    LEAD(AfterEd),                                                   // 0xED
    LEAD(AfterF0),                                                   // 0xF0
    LEAD(Need3),                                                     // 0xF1 to 0xF3
    LEAD(AfterF4),                                                   // 0xF4
    LEAD(Reject),                                                    // 0xC0, 0xC1, 0xF5 to 0xFF
};

// The class of each byte: its row in s_rows.
static const uint8_t s_classes[256] = {
    0,  0,  0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 0x00
    0,  0,  0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 0x10
    0,  0,  0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 0x20
    0,  0,  0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 0x30
    0,  0,  0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 0x40
    0,  0,  0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 0x50
    0,  0,  0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 0x60
    0,  0,  0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 0x70
    1,  1,  1, 1, 1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  // 0x80
    2,  2,  2, 2, 2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  // 0x90
    3,  3,  3, 3, 3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  // 0xA0
    3,  3,  3, 3, 3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  // 0xB0
    11, 11, 4, 4, 4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  // 0xC0
    4,  4,  4, 4, 4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  // 0xD0
    5,  6,  6, 6, 6,  6,  6,  6,  6,  6,  6,  6,  6,  7,  6,  6,  // 0xE0
    8,  9,  9, 9, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, // 0xF0
};

// The bytes the DFA takes between checks for an error.
#define CHUNK 32

// The first error at or after start, a code point boundary, by the exact
// decoder.
static muniTextResult FindError(const uint8_t* text, size_t length, size_t start)
{
    size_t offset = start;
    while (offset < length)
    {
        uint32_t codePoint;
        size_t size;
        muniResult status = muniStepUtf8(text + offset, length - offset, &codePoint, &size);
        if (status != muni_success)
        {
            return (muniTextResult){status, offset};
        }
        offset += size;
    }
    return (muniTextResult){muni_success, length};
}

muniTextResult muniValidateUtf8(const char* bytes, size_t length)
{
    if (bytes == nullptr && length != 0)
    {
        return (muniTextResult){muni_errorInvalid, 0};
    }
    const uint8_t* text = (const uint8_t*)bytes;
    uint64_t state = Accept;
    size_t boundary = 0; // where the DFA last stood between code points
    size_t offset = 0;
    while (offset < length)
    {
        if ((state & 63) == Accept)
        {
            boundary = offset;
            offset += AsciiPrefix(text + offset, length - offset);
        }
        size_t end = length - offset > CHUNK ? offset + CHUNK : length;
        for (; offset < end; offset++)
        {
            // Only the low six bits of the shift count matter, which the
            // shift instructions of x86 and ARM take for free.
            state = s_rows[s_classes[text[offset]]] >> (state & 63);
        }
        if ((state & 63) == Reject)
        {
            return FindError(text, length, boundary);
        }
    }
    if ((state & 63) != Accept)
    {
        return FindError(text, length, boundary);
    }
    return (muniTextResult){muni_success, length};
}

muniTextResult muniValidateUtf16(const uint16_t* units, size_t length)
{
    if (units == nullptr && length != 0)
    {
        return (muniTextResult){muni_errorInvalid, 0};
    }
    size_t offset = 0;
    while (offset < length)
    {
        uint32_t unit = units[offset];
        if (IsHighSurrogate(unit) && offset + 1 < length && IsLowSurrogate(units[offset + 1]))
        {
            offset += 2;
            continue;
        }
        if (IsHighSurrogate(unit) || IsLowSurrogate(unit))
        {
            return (muniTextResult){muni_errorUtf16Surrogate, offset};
        }
        offset += 1;
    }
    return (muniTextResult){muni_success, length};
}

muniResult muniDecodeUtf8(const char* bytes, size_t length, uint32_t* codePointOut, size_t* sizeOut)
{
    if (bytes == nullptr || length == 0 || codePointOut == nullptr || sizeOut == nullptr)
    {
        return muni_errorInvalid;
    }
    return muniStepUtf8((const uint8_t*)bytes, length, codePointOut, sizeOut);
}

// Writes the UTF-8 form of a scalar value into four bytes and returns its
// length.
static size_t EncodeUtf8Unchecked(uint32_t codePoint, uint8_t* bytes)
{
    if (codePoint < 0x80)
    {
        bytes[0] = (uint8_t)codePoint;
        return 1;
    }
    if (codePoint < 0x800)
    {
        bytes[0] = (uint8_t)(0xC0 | (codePoint >> 6));
        bytes[1] = (uint8_t)(0x80 | (codePoint & 0x3F));
        return 2;
    }
    if (codePoint < 0x10000)
    {
        bytes[0] = (uint8_t)(0xE0 | (codePoint >> 12));
        bytes[1] = (uint8_t)(0x80 | ((codePoint >> 6) & 0x3F));
        bytes[2] = (uint8_t)(0x80 | (codePoint & 0x3F));
        return 3;
    }
    bytes[0] = (uint8_t)(0xF0 | (codePoint >> 18));
    bytes[1] = (uint8_t)(0x80 | ((codePoint >> 12) & 0x3F));
    bytes[2] = (uint8_t)(0x80 | ((codePoint >> 6) & 0x3F));
    bytes[3] = (uint8_t)(0x80 | (codePoint & 0x3F));
    return 4;
}

muniResult muniEncodeUtf8(uint32_t codePoint, char* bytesOut, size_t* sizeOut)
{
    if (bytesOut == nullptr || sizeOut == nullptr || codePoint > 0x10FFFF ||
        (codePoint >= 0xD800 && codePoint <= 0xDFFF))
    {
        return muni_errorInvalid;
    }
    *sizeOut = EncodeUtf8Unchecked(codePoint, (uint8_t*)bytesOut);
    return muni_success;
}

// The next code point of a UTF-8 text for a conversion: decodes one
// sequence, and in replace mode turns an error into U+FFFD.
static muniResult NextFromUtf8(const uint8_t* text, size_t length, size_t offset,
                               muniConvertMode mode, uint32_t* codePointOut, size_t* sizeOut)
{
    muniResult status = muniStepUtf8(text + offset, length - offset, codePointOut, sizeOut);
    return status != muni_success && mode == muni_convertReplace ? muni_success : status;
}

static muniTextResult Finish(size_t needed, size_t capacity, size_t length, size_t* neededOut)
{
    *neededOut = needed;
    return (muniTextResult){needed > capacity ? muni_errorCapacity : muni_success, length};
}

static bool BadArguments(const void* input, size_t length, const void* output, size_t capacity,
                         muniConvertMode mode, const size_t* neededOut)
{
    return (input == nullptr && length != 0) || (output == nullptr && capacity != 0) ||
           neededOut == nullptr || mode > muni_convertReplace;
}

muniTextResult muniConvertUtf8ToUtf16(const char* bytes, size_t length, uint16_t* units,
                                      size_t capacity, muniConvertMode mode, size_t* neededOut)
{
    if (BadArguments(bytes, length, units, capacity, mode, neededOut))
    {
        return (muniTextResult){muni_errorInvalid, 0};
    }
    const uint8_t* text = (const uint8_t*)bytes;
    size_t needed = 0;
    size_t offset = 0;
    while (offset < length)
    {
        uint32_t codePoint;
        size_t size;
        muniResult status = NextFromUtf8(text, length, offset, mode, &codePoint, &size);
        if (status != muni_success)
        {
            *neededOut = needed;
            return (muniTextResult){status, offset};
        }
        if (codePoint >= 0x10000)
        {
            if (needed + 2 <= capacity)
            {
                units[needed] = (uint16_t)(0xD800 + ((codePoint - 0x10000) >> 10));
                units[needed + 1] = (uint16_t)(0xDC00 + ((codePoint - 0x10000) & 0x3FF));
            }
            needed += 2;
        }
        else
        {
            if (needed < capacity)
            {
                units[needed] = (uint16_t)codePoint;
            }
            needed += 1;
        }
        offset += size;
    }
    return Finish(needed, capacity, length, neededOut);
}

muniTextResult muniConvertUtf16ToUtf8(const uint16_t* units, size_t length, char* bytes,
                                      size_t capacity, muniConvertMode mode, size_t* neededOut)
{
    if (BadArguments(units, length, bytes, capacity, mode, neededOut))
    {
        return (muniTextResult){muni_errorInvalid, 0};
    }
    size_t needed = 0;
    size_t offset = 0;
    while (offset < length)
    {
        uint32_t codePoint = units[offset];
        size_t size = 1;
        if (IsHighSurrogate(codePoint) && offset + 1 < length && IsLowSurrogate(units[offset + 1]))
        {
            codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (units[offset + 1] - 0xDC00u);
            size = 2;
        }
        else if (IsHighSurrogate(codePoint) || IsLowSurrogate(codePoint))
        {
            if (mode == muni_convertStrict)
            {
                *neededOut = needed;
                return (muniTextResult){muni_errorUtf16Surrogate, offset};
            }
            codePoint = MUNI_REPLACEMENT_CHARACTER;
        }
        uint8_t encoded[4];
        size_t encodedSize = EncodeUtf8Unchecked(codePoint, encoded);
        if (needed + encodedSize <= capacity)
        {
            memcpy(bytes + needed, encoded, encodedSize);
        }
        needed += encodedSize;
        offset += size;
    }
    return Finish(needed, capacity, length, neededOut);
}

muniTextResult muniConvertUtf8ToUtf32(const char* bytes, size_t length, uint32_t* codePoints,
                                      size_t capacity, muniConvertMode mode, size_t* neededOut)
{
    if (BadArguments(bytes, length, codePoints, capacity, mode, neededOut))
    {
        return (muniTextResult){muni_errorInvalid, 0};
    }
    const uint8_t* text = (const uint8_t*)bytes;
    size_t needed = 0;
    size_t offset = 0;
    while (offset < length)
    {
        uint32_t codePoint;
        size_t size;
        muniResult status = NextFromUtf8(text, length, offset, mode, &codePoint, &size);
        if (status != muni_success)
        {
            *neededOut = needed;
            return (muniTextResult){status, offset};
        }
        if (needed < capacity)
        {
            codePoints[needed] = codePoint;
        }
        needed += 1;
        offset += size;
    }
    return Finish(needed, capacity, length, neededOut);
}

muniTextResult muniConvertUtf32ToUtf8(const uint32_t* codePoints, size_t length, char* bytes,
                                      size_t capacity, muniConvertMode mode, size_t* neededOut)
{
    if (BadArguments(codePoints, length, bytes, capacity, mode, neededOut))
    {
        return (muniTextResult){muni_errorInvalid, 0};
    }
    size_t needed = 0;
    for (size_t offset = 0; offset < length; offset++)
    {
        uint32_t codePoint = codePoints[offset];
        if (codePoint > 0x10FFFF || IsHighSurrogate(codePoint) || IsLowSurrogate(codePoint))
        {
            if (mode == muni_convertStrict)
            {
                *neededOut = needed;
                return (muniTextResult){muni_errorUtf32Value, offset};
            }
            codePoint = MUNI_REPLACEMENT_CHARACTER;
        }
        uint8_t encoded[4];
        size_t encodedSize = EncodeUtf8Unchecked(codePoint, encoded);
        if (needed + encodedSize <= capacity)
        {
            memcpy(bytes + needed, encoded, encodedSize);
        }
        needed += encodedSize;
    }
    return Finish(needed, capacity, length, neededOut);
}
