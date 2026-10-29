// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The tagged fields of an asset format.

#include "fields.h"

#include "invariant.h"

#include <stddef.h>
#include <stdint.h>

// The padding after a payload of size bytes.
static size_t Pad(size_t size)
{
    return (MNAV_FIELD_ALIGN - size % MNAV_FIELD_ALIGN) % MNAV_FIELD_ALIGN;
}

// The tag's name as a number, its first letter highest, so that names
// in order are numbers in order.
static uint32_t Rank(uint32_t tag)
{
    return (tag & 0xFFu) << 24 | (tag >> 8 & 0xFFu) << 16 | (tag >> 16 & 0xFFu) << 8 | tag >> 24;
}

size_t mnavFieldBytes(size_t size)
{
    return MNAV_FIELD_HEAD_BYTES + size + Pad(size);
}

void mnavPutFieldHead(mnavByteWriter* w, uint32_t tag, size_t size)
{
    mnavPutU32(w, tag);
    mnavPutU32(w, size);
}

void mnavPutFieldPad(mnavByteWriter* w, size_t size)
{
    for (size_t k = Pad(size); k > 0; --k)
    {
        mnavPutU8(w, 0);
    }
}

static mnavFieldsResult Refuse(mnavResult result, int32_t known, uint64_t at)
{
    return (mnavFieldsResult){result, known, (int32_t)at};
}

mnavFieldsResult mnavReadFields(const uint8_t* bytes, size_t size, uint64_t count,
                                const uint32_t* known, int32_t knownCount, mnavField* fields)
{
    uint32_t rank = 0;
    for (int32_t k = 0; k < knownCount; ++k)
    {
        uint32_t next = Rank(known[k]);
        MNAV_ASSERT(k == 0 || rank < next);
        rank = next;
        fields[k] = (mnavField){nullptr, 0};
    }
    (void)rank;
    mnavByteReader r = {bytes, size, false};
    int32_t last = -1;
    for (uint64_t f = 0; f < count; ++f)
    {
        uint32_t tag = (uint32_t)mnavGetU32(&r);
        size_t length = (size_t)mnavGetU32(&r);
        if (r.exhausted)
        {
            return Refuse(mnav_errorFormat, -1, f);
        }
        int32_t k = 0;
        while (k < knownCount && known[k] != tag)
        {
            ++k;
        }
        if (k == knownCount)
        {
            return Refuse(mnav_errorVersion, -1, f);
        }
        // Known tags are in order, so a field after a later or the same
        // one comes out of order or twice. A field of no bytes holds its
        // default, which the writer leaves out.
        if (k <= last || length == 0 || length > r.left || Pad(length) > r.left - length)
        {
            return Refuse(mnav_errorFormat, k, f);
        }
        fields[k] = (mnavField){r.at, length};
        r.at += length;
        r.left -= length;
        for (size_t p = Pad(length); p > 0; --p)
        {
            if (mnavGetU8(&r) != 0)
            {
                return Refuse(mnav_errorFormat, k, f);
            }
        }
        last = k;
    }
    return r.left == 0 ? (mnavFieldsResult){mnav_success, -1, -1}
                       : Refuse(mnav_errorFormat, -1, count);
}
