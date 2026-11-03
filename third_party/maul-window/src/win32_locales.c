// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Windows' preferred languages handed to the context (win32_locales.h).

#include "win32_locales.h"

#include "maul-unicode/encoding.h"

void mwinWin32TakeLocales(mwinContext* context, const uint16_t* units, size_t length, char* text,
                          uint64_t nowNs)
{
    size_t bytes = 0;
    muniTextResult converted = muniConvertUtf16ToUtf8(units, length, muni_convertReplace, text,
                                                      context->limits.localeBytes + 1u, &bytes);
    if (converted.status == muni_success)
    {
        (void)mwinSetLocales(context, text, bytes, nowNs);
    }
}
