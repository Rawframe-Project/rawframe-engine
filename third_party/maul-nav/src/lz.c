// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The LZ coder (mnav-0016): a greedy encoder that looks up the last place
// each four bytes were seen, and a decoder that checks every count
// against what is left.

#include "lz.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Matches are four bytes at least; offsets fit in two bytes.
#define MIN_MATCH  4
#define MAX_OFFSET 65535u
#define NO_PLACE   UINT32_MAX

size_t mnavLzBound(size_t size)
{
    return size + size / 255 + 16;
}

static uint32_t Read32(const uint8_t* p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t Hash(uint32_t v)
{
    return (v * 2654435761u) >> (32 - MNAV_LZ_HASH_BITS);
}

// Writes a count's bytes past the 15 its token holds.
static uint8_t* PutLength(uint8_t* out, size_t length)
{
    for (length -= 15; length >= 255; length -= 255)
    {
        *out++ = 255;
    }
    *out++ = (uint8_t)length;
    return out;
}

// Writes a sequence: the literals, then a match of length at an offset,
// or none for length 0.
static uint8_t* PutSequence(uint8_t* out, const uint8_t* literals, size_t count, size_t offset,
                            size_t length)
{
    size_t matchCode = length > 0 ? length - MIN_MATCH : 0;
    uint8_t* token = out++;
    *token = (uint8_t)((count < 15 ? count : 15) << 4 | (matchCode < 15 ? matchCode : 15));
    out = count >= 15 ? PutLength(out, count) : out;
    memcpy(out, literals, count);
    out += count;
    if (length > 0)
    {
        *out++ = (uint8_t)offset;
        *out++ = (uint8_t)(offset >> 8);
        out = matchCode >= 15 ? PutLength(out, matchCode) : out;
    }
    return out;
}

size_t mnavLzEncode(const uint8_t* in, size_t size, uint8_t* out, uint32_t* table)
{
    for (uint32_t k = 0; k < MNAV_LZ_TABLE; ++k)
    {
        table[k] = NO_PLACE;
    }
    uint8_t* start = out;
    size_t anchor = 0;
    size_t i = 0;
    while (i + MIN_MATCH <= size)
    {
        uint32_t v = Read32(&in[i]);
        uint32_t h = Hash(v);
        uint32_t place = table[h];
        table[h] = (uint32_t)i;
        if (place == NO_PLACE || i - place > MAX_OFFSET || Read32(&in[place]) != v)
        {
            i += 1;
            continue;
        }
        size_t length = MIN_MATCH;
        while (i + length < size && in[place + length] == in[i + length])
        {
            length += 1;
        }
        out = PutSequence(out, &in[anchor], i - anchor, i - place, length);
        i += length;
        anchor = i;
    }
    out = PutSequence(out, &in[anchor], size - anchor, 0, 0);
    return (size_t)(out - start);
}

// Reads a count continued past 15; false when the bytes end first or it
// passes most.
static bool GetLength(const uint8_t* in, size_t size, size_t* at, size_t* length, size_t most)
{
    uint8_t more = 255;
    while (more == 255)
    {
        if (*at >= size)
        {
            return false;
        }
        more = in[(*at)++];
        *length += more;
        if (*length > most)
        {
            return false;
        }
    }
    return true;
}

bool mnavLzDecode(const uint8_t* in, size_t size, uint8_t* out, size_t outSize)
{
    size_t at = 0;
    size_t written = 0;
    while (at < size)
    {
        uint8_t token = in[at++];
        size_t count = token >> 4;
        if ((count == 15 && !GetLength(in, size, &at, &count, outSize)) || count > size - at ||
            count > outSize - written)
        {
            return false;
        }
        memcpy(&out[written], &in[at], count);
        at += count;
        written += count;
        if (at == size)
        {
            // The last sequence: literals only.
            return (token & 15) == 0 && written == outSize;
        }
        if (size - at < 2)
        {
            return false;
        }
        size_t offset = (size_t)in[at] | (size_t)in[at + 1] << 8;
        at += 2;
        size_t length = (token & 15u);
        if ((length == 15 && !GetLength(in, size, &at, &length, outSize)) || offset == 0 ||
            offset > written || length + MIN_MATCH > outSize - written)
        {
            return false;
        }
        length += MIN_MATCH;
        // Byte by byte: a match may overlap the bytes it writes.
        for (size_t k = 0; k < length; ++k)
        {
            out[written + k] = out[written + k - offset];
        }
        written += length;
    }
    return written == outSize;
}
