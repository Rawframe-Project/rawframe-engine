// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The scale of Xft.dpi in the root window's resources.

#include "x11_resources.h"

#include <string.h>

// The pixels per inch of a scale of 1.
#define BASE_DPI 96.0f

// The number after "Xft.dpi:" in a resource string, or 0.
static float ParseDpi(const char* text, size_t length)
{
    static const char key[] = "Xft.dpi:";
    size_t keyLength = sizeof(key) - 1;
    for (size_t at = 0; at + keyLength <= length; at++)
    {
        if ((at > 0 && text[at - 1] != '\n') || memcmp(text + at, key, keyLength) != 0)
        {
            continue;
        }
        size_t i = at + keyLength;
        while (i < length && (text[i] == ' ' || text[i] == '\t'))
        {
            i++;
        }
        float value = 0.0f;
        // The weight of the next fraction digit, 0 before the point.
        float fraction = 0.0f;
        for (; i < length && ((text[i] >= '0' && text[i] <= '9') || text[i] == '.'); i++)
        {
            if (text[i] == '.')
            {
                if (fraction > 0.0f)
                {
                    break;
                }
                fraction = 0.1f;
                continue;
            }
            float digit = (float)(text[i] - '0');
            value = fraction > 0.0f ? value + digit * fraction : value * 10.0f + digit;
            fraction /= 10.0f;
        }
        return value;
    }
    return 0.0f;
}

float mwinX11ScaleOfResources(const char* text, size_t length)
{
    float dpi = ParseDpi(text, length);
    return dpi >= BASE_DPI / 4.0f && dpi <= BASE_DPI * 8.0f ? dpi / BASE_DPI : 1.0f;
}
