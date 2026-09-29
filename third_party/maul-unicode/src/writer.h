// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// UTF-8 output into a caller buffer that may be too small: bytes that
// fit are written, and every byte is counted, so the caller learns the
// size the whole result needs.

#ifndef MAUL_UNICODE_SRC_WRITER_H
#define MAUL_UNICODE_SRC_WRITER_H

#include <stddef.h>
#include <stdint.h>

typedef struct muniWriter
{
    char* output;
    size_t capacity;
    size_t needed; // bytes the whole output needs so far
} muniWriter;

// Writes a Unicode scalar value.
static inline void muniWriterPut(muniWriter* writer, uint32_t codePoint)
{
    uint8_t bytes[4];
    size_t size;
    if (codePoint < 0x80)
    {
        bytes[0] = (uint8_t)codePoint;
        size = 1;
    }
    else if (codePoint < 0x800)
    {
        bytes[0] = (uint8_t)(0xC0 | codePoint >> 6);
        bytes[1] = (uint8_t)(0x80 | (codePoint & 0x3F));
        size = 2;
    }
    else if (codePoint < 0x10000)
    {
        bytes[0] = (uint8_t)(0xE0 | codePoint >> 12);
        bytes[1] = (uint8_t)(0x80 | (codePoint >> 6 & 0x3F));
        bytes[2] = (uint8_t)(0x80 | (codePoint & 0x3F));
        size = 3;
    }
    else
    {
        bytes[0] = (uint8_t)(0xF0 | codePoint >> 18);
        bytes[1] = (uint8_t)(0x80 | (codePoint >> 12 & 0x3F));
        bytes[2] = (uint8_t)(0x80 | (codePoint >> 6 & 0x3F));
        bytes[3] = (uint8_t)(0x80 | (codePoint & 0x3F));
        size = 4;
    }
    for (size_t i = 0; i < size; i++)
    {
        if (writer->needed + i < writer->capacity)
        {
            writer->output[writer->needed + i] = (char)bytes[i];
        }
    }
    writer->needed += size;
}

#endif // MAUL_UNICODE_SRC_WRITER_H
