// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A monitor's HDR static metadata from its EDID.

#include "edid.h"

#include <math.h>
#include <string.h>

#define BLOCK_BYTES 128

// The CTA-861 extension's tag, the data block tag that says an extended
// tag follows, and the extended tag of HDR static metadata.
#define CTA_EXTENSION   0x02u
#define USE_EXTENDED    7u
#define HDR_STATIC      6u
#define EOTF_ST2084_BIT 0x04u
#define EOTF_HLG_BIT    0x08u

static bool IsBlockWhole(const uint8_t* block)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < BLOCK_BYTES; i++)
    {
        sum = (uint8_t)(sum + block[i]);
    }
    return sum == 0;
}

// The luminance a code value names: 50 times 2 to the code over 32, in
// nits; 0 for a code of 0, which gives none.
static float Luminance(uint8_t code)
{
    return code != 0 ? 50.0f * exp2f((float)code / 32.0f) : 0.0f;
}

// Reads an HDR static metadata block's payload (after its extended tag).
static void ReadHdr(const uint8_t* payload, size_t length, mwinEdidHdr* hdrOut)
{
    *hdrOut = (mwinEdidHdr){
        .pq = (payload[0] & EOTF_ST2084_BIT) != 0,
        .hlg = (payload[0] & EOTF_HLG_BIT) != 0,
        .peakNits = length > 2 ? Luminance(payload[2]) : 0.0f,
        .frameAverageNits = length > 3 ? Luminance(payload[3]) : 0.0f,
    };
    // The least is a share of the most: most times (code / 255) squared
    // over 100.
    if (length > 4 && hdrOut->peakNits > 0.0f)
    {
        float share = (float)payload[4] / 255.0f;
        hdrOut->minimumNits = hdrOut->peakNits * share * share / 100.0f;
    }
}

// Looks for the block in a CTA-861 extension's data block collection.
static bool FindInExtension(const uint8_t* block, mwinEdidHdr* hdrOut)
{
    // Byte 2 is where the detailed timings start; the data blocks lie
    // from byte 4 to there.
    size_t end = block[2];
    if (block[0] != CTA_EXTENSION || end < 4 || end > BLOCK_BYTES - 1)
    {
        return false;
    }
    for (size_t at = 4; at < end;)
    {
        uint8_t tag = (uint8_t)(block[at] >> 5);
        size_t length = block[at] & 0x1Fu;
        if (at + 1 + length > end)
        {
            return false;
        }
        // The extended tag, then at least the transfer functions and the
        // metadata descriptors.
        if (tag == USE_EXTENDED && length >= 3 && block[at + 1] == HDR_STATIC)
        {
            ReadHdr(&block[at + 2], length - 1, hdrOut);
            return true;
        }
        at += 1 + length;
    }
    return false;
}

// Whether the bytes begin with a base block: its header and checksum.
static bool IsEdid(const uint8_t* bytes, size_t length)
{
    static const uint8_t header[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    return bytes != nullptr && length >= BLOCK_BYTES &&
           memcmp(bytes, header, sizeof(header)) == 0 && IsBlockWhole(bytes);
}

bool mwinEdidHdrOf(const uint8_t* bytes, size_t length, mwinEdidHdr* hdrOut)
{
    *hdrOut = (mwinEdidHdr){0};
    if (!IsEdid(bytes, length))
    {
        return false;
    }
    // Byte 126 counts the extensions; only those the bytes hold are read.
    size_t blocks = length / BLOCK_BYTES;
    size_t extensions = bytes[126];
    for (size_t i = 1; i <= extensions && i < blocks; i++)
    {
        const uint8_t* block = bytes + i * BLOCK_BYTES;
        if (IsBlockWhole(block) && FindInExtension(block, hdrOut))
        {
            return true;
        }
    }
    *hdrOut = (mwinEdidHdr){0};
    return false;
}

// Whether a size in millimeters is within a fifth of one in centimeters.
static bool Agrees(uint32_t millimeters, uint32_t centimeters)
{
    uint32_t base = centimeters * 10;
    uint32_t apart = millimeters > base ? millimeters - base : base - millimeters;
    return apart * 5 <= base;
}

bool mwinEdidSizeOf(const uint8_t* bytes, size_t length, uint32_t* widthMmOut,
                    uint32_t* heightMmOut)
{
    *widthMmOut = 0;
    *heightMmOut = 0;
    if (!IsEdid(bytes, length))
    {
        return false;
    }
    // Bytes 21 and 22 give the size in centimeters, or an aspect ratio
    // when one of them is 0.
    uint32_t widthCm = bytes[21];
    uint32_t heightCm = bytes[22];
    bool centimeters = widthCm != 0 && heightCm != 0;
    // The first detailed timing, at byte 54, is the preferred one: a
    // pixel clock (its first two bytes) not 0, then its image's size in
    // millimeters at 12 to 14, the high bits in 14's nibbles.
    const uint8_t* timing = bytes + 54;
    uint32_t width = (uint32_t)timing[12] | (uint32_t)(timing[14] & 0xF0u) << 4;
    uint32_t height = (uint32_t)timing[13] | (uint32_t)(timing[14] & 0x0Fu) << 8;
    bool timed = (timing[0] != 0 || timing[1] != 0) && width != 0 && height != 0;
    if (timed && (!centimeters || (Agrees(width, widthCm) && Agrees(height, heightCm))))
    {
        *widthMmOut = width;
        *heightMmOut = height;
    }
    else if (centimeters)
    {
        *widthMmOut = widthCm * 10;
        *heightMmOut = heightCm * 10;
    }
    return *widthMmOut != 0;
}
