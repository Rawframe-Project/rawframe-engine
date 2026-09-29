// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Debug labels, checked as hostile input: every byte is read at most
// once, and never past the length.

#include "label.h"

// The length of the well-formed sequence at bytes, or 0 for none.
static size_t SequenceLength(const unsigned char* bytes, size_t left)
{
    unsigned char lead = bytes[0];
    if (lead >= 0x01 && lead <= 0x7F)
    {
        return 1;
    }
    // The second byte's range after each lead, per the table of well-formed byte sequences.
    size_t length = 0;
    unsigned char low = 0x80;
    unsigned char high = 0xBF;
    if (lead >= 0xC2 && lead <= 0xDF)
    {
        length = 2;
    }
    else if (lead >= 0xE0 && lead <= 0xEF)
    {
        length = 3;
        low = lead == 0xE0 ? 0xA0 : 0x80;
        high = lead == 0xED ? 0x9F : 0xBF;
    }
    else if (lead >= 0xF0 && lead <= 0xF4)
    {
        length = 4;
        low = lead == 0xF0 ? 0x90 : 0x80;
        high = lead == 0xF4 ? 0x8F : 0xBF;
    }
    if (length == 0 || length > left || bytes[1] < low || bytes[1] > high)
    {
        return 0;
    }
    for (size_t i = 2; i < length; ++i)
    {
        if (bytes[i] < 0x80 || bytes[i] > 0xBF)
        {
            return 0;
        }
    }
    return length;
}

bool mrhiIsLabelValid(const char* label, size_t length)
{
    if (length == 0)
    {
        return true;
    }
    return label != nullptr && length <= MRHI_LABEL_BYTES && mrhiIsTextValid(label, length);
}

bool mrhiIsTextValid(const char* text, size_t length)
{
    const unsigned char* bytes = (const unsigned char*)text;
    size_t at = 0;
    while (at < length)
    {
        size_t step = SequenceLength(bytes + at, length - at);
        if (step == 0)
        {
            return false;
        }
        at += step;
    }
    return true;
}
