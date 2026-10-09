// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// DEFLATE (RFC 1951) in a zlib stream (RFC 1950). Huffman codes are
// canonical: a code's symbol is found from the counts of each length, as
// Mark Adler's puff does, after a table of the codes of up to FAST_BITS
// bits, indexed by the stream's next bits, finds most at once. Bits come
// least significant first; a code's first bit is its most significant.

#include "inflate.h"

#include <stdbool.h>
#include <string.h>

enum
{
    MAX_BITS = 15,
    MAX_LITERALS = 288,
    MAX_DISTANCES = 30,
    FAST_BITS = 9,
    // A table entry: its symbol above, its length in the low four bits;
    // 0 for a code longer than FAST_BITS or none.
    LENGTH_MASK = 15
};

typedef struct Huffman
{
    uint16_t count[MAX_BITS + 1];
    uint16_t symbol[MAX_LITERALS];
    uint16_t fast[1u << FAST_BITS];
} Huffman;

typedef struct Stream
{
    const uint8_t* in;
    size_t inSize;
    size_t inAt;
    uint64_t bits;
    uint32_t bitCount;
    uint8_t* out;
    size_t outSize;
    size_t outAt;
    // Set when the input ran out or a code was wrong.
    bool failed;
} Stream;

// Holds at least n bits, or as many as the input has left.
static void Fill(Stream* s, uint32_t n)
{
    while (s->bitCount < n && s->inAt < s->inSize)
    {
        s->bits |= (uint64_t)s->in[s->inAt++] << s->bitCount;
        s->bitCount += 8;
    }
}

static uint32_t Bits(Stream* s, uint32_t n)
{
    Fill(s, n);
    if (s->bitCount < n)
    {
        s->failed = true;
        return 0;
    }
    uint32_t value = (uint32_t)(s->bits & ((1u << n) - 1u));
    s->bits >>= n;
    s->bitCount -= n;
    return value;
}

// Builds a code from its symbols' lengths; false for one with more codes
// of a length than there are; an incomplete code is kept, its missing
// codes failing when met.
static bool Build(Huffman* h, const uint8_t* lengths, uint32_t n)
{
    memset(h->count, 0, sizeof h->count);
    memset(h->fast, 0, sizeof h->fast);
    for (uint32_t i = 0; i < n; i++)
    {
        h->count[lengths[i]]++;
    }
    h->count[0] = 0;
    int left = 1;
    for (uint32_t len = 1; len <= MAX_BITS; len++)
    {
        left = left * 2 - h->count[len];
        if (left < 0)
        {
            return false;
        }
    }
    uint16_t offsets[MAX_BITS + 2];
    uint16_t next[MAX_BITS + 1];
    offsets[1] = 0;
    uint32_t code = 0;
    for (uint32_t len = 1; len <= MAX_BITS; len++)
    {
        offsets[len + 1] = (uint16_t)(offsets[len] + h->count[len]);
        code = (code + h->count[len - 1]) << 1;
        next[len] = (uint16_t)code;
    }
    for (uint32_t i = 0; i < n; i++)
    {
        uint32_t len = lengths[i];
        if (len == 0)
        {
            continue;
        }
        h->symbol[offsets[len]++] = (uint16_t)i;
        uint32_t c = next[len]++;
        if (len <= FAST_BITS)
        {
            // The code's bits reversed, as the stream gives them.
            uint32_t reversed = 0;
            for (uint32_t b = 0; b < len; b++)
            {
                reversed |= ((c >> b) & 1u) << (len - 1 - b);
            }
            for (uint32_t at = reversed; at < (1u << FAST_BITS); at += 1u << len)
            {
                h->fast[at] = (uint16_t)(i << 4 | len);
            }
        }
    }
    return true;
}

