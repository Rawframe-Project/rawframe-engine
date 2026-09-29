// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UTF-8 decoding step every module that reads text shares. It
// follows the Unicode Standard's table of well-formed UTF-8 byte
// sequences: the second byte's allowed range depends on the lead byte
// (A0..BF after E0, 80..9F after ED, 90..BF after F0, 80..8F after F4),
// every other continuation byte is 80..BF.

#ifndef MAUL_UNICODE_SRC_ENCODING_H
#define MAUL_UNICODE_SRC_ENCODING_H

#include "maul-unicode/base.h"

#define MUNI_REPLACEMENT_CHARACTER 0xFFFDu

// How a lead byte continues: the number of continuation bytes, the
// allowed range of the first one, the error for a first byte below and
// above that range, and the lead's payload bits.
typedef struct muniUtf8Lead
{
    uint32_t continuations;
    uint8_t low;
    uint8_t high;
    muniResult belowLow;
    muniResult aboveHigh;
    uint32_t payload;
} muniUtf8Lead;

// Classifies a non-ASCII lead byte. Returns the error for a byte that
// cannot lead a sequence, or success.
static inline muniResult muniClassifyUtf8Lead(uint8_t byte, muniUtf8Lead* leadOut)
{
    *leadOut = (muniUtf8Lead){
        1, 0x80, 0xBF, muni_errorUtf8Continuation, muni_errorUtf8Continuation, byte & 0x1Fu};
    if (byte < 0xC0)
    {
        return muni_errorUtf8Lead;
    }
    if (byte < 0xC2)
    {
        return muni_errorUtf8Overlong;
    }
    if (byte < 0xE0)
    {
        return muni_success;
    }
    if (byte < 0xF0)
    {
        leadOut->continuations = 2;
        leadOut->payload = byte & 0x0Fu;
        if (byte == 0xE0)
        {
            leadOut->low = 0xA0;
            leadOut->belowLow = muni_errorUtf8Overlong;
        }
        else if (byte == 0xED)
        {
            leadOut->high = 0x9F;
            leadOut->aboveHigh = muni_errorUtf8Surrogate;
        }
        return muni_success;
    }
    if (byte < 0xF5)
    {
        leadOut->continuations = 3;
        leadOut->payload = byte & 0x07u;
        if (byte == 0xF0)
        {
            leadOut->low = 0x90;
            leadOut->belowLow = muni_errorUtf8Overlong;
        }
        else if (byte == 0xF4)
        {
            leadOut->high = 0x8F;
            leadOut->aboveHigh = muni_errorUtf8TooLarge;
        }
        return muni_success;
    }
    return byte < 0xF8 ? muni_errorUtf8TooLarge : muni_errorUtf8Lead;
}

// Decodes the sequence at bytes[0], with length >= 1 bytes available. On
// success writes the code point and the sequence length; on an error
// writes U+FFFD and the length of the maximal ill-formed subpart, which
// is at least 1, so a caller that skips it always makes progress.
static inline muniResult muniStepUtf8(const uint8_t* bytes, size_t length, uint32_t* codePointOut,
                                      size_t* sizeOut)
{
    uint8_t first = bytes[0];
    *codePointOut = MUNI_REPLACEMENT_CHARACTER;
    *sizeOut = 1;
    if (first < 0x80)
    {
        *codePointOut = first;
        return muni_success;
    }
    muniUtf8Lead lead;
    muniResult status = muniClassifyUtf8Lead(first, &lead);
    if (status != muni_success)
    {
        return status;
    }
    uint32_t codePoint = lead.payload;
    for (size_t i = 1; i <= lead.continuations; i++)
    {
        *sizeOut = i;
        if (i >= length)
        {
            return muni_errorUtf8Truncated;
        }
        uint8_t byte = bytes[i];
        uint8_t low = i == 1 ? lead.low : 0x80;
        uint8_t high = i == 1 ? lead.high : 0xBF;
        if (byte < low || byte > high)
        {
            bool continuation = byte >= 0x80 && byte <= 0xBF;
            if (i == 1 && continuation)
            {
                return byte < low ? lead.belowLow : lead.aboveHigh;
            }
            return muni_errorUtf8Continuation;
        }
        codePoint = (codePoint << 6) | (byte & 0x3Fu);
    }
    *codePointOut = codePoint;
    *sizeOut = lead.continuations + 1;
    return muni_success;
}

#endif // MAUL_UNICODE_SRC_ENCODING_H
