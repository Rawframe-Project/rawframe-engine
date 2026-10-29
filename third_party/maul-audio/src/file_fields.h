// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The tagged fields the library's asset formats are made of
// (docs/internals/file-formats.md). After a file's header comes a
// sequence of fields, each a little-endian uint32 tag and uint32 byte
// length, then its payload, which starts at a multiple of
// MAUD_FIELD_ALIGN bytes from the file's start and is padded with zero
// bytes to the next. A reader takes the tags it knows; a tag it does not
// know comes from a later version and is refused.

#ifndef MAUL_AUDIO_SRC_FILE_FIELDS_H
#define MAUL_AUDIO_SRC_FILE_FIELDS_H

#include "maul-audio/base.h"

#include <stddef.h>
#include <stdint.h>

// Every format's alignment: no payload holds an element wider than 4
// bytes, and the loaders copy what they keep.
#define MAUD_FIELD_ALIGN 8u

// A field's tag and length, before its payload.
#define MAUD_FIELD_FRAME 8u

// Four ASCII characters as a tag, the first in the lowest byte, so that a
// dump of the file shows them in order.
#define MAUD_FIELD_TAG(a, b, c, d)                                                                 \
    ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)

// A field as read: where its payload lies and how long it is; present is
// false for a tag the file did not carry.
typedef struct maudField
{
    const uint8_t* bytes;
    uint32_t length;
    bool present;
} maudField;

// Reads the fields of count bytes of data from first, a multiple of
// MAUD_FIELD_ALIGN, to the end, into fields, one entry per tag of tags.
// maud_errorVersion for a tag not among them; maud_errorFormat for a
// field cut short, a tag found twice or padding that is not zero.
maudResult maudReadFields(const uint8_t* data, size_t count, size_t first, const uint32_t* tags,
                          uint32_t tagCount, maudField* fields);

// The bytes a field with a payload of length takes: frame, payload and
// padding.
uint64_t maudFieldSize(uint64_t length);

// Writes a field's frame at out and zeroes the padding after its payload
// of length bytes, which the caller writes at out + MAUD_FIELD_FRAME.
void maudPutFieldFrame(uint8_t* out, uint32_t tag, uint32_t length);

#endif // MAUL_AUDIO_SRC_FILE_FIELDS_H
