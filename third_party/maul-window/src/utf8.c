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
