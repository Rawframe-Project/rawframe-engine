// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// DEFLATE (RFC 1951) in a zlib stream (RFC 1950), for PNG (record
// mui-0006): stored, fixed and dynamic Huffman blocks inflated into a
// buffer whose size the caller knows, every length, distance and code
// checked against the input and the output, and the stream's Adler-32
// checked against what it gave. It allocates nothing.

#ifndef MAUL_UI_SRC_INFLATE_H
#define MAUL_UI_SRC_INFLATE_H

#include "maul-ui/base.h"

#include <stddef.h>
#include <stdint.h>

// Inflates a zlib stream into exactly outSize bytes: mui_success when
// the stream ends, filling them, and its checksum matches; otherwise
// mui_errorFormat, out's bytes unspecified.
muiResult muiInflateZlib(const uint8_t* in, size_t inSize, uint8_t* out, size_t outSize);

#endif // MAUL_UI_SRC_INFLATE_H
