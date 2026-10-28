// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The tagged fields of an asset format, after its header: each a 32-bit
// tag and a 32-bit byte length, little-endian, then its payload,
// zero-padded to MNAV_FIELD_ALIGN bytes. Fields come in increasing order
// of their names, each at most once.

#ifndef MAUL_NAV_SRC_FIELDS_H
#define MAUL_NAV_SRC_FIELDS_H

#include "bytes.h"

#include "maul-nav/base.h"

#include <stddef.h>
#include <stdint.h>

// Where every field's head and payload start, from the format's start,
// given a header of a multiple of it.
#define MNAV_FIELD_ALIGN 8

// A field's tag and length.
#define MNAV_FIELD_HEAD_BYTES 8

// The tag named by four letters, which it reads as in the bytes.
#define MNAV_TAG(a, b, c, d)                                                                       \
    ((uint32_t)(uint8_t)(a) | (uint32_t)(uint8_t)(b) << 8 | (uint32_t)(uint8_t)(c) << 16 |         \
     (uint32_t)(uint8_t)(d) << 24)

// The bytes a field of size payload bytes takes, its head and padding
// included.
size_t mnavFieldBytes(size_t size);

// Writes a field's head; the size payload bytes follow, then
// mnavPutFieldPad.
void mnavPutFieldHead(mnavByteWriter* w, uint32_t tag, size_t size);

// Writes the zero bytes after a payload of size bytes.
void mnavPutFieldPad(mnavByteWriter* w, size_t size);

// A field read: its payload, or no bytes when absent, as a field present
// always has some.
typedef struct mnavField
{
    const uint8_t* bytes;
    size_t size;
} mnavField;

// How reading the fields ended: the result, the known tag at fault (an
// index into the known tags, or -1), and the field's place in the bytes
// (or -1).
typedef struct mnavFieldsResult
{
    mnavResult result;
    int32_t known;
    int32_t at;
} mnavFieldsResult;

// Reads count fields that fill size bytes exactly. known lists the tags
// this version reads, in increasing order of their names; the field of
// known[i] lands in fields[i]. An unknown tag is mnav_errorVersion; a
// head or payload past the end, a field out of order or repeated, a
// field of no bytes, a padding byte not 0, or bytes after the last field
// is mnav_errorFormat.
mnavFieldsResult mnavReadFields(const uint8_t* bytes, size_t size, uint64_t count,
                                const uint32_t* known, int32_t knownCount, mnavField* fields);

#endif // MAUL_NAV_SRC_FIELDS_H
