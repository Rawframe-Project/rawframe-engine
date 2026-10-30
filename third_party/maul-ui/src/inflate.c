// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// DEFLATE (RFC 1951) in a zlib stream (RFC 1950). Huffman codes are
// canonical: a code's symbol is found from the counts of each length, as
// Mark Adler's puff does, after a table of the codes of up to FAST_BITS
// bits, indexed by the stream's next bits, finds most at once. Bits come
// least significant first; a code's first bit is its most significant.
// A fixed block's two codes are constant data, built once by
// tools/gen_inflate_tables.py as Build builds them.

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
    LENGTH_MASK = 15,
    // The most bits a literal or match takes: a 15-bit code, 5 extra
    // bits, a 15-bit distance code and 13 more.
    SYMBOL_BITS = 48
};

typedef struct Huffman
{
    uint16_t count[MAX_BITS + 1];
    uint16_t symbol[MAX_LITERALS];
    // The fast table's index bits: the longest code's length, at most
    // FAST_BITS, so a small code fills a small table.
    uint32_t fastMask;
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

// Holds at least n bits, or as many as the input has left. While eight
// bytes are left it loads them at once, least significant first on any
// target, and keeps as many whole bytes as fit; the last eight come a
// byte at a time. Bits above bitCount stay zero.
static void Fill(Stream* s, uint32_t n)
{
    if (s->bitCount < n && s->inSize - s->inAt >= 8)
    {
        uint64_t word;
        memcpy(&word, s->in + s->inAt, sizeof word);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        word = __builtin_bswap64(word);
#endif
        uint32_t bytes = (63 - s->bitCount) >> 3;
        s->bits |= word << s->bitCount;
        s->bitCount += bytes * 8;
        s->bits &= ((uint64_t)1 << s->bitCount) - 1; // bitCount is at most 63
        s->inAt += bytes;
        return;
    }
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
    uint32_t value = (uint32_t)(s->bits & (((uint64_t)1 << n) - 1u));
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
    for (uint32_t i = 0; i < n; i++)
    {
        h->count[lengths[i]]++;
    }
    h->count[0] = 0;
    uint32_t longest = MAX_BITS;
    while (longest > 1 && h->count[longest] == 0)
    {
        longest--;
    }
    h->fastMask = (1u << (longest < FAST_BITS ? longest : FAST_BITS)) - 1u;
    memset(h->fast, 0, (h->fastMask + 1u) * sizeof h->fast[0]);
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
            for (uint32_t at = reversed; at <= h->fastMask; at += 1u << len)
            {
                h->fast[at] = (uint16_t)(i << 4 | len);
            }
        }
    }
    return true;
}

