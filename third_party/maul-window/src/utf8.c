// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text from other programs, made well-formed.

#include "utf8.h"

#include "maul-unicode/encoding.h"

#include <string.h>

size_t mwinRepairUtf8(const char* bytes, size_t length, char* out)
{
    static const char replacement[3] = {'\xEF', '\xBF', '\xBD'};
    size_t written = 0;
    size_t at = 0;
    while (at < length)
    {
        size_t run = muniValidateUtf8(bytes + at, length - at).offset;
        if (out != nullptr && run > 0)
        {
            memcpy(out + written, bytes + at, run);
        }
        written += run;
        at += run;
        if (at == length)
        {
            break;
        }
        uint32_t codePoint = 0;
        size_t size = 1;
        (void)muniDecodeUtf8(bytes + at, length - at, &codePoint, &size);
        if (out != nullptr)
        {
            memcpy(out + written, replacement, sizeof(replacement));
        }
        written += sizeof(replacement);
        at += size;
    }
    return written;
}

mwinResult mwinCheckText(const char* text, size_t length, size_t limit, mwinResult over, bool nul)
{
    if (text == nullptr && length != 0)
    {
        return mwin_errorInvalid;
    }
    if (length > limit)
    {
        return over;
    }
    bool valid = length == 0 || (muniValidateUtf8(text, length).status == muni_success &&
                                 (nul || memchr(text, '\0', length) == nullptr));
    return valid ? mwin_success : mwin_errorInvalid;
}

size_t mwinCutUtf16(const uint16_t* units, size_t count, char* out, size_t capacity)
{
    // Each unit takes at least a byte, so one past capacity never fits,
    // and a high surrogate cut from its pair there, as U+FFFD, never
    // does either.
    size_t taken = count < capacity ? count : capacity;
    size_t written = 0;
    muniTextResult result =
        muniConvertUtf16ToUtf8(units, taken, muni_convertReplace, out, capacity, &written);
    if (result.status == muni_errorCapacity)
    {
        // Out already holds the units before the stop; this counts them.
        (void)muniConvertUtf16ToUtf8(units, result.offset, muni_convertReplace, out, capacity,
                                     &written);
    }
    return written;
}

size_t mwinBytesBefore(const char* text, size_t length, muniTextUnit unit, size_t count)
{
    size_t bytes = 0;
    (void)muniMeasureUtf8(text, length, unit, SIZE_MAX, count, &bytes, nullptr);
    return bytes;
}
