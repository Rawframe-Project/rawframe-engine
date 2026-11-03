// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The name of a document's copy on Android (android_name.h).

#include "android_name.h"

#include "utf8.h"

#include <stdio.h>
#include <string.h>

void mwinAndroidNameOf(const uint16_t* units, size_t count, char* name)
{
    size_t needed =
        mwinCutUtf16(units, units != nullptr ? count : 0, name, MWIN_ANDROID_NAME_BYTES);
    name[needed] = '\0';
    // A '/' would leave the folder, and a NUL end the name early.
    for (size_t i = 0; i < needed; i++)
    {
        if (name[i] == '/' || name[i] == '\0')
        {
            name[i] = '_';
        }
    }
    if (needed == 0 || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
    {
        (void)snprintf(name, MWIN_ANDROID_NAME_BYTES + 1, "Document");
    }
}
