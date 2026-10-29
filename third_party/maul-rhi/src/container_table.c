// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The container's header and section table: the magic, the version, the
// size and the digest, then each section's tag, bounds and place, so that
// the sections fill the container 8-byte aligned with zero padding, as
// docs/user/shader-container-format.md lays it out.

#include "container_table.h"

#include "bytes.h"
#include "sha256.h"

#include "maul-rhi/shader.h"

#include <stdckdint.h>

#define HEADER_BYTES      64
#define SECTION_BYTES     24
#define CONTAINER_VERSION 2

// Checks the header: success with the section count, version or format.
static mrhiResult CheckHeader(const uint8_t* bytes, size_t size, uint32_t* countOut)
{
    if (size < HEADER_BYTES || bytes[0] != 'M' || bytes[1] != 'R' || bytes[2] != 'S' ||
        bytes[3] != 'C')
    {
        return mrhi_errorFormat;
    }
    if (mrhiRead32(bytes + 4) != CONTAINER_VERSION)
    {
        return mrhi_errorVersion;
    }
    uint32_t count = mrhiRead32(bytes + 48);
    if (mrhiRead64(bytes + 8) != size || !mrhiIsZero(bytes + 52, 12) ||
        count > MRHI_MAX_CONTAINER_SECTIONS ||
        count * (uint64_t)SECTION_BYTES > size - HEADER_BYTES)
    {
        return mrhi_errorFormat;
    }
    uint8_t digest[MRHI_DIGEST_BYTES];
    mrhiSha256(bytes + 48, size - 48, digest);
    for (int i = 0; i < MRHI_DIGEST_BYTES; ++i)
    {
        if (digest[i] != bytes[16 + i])
        {
            return mrhi_errorFormat;
        }
    }
    *countOut = count;
    return mrhi_success;
}

// Rounds a section's end up to the next multiple of 8.
static uint64_t Padded(uint64_t end)
{
    return end + (8 - end % 8) % 8;
}

// Reads and checks the section table: version for a type the library
// does not know, which only a later version writes; format unless every
// section holds bytes, appears once and lies inside the container, and
// the sections fill the bytes after the table, each 8-byte aligned right
// after the one before, in any order of the table, with zero bytes
// between them and to the end.
static mrhiResult ReadSections(const uint8_t* bytes, size_t size, uint32_t count,
                               mrhiContainerSection* sections)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t type = mrhiRead32(bytes + HEADER_BYTES + (size_t)i * SECTION_BYTES);
        if (type == 0 || type > MRHI_CONTAINER_TYPES)
        {
            return mrhi_errorVersion;
        }
    }
    // The table's records in the order of their offsets.
    uint32_t order[MRHI_MAX_CONTAINER_SECTIONS];
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint8_t* record = bytes + HEADER_BYTES + (size_t)i * SECTION_BYTES;
        mrhiContainerSection section = {mrhiRead32(record), mrhiRead64(record + 8),
                                        mrhiRead64(record + 16)};
        uint64_t end = 0;
        if (mrhiRead32(record + 4) != 0 || section.size == 0 ||
            ckd_add(&end, section.offset, section.size) || end > size)
        {
            return mrhi_errorFormat;
        }
        for (uint32_t j = 0; j < i; ++j)
        {
            if (sections[j].type == section.type)
            {
                return mrhi_errorFormat;
            }
        }
        uint32_t at = i;
        while (at > 0 && sections[order[at - 1]].offset > section.offset)
        {
            order[at] = order[at - 1];
            --at;
        }
        order[at] = i;
        sections[i] = section;
    }
    uint64_t next = HEADER_BYTES + (uint64_t)count * SECTION_BYTES;
    for (uint32_t i = 0; i < count; ++i)
    {
        mrhiContainerSection section = sections[order[i]];
        uint64_t end = section.offset + section.size;
        uint64_t padded = Padded(end);
        if (section.offset != next || padded > size || !mrhiIsZero(bytes + end, padded - end))
        {
            return mrhi_errorFormat;
        }
        next = padded;
    }
    return next == size ? mrhi_success : mrhi_errorFormat;
}

mrhiResult mrhiReadContainerTable(const uint8_t* bytes, size_t size, mrhiContainerSection* sections,
                                  uint32_t* countOut)
{
    uint32_t count = 0;
    mrhiResult status = CheckHeader(bytes, size, &count);
    if (status != mrhi_success)
    {
        return status;
    }
    status = ReadSections(bytes, size, count, sections);
    *countOut = count;
    return status;
}
