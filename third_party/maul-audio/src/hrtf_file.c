// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Reading .maudhrtf files. Numbers are assembled byte by byte, so the
// host's byte order does not matter. The checksum must match, the fields
// must be well framed (file_fields.h), each field's length must be the
// one its counts give, each count bounded first so no product overflows
// 64 bits, and the texts must be UTF-8.

#include "hrtf_file.h"

#include "file_fields.h"

#include <math.h>
#include <string.h>

#define HEADER_BYTES 16u
#define VERSION      3u
// The head field: rate, taps, rings, directions, scale and distance.
#define HEAD_BYTES   24u
#define MIN_RATE     8000u
#define MAX_RATE     384000u
#define MIN_TAPS     8u
#define MAX_TAPS     1024u
#define MAX_RINGS    181u
#define MAX_DIRS     65536u
#define MAX_NAME     256u
#define MAX_LICENSE  65536u
#define MIN_DISTANCE 0.05f
#define MAX_DISTANCE 100.0f

// The fields, in the order a writer puts them.
enum
{
    FIELD_HEAD,
    FIELD_NAME,
    FIELD_LICENSE,
    FIELD_RINGS,
    FIELD_DELAYS,
    FIELD_TAPS,
    FIELD_COUNT
};

static const uint32_t s_tags[FIELD_COUNT] = {
    MAUD_FIELD_TAG('h', 'e', 'a', 'd'), MAUD_FIELD_TAG('n', 'a', 'm', 'e'),
    MAUD_FIELD_TAG('l', 'i', 'c', 's'), MAUD_FIELD_TAG('r', 'i', 'n', 'g'),
    MAUD_FIELD_TAG('d', 'l', 'a', 'y'), MAUD_FIELD_TAG('t', 'a', 'p', 's'),
};

static uint32_t U16(const uint8_t* at)
{
    return (uint32_t)at[0] | (uint32_t)at[1] << 8;
}

static uint32_t U32(const uint8_t* at)
{
    return (uint32_t)at[0] | (uint32_t)at[1] << 8 | (uint32_t)at[2] << 16 | (uint32_t)at[3] << 24;
}