// A code read bit by bit from the buffer's low bits, have of them:
// codes of a length are consecutive, after the shorter. Its symbol and
// its length in *used, or -1 when the bits run out first or no code is
// met.
static int32_t Walk(const Huffman* h, uint64_t bits, uint32_t have, uint32_t* used)
{
    int32_t code = 0;
    int32_t first = 0;
    int32_t index = 0;
    for (uint32_t len = 1; len <= MAX_BITS && len <= have; len++)
    {
        code |= (int32_t)((bits >> (len - 1)) & 1u);
        int32_t count = h->count[len];
        if (code - count < first)
        {
            *used = len;
            return h->symbol[index + (code - first)];
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

// The next symbol of a code; 0 and failed for none.
static uint32_t Decode(Stream* s, const Huffman* h)
{
    Fill(s, MAX_BITS);
    uint32_t entry = h->fast[s->bits & h->fastMask];
    uint32_t len = entry & LENGTH_MASK;
    if (len == 0 || len > s->bitCount)
    {
        int32_t symbol = Walk(h, s->bits, s->bitCount, &len);
        if (symbol < 0)
        {
            s->failed = true;
            return 0;
        }
        entry = (uint32_t)symbol << 4;
    }
    s->bits >>= len;
    s->bitCount -= len;
    return entry >> 4;
}

#include "generated/inflate_tables.h"

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

typedef enum Next
{
    NEXT_SYMBOL,
    NEXT_BLOCK,
    NEXT_FAILED
} Next;

// One literal, match or block end, every bound checked.
static Next Checked(Stream* s, const Huffman* literals, const Huffman* distances)
{
    uint32_t symbol = Decode(s, literals);
    if (s->failed)
    {
        return NEXT_FAILED;
    }
    if (symbol < 256)
    {
        if (s->outAt >= s->outSize)
        {
            return NEXT_FAILED;
        }
        s->out[s->outAt++] = (uint8_t)symbol;
        return NEXT_SYMBOL;
    }
    if (symbol == 256)
    {
        return NEXT_BLOCK;
    }
    symbol -= 257;
    if (symbol >= 29)
    {
        return NEXT_FAILED;
    }
    size_t length = LENGTH_BASE[symbol] + Bits(s, LENGTH_EXTRA[symbol]);
    uint32_t code = Decode(s, distances);
    if (s->failed || code >= MAX_DISTANCES)
    {
        return NEXT_FAILED;
    }
    size_t distance = DISTANCE_BASE[code] + Bits(s, DISTANCE_EXTRA[code]);
    if (s->failed || distance > s->outAt || length > s->outSize - s->outAt)
    {
        return NEXT_FAILED;
    }
    // Byte by byte: a copy may overlap what it makes.
    for (size_t i = 0; i < length; i++, s->outAt++)
    {
        s->out[s->outAt] = s->out[s->outAt - distance];
    }
    return NEXT_SYMBOL;
}

// The symbol's code from bits the buffer holds; -1 for none.
static int32_t Held(const Huffman* h, uint64_t* bits, uint32_t* bitCount)
{
    uint32_t entry = h->fast[*bits & h->fastMask];
    uint32_t len = entry & LENGTH_MASK;
    int32_t symbol = (int32_t)(entry >> 4);
    if (len == 0)
    {
        symbol = Walk(h, *bits, *bitCount, &len);
        if (symbol < 0)
        {
            return -1;
        }
    }
    *bits >>= len;
    *bitCount -= len;
    return symbol;
}

// Most of a block, as zlib's inflate_fast does it: while eight bytes of
// input are left one refill holds a symbol's longest form, so no read
// checks the input, and the state stays in locals. Everything else is
// checked in Checked's order, so a stream fails at the same symbol
// either way. NEXT_SYMBOL when the input's margin runs out, the stream
// at a symbol's start.
static Next Fast(Stream* s, const Huffman* literals, const Huffman* distances)
{
    const uint8_t* in = s->in;
    uint8_t* out = s->out;
    size_t inSize = s->inSize;
    size_t outSize = s->outSize;
    size_t inAt = s->inAt;
    size_t outAt = s->outAt;
    uint64_t bits = s->bits;
    uint32_t bitCount = s->bitCount;
    Next next = NEXT_SYMBOL;
    while (inSize - inAt >= 8)
    {
        if (bitCount < SYMBOL_BITS)
        {
            uint64_t word;
            memcpy(&word, in + inAt, sizeof word);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
            word = __builtin_bswap64(word);
#endif
            uint32_t bytes = (63 - bitCount) >> 3;
            bits = (bits | word << bitCount) & (((uint64_t)1 << (bitCount + bytes * 8)) - 1);
            bitCount += bytes * 8;
            inAt += bytes;
        }
        int32_t symbol = Held(literals, &bits, &bitCount);
        if (symbol < 256)
        {
            if (symbol < 0 || outAt >= outSize)
            {
                next = NEXT_FAILED;
                break;
            }
            out[outAt++] = (uint8_t)symbol;
            continue;
        }
        if (symbol == 256)
        {
            next = NEXT_BLOCK;
            break;
        }
        uint32_t index = (uint32_t)symbol - 257;
        if (index >= 29)
        {
            next = NEXT_FAILED;
            break;
        }
        uint32_t extra = LENGTH_EXTRA[index];
        size_t length = LENGTH_BASE[index] + (uint32_t)(bits & ((1u << extra) - 1u));
        bits >>= extra;
        bitCount -= extra;
        int32_t code = Held(distances, &bits, &bitCount);
        if (code < 0 || code >= MAX_DISTANCES)
        {
            next = NEXT_FAILED;
            break;
        }
        extra = DISTANCE_EXTRA[code];
        size_t distance = DISTANCE_BASE[code] + (uint32_t)(bits & ((1u << extra) - 1u));
        bits >>= extra;
        bitCount -= extra;
        if (distance > outAt || length > outSize - outAt)
        {
            next = NEXT_FAILED;
            break;
        }
        // Byte by byte: a copy may overlap what it makes.
        for (size_t i = 0; i < length; i++, outAt++)
        {
            out[outAt] = out[outAt - distance];
        }
    }
    s->inAt = inAt;
    s->outAt = outAt;
    s->bits = bits;
    s->bitCount = bitCount;
    return next;
}

// Inflates one block's codes until its end.
static bool Codes(Stream* s, const Huffman* literals, const Huffman* distances)
{
    for (;;)
    {
        Next next = Fast(s, literals, distances);
        if (next == NEXT_SYMBOL)
        {
            next = Checked(s, literals, distances);
        }
        if (next != NEXT_SYMBOL)
        {
            return next == NEXT_BLOCK;
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
    return Codes(s, &FIXED_LITERALS, &FIXED_DISTANCES);
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
