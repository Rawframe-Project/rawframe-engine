// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// PNG images (record mui-0006), for the colour bitmaps of CBDT and sbix
// fonts: every colour type and bit depth of the core format, PLTE and
// tRNS, the five filters and Adam7 interlacing, each chunk's CRC and the
// image data's checksum checked; other ancillary chunks are skipped, and
// colours are taken as sRGB. Decoded to RGBA, eight bits a channel,
// straight alpha.

#ifndef MAUL_UI_SRC_PNG_H
#define MAUL_UI_SRC_PNG_H

#include "text_block.h"

#include "maul-ui/base.h"

#include <stddef.h>
#include <stdint.h>

// Decodes a PNG of at most maxExtent pixels a side into width * height
// * 4 bytes of RGBA, rows from the top, scratch holding the image data
// on the way: mui_success; mui_errorCapacity with the size in widthOut
// and heightOut when pixels hold too few bytes, or scratch cannot grow;
// mui_errorFormat for anything not a PNG of that size.
muiResult muiDecodePng(const muiAllocator* allocator, muiBuffer* scratch, const uint8_t* data,
                       size_t size, uint32_t maxExtent, uint32_t* widthOut, uint32_t* heightOut,
                       uint8_t* pixels, size_t capacity);

#endif // MAUL_UI_SRC_PNG_H