static float F32(const uint8_t* at)
{
    uint32_t bits = U32(at);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

// The continuation bytes a UTF-8 lead byte announces, or 4 for a byte
// that cannot lead.
static size_t ContinuationsOf(uint32_t lead)
{
    return lead < 0x80             ? 0
           : (lead & 0xE0) == 0xC0 ? 1
           : (lead & 0xF0) == 0xE0 ? 2
           : (lead & 0xF8) == 0xF0 ? 3
                                   : 4;
}

// Decodes the character at text, with available bytes from it on; its
// length in bytes, or 0 when it is not well-formed: cut short, overlong,
// a surrogate or past U+10FFFF.
static size_t CharacterLength(const uint8_t* text, size_t available)
{
    static const uint32_t least[] = {0, 0x80, 0x800, 0x10000};
    size_t extra = ContinuationsOf(text[0]);
    if (extra == 4 || extra >= available)
    {
        return 0;
    }
    uint32_t value = extra == 0 ? text[0] : text[0] & (0x3Fu >> extra);
    for (size_t k = 1; k <= extra; ++k)
    {
        if ((text[k] & 0xC0) != 0x80)
        {
            return 0;
        }
        value = value << 6 | (text[k] & 0x3Fu);
    }
    bool valid = value >= least[extra] && value <= 0x10FFFF && (value < 0xD800 || value > 0xDFFF);
    return valid ? extra + 1 : 0;
}

static bool IsUtf8(const uint8_t* text, size_t count)
{
    size_t i = 0;
    while (i < count)
    {
        size_t length = CharacterLength(text + i, count - i);
        if (length == 0)
        {
            return false;
        }
        i += length;
    }
    return true;
}

// The header's counts, each within the format's bounds.
static bool CountsValid(const maudHrtfFile* file, uint32_t nameBytes, uint32_t licenseBytes)
{
    return file->sampleRate >= MIN_RATE && file->sampleRate <= MAX_RATE && file->taps >= MIN_TAPS &&
           file->taps <= MAX_TAPS && file->ringCount >= 1 && file->ringCount <= MAX_RINGS &&
           file->directionCount >= 1 && file->directionCount <= MAX_DIRS && isfinite(file->scale) &&
           file->scale > 0.0f && file->distance >= MIN_DISTANCE && file->distance <= MAX_DISTANCE &&
           nameBytes <= MAX_NAME && licenseBytes <= MAX_LICENSE;
}

// Elevations rising strictly within [-90, 90], every ring with an
// azimuth, the counts adding up to the header's.
static bool RingsValid(const maudHrtfFile* file)
{
    uint64_t directions = 0;
    float previous = -INFINITY;
    for (uint32_t ring = 0; ring < file->ringCount; ++ring)
    {
        float elevation = maudHrtfFileElevation(file, ring);
        uint32_t azimuths = maudHrtfFileAzimuths(file, ring);
        if (!(elevation > previous) || elevation < -90.0f || elevation > 90.0f || azimuths == 0 ||
            azimuths > MAX_DIRS)
        {
            return false;
        }
        previous = elevation;
        directions += azimuths;
    }
    return directions == file->directionCount;
}

maudResult maudReadHrtfFile(const void* bytes, size_t count, maudHrtfFile* fileOut)
{
    const uint8_t* data = bytes;
    if (data == nullptr)
    {
        return maud_errorInvalid;
    }
    if (count < HEADER_BYTES || memcmp(data, "MAUDHRTF", 8) != 0)
    {
        return maud_errorFormat;
    }
    if (U32(data + 8) != VERSION)
    {
        return maud_errorVersion;
    }
    if (maudCrc32(data + HEADER_BYTES, count - HEADER_BYTES) != U32(data + 12))
    {
        return maud_errorFormat;
    }
    maudField fields[FIELD_COUNT];
    maudResult read = maudReadFields(data, count, HEADER_BYTES, s_tags, FIELD_COUNT, fields);
    if (read != maud_success)
    {
        return read;
    }
    // The head, rings, delays and taps have no default; an absent name or
    // license is empty.
    if (!fields[FIELD_HEAD].present || fields[FIELD_HEAD].length != HEAD_BYTES ||
        !fields[FIELD_RINGS].present || !fields[FIELD_DELAYS].present ||
        !fields[FIELD_TAPS].present)
    {
        return maud_errorFormat;
    }
    const uint8_t* head = fields[FIELD_HEAD].bytes;
    maudHrtfFile file = {
        .sampleRate = U32(head),
        .taps = U32(head + 4),
        .ringCount = U32(head + 8),
        .directionCount = U32(head + 12),
        .scale = F32(head + 16),
        .distance = F32(head + 20),
    };
    uint32_t nameBytes = fields[FIELD_NAME].length;
    uint32_t licenseBytes = fields[FIELD_LICENSE].length;
    if (!CountsValid(&file, nameBytes, licenseBytes))
    {
        return maud_errorFormat;
    }
    // Bounded counts: every product here fits in 64 bits.
    if (fields[FIELD_RINGS].length != 8u * (uint64_t)file.ringCount ||
        fields[FIELD_DELAYS].length != 4u * (uint64_t)file.directionCount ||
        fields[FIELD_TAPS].length != 4u * (uint64_t)file.directionCount * file.taps)
    {
        return maud_errorFormat;
    }
    file.name = fields[FIELD_NAME].present ? fields[FIELD_NAME].bytes : data;
    file.nameLength = nameBytes;
    file.license = fields[FIELD_LICENSE].present ? fields[FIELD_LICENSE].bytes : data;
    file.licenseLength = licenseBytes;
    file.rings = fields[FIELD_RINGS].bytes;
    file.delays = fields[FIELD_DELAYS].bytes;
    file.samples = fields[FIELD_TAPS].bytes;
    if (!IsUtf8(file.name, nameBytes) || !IsUtf8(file.license, licenseBytes) || !RingsValid(&file))
    {
        return maud_errorFormat;
    }
    *fileOut = file;
    return maud_success;
}

float maudHrtfFileElevation(const maudHrtfFile* file, uint32_t ring)
{
    return F32(file->rings + 8u * (size_t)ring);
}

uint32_t maudHrtfFileAzimuths(const maudHrtfFile* file, uint32_t ring)
{
    return U32(file->rings + 8u * (size_t)ring + 4u);
}

float maudHrtfFileDelay(const maudHrtfFile* file, uint32_t direction, uint32_t ear)
{
    return (float)U16(file->delays + 4u * (size_t)direction + 2u * ear) / 256.0f;
}

void maudHrtfFileResponse(const maudHrtfFile* file, uint32_t direction, uint32_t ear, float* out)
{
    const uint8_t* at = file->samples + 2u * (size_t)file->taps * (2u * (size_t)direction + ear);
    for (uint32_t tap = 0; tap < file->taps; ++tap)
    {
        uint32_t bits = U16(at + 2u * tap);
        int32_t value = (int32_t)bits - ((bits & 0x8000u) != 0 ? 65536 : 0);
        out[tap] = (float)value * file->scale;
    }
}