// The next symbol of a code; 0 and failed for none.
static uint32_t Decode(Stream* s, const Huffman* h)
{
    Fill(s, FAST_BITS);
    uint32_t entry = h->fast[s->bits & ((1u << FAST_BITS) - 1u)];
    uint32_t len = entry & LENGTH_MASK;
    if (len != 0 && len <= s->bitCount)
    {
        s->bits >>= len;
        s->bitCount -= len;
        return entry >> 4;
    }
    // Bit by bit: codes of a length are consecutive, after the shorter.
    int code = 0;
    int first = 0;
    int index = 0;
    for (uint32_t bits = 1; bits <= MAX_BITS; bits++)
    {
        code |= (int)Bits(s, 1);
        if (s->failed)
        {
            return 0;
        }
        int count = h->count[bits];
        if (code - count < first)
        {
            return h->symbol[index + (code - first)];
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    s->failed = true;
    return 0;
}

static const uint16_t LENGTH_BASE[29] = {3,  4,  5,  6,   7,   8,   9,   10,  11, 13,
                                         15, 17, 19, 23,  27,  31,  35,  43,  51, 59,
                                         67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t LENGTH_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                         2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t DISTANCE_BASE[30] = {
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t DISTANCE_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                           6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// Inflates one block's codes until its end.
static bool Codes(Stream* s, const Huffman* literals, const Huffman* distances)
{
    for (;;)
    {
        uint32_t symbol = Decode(s, literals);
        if (s->failed)
        {
            return false;
        }
        if (symbol < 256)
        {
            if (s->outAt >= s->outSize)
            {
                return false;
            }
            s->out[s->outAt++] = (uint8_t)symbol;
            continue;
        }
        if (symbol == 256)
        {
            return true;
        }
        symbol -= 257;
        if (symbol >= 29)
        {
            return false;
        }
        size_t length = LENGTH_BASE[symbol] + Bits(s, LENGTH_EXTRA[symbol]);
        uint32_t code = Decode(s, distances);
        if (s->failed || code >= MAX_DISTANCES)
        {
            return false;
        }
        size_t distance = DISTANCE_BASE[code] + Bits(s, DISTANCE_EXTRA[code]);
        if (s->failed || distance > s->outAt || length > s->outSize - s->outAt)
        {
            return false;
        }
        // Byte by byte: a copy may overlap what it makes.
        for (size_t i = 0; i < length; i++, s->outAt++)
        {
            s->out[s->outAt] = s->out[s->outAt - distance];
        }
    }
}

static bool Stored(Stream* s)
{
    // To the next whole byte.
    s->bits >>= s->bitCount % 8;
    s->bitCount -= s->bitCount % 8;
    uint32_t length = Bits(s, 16);
    uint32_t complement = Bits(s, 16);
    if (s->failed || length != (~complement & 0xFFFFu) || length > s->outSize - s->outAt)
    {
        return false;
    }
    for (; length > 0 && s->bitCount > 0; length--)
    {
        s->out[s->outAt++] = (uint8_t)Bits(s, 8);
    }
    if (length > s->inSize - s->inAt)
    {
        return false;
    }
    memcpy(s->out + s->outAt, s->in + s->inAt, length);
    s->outAt += length;
    s->inAt += length;
    return true;
}

static bool Fixed(Stream* s)
{
    uint8_t lengths[MAX_LITERALS + MAX_DISTANCES];
    for (uint32_t i = 0; i < MAX_LITERALS; i++)
    {
        lengths[i] = i < 144 ? 8 : (i < 256 ? 9 : (i < 280 ? 7 : 8));
    }
    for (uint32_t i = 0; i < MAX_DISTANCES; i++)
    {
        lengths[MAX_LITERALS + i] = 5;
    }
    Huffman literals;
    Huffman distances;
    return Build(&literals, lengths, MAX_LITERALS) &&
           Build(&distances, lengths + MAX_LITERALS, MAX_DISTANCES) &&
           Codes(s, &literals, &distances);
}

// The order the code length code's lengths come in.
static const uint8_t ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

// Reads the lengths of a dynamic block's two codes.
static bool ReadLengths(Stream* s, uint8_t* lengths, uint32_t total)
{
    uint32_t codes = Bits(s, 4) + 4;
    uint8_t codeLengths[19] = {0};
    for (uint32_t i = 0; i < codes; i++)
    {
        codeLengths[ORDER[i]] = (uint8_t)Bits(s, 3);
    }
    Huffman code;
    if (s->failed || !Build(&code, codeLengths, 19))
    {
        return false;
    }
    for (uint32_t at = 0; at < total;)
    {
        uint32_t symbol = Decode(s, &code);
        if (s->failed)
        {
            return false;
        }
        if (symbol < 16)
        {
            lengths[at++] = (uint8_t)symbol;
            continue;
        }
        if (symbol == 16 && at == 0)
        {
            return false;
        }
        uint8_t repeated = symbol == 16 ? lengths[at - 1] : 0;
        uint32_t times =
            symbol == 16 ? 3 + Bits(s, 2) : (symbol == 17 ? 3 + Bits(s, 3) : 11 + Bits(s, 7));
        if (s->failed || times > total - at)
        {
            return false;
        }
        memset(lengths + at, repeated, times);
        at += times;
    }
    return true;
}

static bool Dynamic(Stream* s)
{
    uint32_t literalCount = Bits(s, 5) + 257;
    uint32_t distanceCount = Bits(s, 5) + 1;
    uint8_t lengths[MAX_LITERALS + MAX_DISTANCES];
    // A code must end blocks.
    if (s->failed || literalCount > 286 || distanceCount > MAX_DISTANCES ||
        !ReadLengths(s, lengths, literalCount + distanceCount) || lengths[256] == 0)
    {
        return false;
    }
    Huffman literals;
    Huffman distances;
    return Build(&literals, lengths, literalCount) &&
           Build(&distances, lengths + literalCount, distanceCount) &&
           Codes(s, &literals, &distances);
}

static uint32_t Adler32(const uint8_t* data, size_t size)
{
    uint32_t a = 1;
    uint32_t b = 0;
    while (size > 0)
    {
        // The most bytes before b could pass 2^32.
        size_t run = size < 5552 ? size : 5552;
        for (size_t i = 0; i < run; i++)
        {
            a += data[i];
            b += a;
        }
        a %= 65521u;
        b %= 65521u;
        data += run;
        size -= run;
    }
    return b << 16 | a;
}

muiResult muiInflateZlib(const uint8_t* in, size_t inSize, uint8_t* out, size_t outSize)
{
    // A header of deflate with a window to 32 KiB, its check, no
    // dictionary.
    if (inSize < 2 || (in[0] & 15u) != 8 || (in[0] >> 4) > 7 ||
        ((uint32_t)in[0] << 8 | in[1]) % 31u != 0 || (in[1] & 0x20u) != 0)
    {
        return mui_errorFormat;
    }
    Stream s = {in, inSize, 2, 0, 0, out, outSize, 0, false};
    bool last = false;
    while (!last)
    {
        last = Bits(&s, 1) != 0;
        uint32_t type = Bits(&s, 2);
        bool ok = !s.failed && (type == 0   ? Stored(&s)
                                : type == 1 ? Fixed(&s)
                                : type == 2 ? Dynamic(&s)
                                            : false);
        if (!ok)
        {
            return mui_errorFormat;
        }
    }
    s.bits >>= s.bitCount % 8;
    s.bitCount -= s.bitCount % 8;
    uint32_t checksum = 0;
    for (int i = 0; i < 4; i++)
    {
        checksum = checksum << 8 | Bits(&s, 8);
    }
    return !s.failed && s.outAt == outSize && checksum == Adler32(out, outSize) ? mui_success
                                                                                : mui_errorFormat;
}
