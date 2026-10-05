// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text helpers the Apple backends share (apple_text.h).

#include "apple_text.h"

uint32_t mwinAppleBytesBefore(NSString* string, NSUInteger index)
{
    NSUInteger count = string.length;
    uint32_t bytes = 0;
    for (NSUInteger i = 0; i < index && i < count; i++)
    {
        unichar unit = [string characterAtIndex:i];
        bool pair = unit >= 0xD800 && unit <= 0xDBFF && i + 1 < count &&
                    [string characterAtIndex:i + 1] >= 0xDC00 &&
                    [string characterAtIndex:i + 1] <= 0xDFFF;
        bytes += unit < 0x80 ? 1u : (unit < 0x800 ? 2u : (pair ? 4u : 3u));
        i += pair ? 1 : 0;
    }
    return bytes;
}
