// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Reading and framing tagged fields. Every size is worked out in 64 bits
// from a 32-bit length and an offset within the file, so none overflows.

#include "file_fields.h"

#include <string.h>

static uint32_t U32(const uint8_t* at)
{
    return (uint32_t)at[0] | (uint32_t)at[1] << 8 | (uint32_t)at[2] << 16 | (uint32_t)at[3] << 24;
}

static void Put32(uint8_t* at, uint32_t value)
{
    at[0] = (uint8_t)value;
    at[1] = (uint8_t)(value >> 8);
    at[2] = (uint8_t)(value >> 16);
    at[3] = (uint8_t)(value >> 24);
}

uint64_t maudFieldSize(uint64_t length)
{
    return MAUD_FIELD_FRAME + (length + MAUD_FIELD_ALIGN - 1) / MAUD_FIELD_ALIGN * MAUD_FIELD_ALIGN;
}

void maudPutFieldFrame(uint8_t* out, uint32_t tag, uint32_t length)
{
    Put32(out, tag);
    Put32(out + 4, length);
    uint64_t padded = maudFieldSize(length) - MAUD_FIELD_FRAME;
    memset(out + MAUD_FIELD_FRAME + length, 0, (size_t)(padded - length));
}

maudResult maudReadFields(const uint8_t* data, size_t count, size_t first, const uint32_t* tags,
                          uint32_t tagCount, maudField* fields)
{
    for (uint32_t i = 0; i < tagCount; ++i)
    {
        fields[i] = (maudField){0};
    }
    uint64_t at = first;
    while (at < count)
    {
        if (count - at < MAUD_FIELD_FRAME)
        {
            return maud_errorFormat;
        }
        uint32_t tag = U32(data + at);
        uint32_t length = U32(data + at + 4);
        uint32_t known = 0;
        while (known < tagCount && tags[known] != tag)
        {
            ++known;
        }
        if (known == tagCount)
        {
            return maud_errorVersion;
        }
        uint64_t size = maudFieldSize(length);
        if (fields[known].present || size > count - at)
        {
            return maud_errorFormat;
        }
        const uint8_t* payload = data + at + MAUD_FIELD_FRAME;
        for (uint64_t pad = length; pad < size - MAUD_FIELD_FRAME; ++pad)
        {
            if (payload[pad] != 0)
            {
                return maud_errorFormat;
            }
        }
        fields[known] = (maudField){.bytes = payload, .length = length, .present = true};
        at += size;
    }
    return maud_success;
}
