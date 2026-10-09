// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// PNG images (record mui-0006), as the PNG specification (third edition)
// has them. The image data's chunks are joined in scratch, inflated after
// them into their filtered rows, pass by pass where interlaced, the rows
// unfiltered in place, and each pixel expanded to RGBA: samples below 8
// bits scaled by repeating their bits, 16-bit samples by their high byte,
// tRNS compared with the full sample. A chunk out of the specification's
// order, an unknown critical chunk, a palette index past the palette, or
// a CRC or checksum that does not match is mui_errorFormat.

#include "png.h"

#include "inflate.h"

#include <stdbool.h>
#include <string.h>

enum
{
    GREY = 0,
    RGB = 2,
    PALETTE = 3,
    GREY_ALPHA = 4,
    RGBA = 6
};

typedef struct Header
{
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t type;
    bool interlaced;
    uint32_t channels;
} Header;

// The palette and its alphas, and tRNS's sample of other types.
typedef struct Colors
{
    uint8_t palette[256][4];
    uint32_t paletteCount;
    bool keyed;
    uint16_t key[3];
} Colors;

static uint32_t Big32(const uint8_t* p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint32_t Big16(const uint8_t* p)
{
    return (uint32_t)p[0] << 8 | p[1];
}

static uint32_t Crc32(const uint8_t* data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; i++)
    {
        crc ^= data[i];
        for (int k = 0; k < 8; k++)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static bool ReadHeader(const uint8_t* p, uint32_t length, uint32_t maxExtent, Header* h)
{
    if (length != 13)
    {
        return false;
    }
    *h = (Header){Big32(p), Big32(p + 4), p[8], p[9], p[12] == 1, 0};
    static const uint8_t CHANNELS[7] = {1, 0, 3, 1, 2, 0, 4};
    h->channels = h->type <= RGBA ? CHANNELS[h->type] : 0;
    uint32_t d = h->depth;
    bool depthFits = h->type == GREY      ? (d == 1 || d == 2 || d == 4 || d == 8 || d == 16)
                     : h->type == PALETTE ? (d == 1 || d == 2 || d == 4 || d == 8)
                                          : (d == 8 || d == 16);
    // Compression, filter method and interlace: 0, 0, and 0 or 1.
    return h->channels != 0 && depthFits && h->width > 0 && h->height > 0 &&
           h->width <= maxExtent && h->height <= maxExtent && p[10] == 0 && p[11] == 0 &&
           p[12] <= 1;
}

static bool ReadPalette(const uint8_t* p, uint32_t length, Colors* c)
{
    if (length == 0 || length % 3 != 0 || length / 3 > 256)
    {
        return false;
    }
    c->paletteCount = length / 3;
    for (uint32_t i = 0; i < c->paletteCount; i++)
    {
        c->palette[i][0] = p[i * 3];
        c->palette[i][1] = p[i * 3 + 1];
        c->palette[i][2] = p[i * 3 + 2];
        c->palette[i][3] = 255;
    }
    return true;
}

static bool ReadTransparency(const uint8_t* p, uint32_t length, const Header* h, Colors* c)
{
    if (h->type == PALETTE)
    {
        if (length > c->paletteCount)
        {
            return false;
        }
        for (uint32_t i = 0; i < length; i++)
        {
            c->palette[i][3] = p[i];
        }
        return true;
    }
    uint32_t samples = h->type == GREY ? 1 : (h->type == RGB ? 3 : 0);
    if (samples == 0 || length != samples * 2)
    {
        return false;
    }
    c->keyed = true;
    for (uint32_t i = 0; i < samples; i++)
    {
        c->key[i] = (uint16_t)Big16(p + i * 2);
    }
    return true;
}

// Adam7's passes: where each starts and how far apart its pixels are.
static const uint8_t PASS_X[7] = {0, 4, 0, 2, 0, 1, 0};
static const uint8_t PASS_Y[7] = {0, 0, 4, 0, 2, 0, 1};
static const uint8_t PASS_DX[7] = {8, 8, 4, 4, 2, 2, 1};
static const uint8_t PASS_DY[7] = {8, 8, 8, 4, 4, 2, 2};

typedef struct Pass
{
    uint32_t x;
    uint32_t y;
    uint32_t dx;
    uint32_t dy;
    uint32_t width;
    uint32_t height;
} Pass;

static Pass PassOf(const Header* h, uint32_t pass)
{
    if (!h->interlaced)
    {
        return (Pass){0, 0, 1, 1, h->width, h->height};
    }
    Pass p = {PASS_X[pass], PASS_Y[pass], PASS_DX[pass], PASS_DY[pass], 0, 0};
    p.width = h->width > p.x ? (h->width - p.x + p.dx - 1) / p.dx : 0;
    p.height = h->height > p.y ? (h->height - p.y + p.dy - 1) / p.dy : 0;
    return p;
}

static size_t RowBytes(const Header* h, uint32_t width)
{
    return ((size_t)width * h->channels * h->depth + 7) / 8;
}

// The filtered rows' bytes, a filter byte a row.
static size_t RawSize(const Header* h)
{
    size_t size = 0;
    for (uint32_t pass = 0; pass < (h->interlaced ? 7u : 1u); pass++)
    {
        Pass p = PassOf(h, pass);
        size += p.width > 0 ? (size_t)p.height * (1 + RowBytes(h, p.width)) : 0;
    }
    return size;
}

static uint8_t Paeth(uint8_t a, uint8_t b, uint8_t c)
{
    int p = (int)a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    return pa <= pb && pa <= pc ? a : (pb <= pc ? b : c);
}

// Average: the mean of the byte to the left and the byte above.
static void Average(uint8_t* row, const uint8_t* prior, size_t rowBytes, size_t bpp)
{
    for (size_t i = 0; i < rowBytes; i++)
    {
        uint32_t a = i >= bpp ? row[i - bpp] : 0;
        uint32_t b = prior != nullptr ? prior[i] : 0;
        row[i] = (uint8_t)(row[i] + (a + b) / 2);
    }
}

// Paeth: of the bytes left, above and above left, the nearest their
// gradient's guess.
static void PaethRow(uint8_t* row, const uint8_t* prior, size_t rowBytes, size_t bpp)
{
    for (size_t i = 0; i < rowBytes; i++)
    {
        uint8_t a = i >= bpp ? row[i - bpp] : 0;
        uint8_t b = prior != nullptr ? prior[i] : 0;
        uint8_t c = i >= bpp && prior != nullptr ? prior[i - bpp] : 0;
        row[i] = (uint8_t)(row[i] + Paeth(a, b, c));
    }
}

// Unfilters a row by its filter, the row above it NULL for the first.
static void UnfilterRow(uint8_t filter, uint8_t* row, const uint8_t* prior, size_t rowBytes,
                        size_t bpp)
{
    switch (filter)
    {
    case 1:
        for (size_t i = bpp; i < rowBytes; i++)
        {
            row[i] = (uint8_t)(row[i] + row[i - bpp]);
        }
        break;
    case 2:
        for (size_t i = 0; i < rowBytes && prior != nullptr; i++)
        {
            row[i] = (uint8_t)(row[i] + prior[i]);
        }
        break;
    case 3:
        Average(row, prior, rowBytes, bpp);
        break;
    case 4:
        PaethRow(row, prior, rowBytes, bpp);
        break;
    default:
        break;
    }
}

// Unfilters a pass's rows in place; false for an unknown filter.
static bool Unfilter(uint8_t* rows, size_t rowBytes, uint32_t height, size_t bpp)
{
    const uint8_t* prior = nullptr;
    for (uint32_t y = 0; y < height; y++)
    {
        if (rows[0] > 4)
        {
            return false;
        }
        UnfilterRow(rows[0], rows + 1, prior, rowBytes, bpp);
        prior = rows + 1;
        rows += 1 + rowBytes;
    }
    return true;
}

// A sample of a row: depths below 8 packed from the high bits.
static uint32_t Sample(const uint8_t* row, size_t index, uint32_t depth)
{
    if (depth == 16)
    {
        return Big16(row + index * 2);
    }
    if (depth == 8)
    {
        return row[index];
    }
    size_t bit = index * depth;
    return (uint32_t)(row[bit / 8] >> (8 - depth - bit % 8)) & ((1u << depth) - 1u);
}

// A sample made eight bits.
static uint8_t Scale(uint32_t sample, uint32_t depth)
{
    switch (depth)
    {
    case 1:
        return (uint8_t)(sample * 255u);
    case 2:
        return (uint8_t)(sample * 85u);
    case 4:
        return (uint8_t)(sample * 17u);
    case 16:
        return (uint8_t)(sample >> 8);
    default:
        return (uint8_t)sample;
    }
}

// Expands one pixel; false for a palette index past the palette.
static bool Expand(const Header* h, const Colors* c, const uint8_t* row, uint32_t x, uint8_t* out)
{
    size_t first = (size_t)x * h->channels;
    uint32_t s[4] = {0, 0, 0, 0};
    for (uint32_t i = 0; i < h->channels; i++)
    {
        s[i] = Sample(row, first + i, h->depth);
    }
    switch (h->type)
    {
    case PALETTE:
        if (s[0] >= c->paletteCount)
        {
            return false;
        }
        memcpy(out, c->palette[s[0]], 4);
        return true;
    case GREY:
    case GREY_ALPHA:
    {
        uint8_t g = Scale(s[0], h->depth);
        bool clear = c->keyed && s[0] == c->key[0];
        uint8_t a = h->type == GREY_ALPHA ? Scale(s[1], h->depth) : (clear ? 0 : 255);
        out[0] = g;
        out[1] = g;
        out[2] = g;
        out[3] = a;
        return true;
    }
    default:
    {
        bool clear = c->keyed && s[0] == c->key[0] && s[1] == c->key[1] && s[2] == c->key[2];
        for (int i = 0; i < 3; i++)
        {
            out[i] = Scale(s[i], h->depth);
        }
        out[3] = h->type == RGBA ? Scale(s[3], h->depth) : (clear ? 0 : 255);
        return true;
    }
    }
}

// Unfilters and expands the inflated rows into the image.
static bool Develop(const Header* h, const Colors* c, uint8_t* raw, uint8_t* pixels)
{
    size_t bpp = (h->channels * h->depth + 7) / 8;
    for (uint32_t pass = 0; pass < (h->interlaced ? 7u : 1u); pass++)
    {
        Pass p = PassOf(h, pass);
        if (p.width == 0 || p.height == 0)
        {
            continue;
        }
        size_t rowBytes = RowBytes(h, p.width);
        if (!Unfilter(raw, rowBytes, p.height, bpp))
        {
            return false;
        }
        for (uint32_t y = 0; y < p.height; y++)
        {
            const uint8_t* row = raw + (size_t)y * (1 + rowBytes) + 1;
            for (uint32_t x = 0; x < p.width; x++)
            {
                size_t at = ((size_t)(p.y + y * p.dy) * h->width + p.x + x * p.dx) * 4;
                if (!Expand(h, c, row, x, pixels + at))
                {
                    return false;
                }
            }
        }
        raw += (size_t)p.height * (1 + rowBytes);
    }
    return true;
}

// A chunk: its type, data and length, and what follows it.
typedef struct Chunk
{
    uint32_t type;
    const uint8_t* data;
    uint32_t length;
} Chunk;

static uint32_t Tag(const char name[4])
{
    return (uint32_t)(uint8_t)name[0] << 24 | (uint32_t)(uint8_t)name[1] << 16 |
           (uint32_t)(uint8_t)name[2] << 8 | (uint8_t)name[3];
}

// The chunk at *at, its CRC checked, *at moved past it.
static bool NextChunk(const uint8_t* data, size_t size, size_t* at, Chunk* chunk)
{
    if (size - *at < 12)
    {
        return false;
    }
    const uint8_t* p = data + *at;
    uint32_t length = Big32(p);
    if (length > 0x7FFFFFFFu || length > size - *at - 12)
    {
        return false;
    }
    *chunk = (Chunk){Big32(p + 4), p + 8, length};
    *at += 12 + (size_t)length;
    return Crc32(p + 4, 4 + (size_t)length) == Big32(p + 8 + length);
}

// Where the chunks stand as they are read in order.
typedef struct Reading
{
    Header header;
    Colors colors;
    bool seenHeader;
    bool seenData;
    bool dataEnded;
    size_t dataSize;
} Reading;

// Takes one chunk in order; *ended set at IEND.
static bool Take(Reading* r, const Chunk* chunk, uint32_t maxExtent, bool* ended)
{
    if (!r->seenHeader)
    {
        r->seenHeader = chunk->type == Tag("IHDR") &&
                        ReadHeader(chunk->data, chunk->length, maxExtent, &r->header);
        return r->seenHeader;
    }
    bool data = chunk->type == Tag("IDAT");
    // Image data chunks follow one another.
    if (r->seenData && !data)
    {
        r->dataEnded = true;
    }
    if (data)
    {
        r->seenData = true;
        r->dataSize += chunk->length;
        return !r->dataEnded;
    }
    if (chunk->type == Tag("IEND"))
    {
        *ended = true;
        return r->seenData;
    }
    if (chunk->type == Tag("PLTE"))
    {
        return !r->seenData && r->colors.paletteCount == 0 && r->header.type != GREY &&
               r->header.type != GREY_ALPHA && ReadPalette(chunk->data, chunk->length, &r->colors);
    }
    if (chunk->type == Tag("tRNS"))
    {
        return !r->seenData && (r->header.type != PALETTE || r->colors.paletteCount > 0) &&
               ReadTransparency(chunk->data, chunk->length, &r->header, &r->colors);
    }
    // Ancillary chunks, a lower-case first letter, are skipped.
    return (chunk->type >> 24 & 0x20u) != 0;
}

static const uint8_t SIGNATURE[8] = {137, 80, 78, 71, 13, 10, 26, 10};

// Reads every chunk, checking their order.
static bool ReadChunks(const uint8_t* data, size_t size, uint32_t maxExtent, Reading* r)
{
    size_t at = sizeof SIGNATURE;
    bool ended = false;
    while (!ended)
    {
        Chunk chunk;
        if (!NextChunk(data, size, &at, &chunk) || !Take(r, &chunk, maxExtent, &ended))
        {
            return false;
        }
    }
    // A palette image without PLTE fails at its first pixel's index.
    return true;
}

// Joins the image data chunks into one stream at dst.
static void JoinData(const uint8_t* data, size_t size, uint8_t* dst)
{
    size_t at = sizeof SIGNATURE;
    Chunk chunk;
    while (NextChunk(data, size, &at, &chunk) && chunk.type != Tag("IEND"))
    {
        if (chunk.type == Tag("IDAT"))
        {
            memcpy(dst, chunk.data, chunk.length);
            dst += chunk.length;
        }
    }
}

muiResult muiDecodePng(const muiAllocator* allocator, muiBuffer* scratch, const uint8_t* data,
                       size_t size, uint32_t maxExtent, uint32_t* widthOut, uint32_t* heightOut,
                       uint8_t* pixels, size_t capacity)
{
    *widthOut = 0;
    *heightOut = 0;
    Reading r = {0};
    if (data == nullptr || size < sizeof SIGNATURE ||
        memcmp(data, SIGNATURE, sizeof SIGNATURE) != 0 || !ReadChunks(data, size, maxExtent, &r))
    {
        return mui_errorFormat;
    }
    const Header* h = &r.header;
    *widthOut = h->width;
    *heightOut = h->height;
    size_t bytes = (size_t)h->width * h->height * 4;
    if (pixels == nullptr || bytes > capacity)
    {
        return mui_errorCapacity;
    }
    size_t raw = RawSize(h);
    if (r.dataSize > SIZE_MAX - raw || !muiReserve(allocator, scratch, r.dataSize + raw))
    {
        return mui_errorCapacity;
    }
    uint8_t* joined = scratch->data;
    JoinData(data, size, joined);
    if (muiInflateZlib(joined, r.dataSize, joined + r.dataSize, raw) != mui_success ||
        !Develop(h, &r.colors, joined + r.dataSize, pixels))
    {
        return mui_errorFormat;
    }
    return mui_success;
}
