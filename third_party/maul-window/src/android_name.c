// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The name of a document's copy on Android (android_name.h).

#include "android_name.h"

#include "maul-unicode/encoding.h"

#include <stdio.h>
#include <string.h>

void mwinAndroidNameOf(const uint16_t* units, size_t count, char* name)
{
    size_t taken = units != nullptr ? count : 0;
    size_t needed = 0;
    // Units dropped from the end until the rest fits, never half a pair.
    for (;;)
    {
        (void)muniConvertUtf16ToUtf8(units, taken, nullptr, 0, muni_convertReplace, &needed);
        if (needed <= MWIN_ANDROID_NAME_BYTES || taken == 0)
        {
            break;
        }
        taken -= taken >= 2 && units[taken - 1] >= 0xDC00 && units[taken - 1] <= 0xDFFF ? 2 : 1;
    }
    (void)muniConvertUtf16ToUtf8(units, taken, name, MWIN_ANDROID_NAME_BYTES, muni_convertReplace,
                                 &needed);
    name[needed] = '\0';
    if (needed == 0 || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
    {
        (void)snprintf(name, MWIN_ANDROID_NAME_BYTES + 1, "Document");
    }
    for (char* at = strchr(name, '/'); at != nullptr; at = strchr(at, '/'))
    {
        *at = '_';
    }
}
