// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The CRC-32 the library's file formats seal their bodies with.

#ifndef MAUL_AUDIO_SRC_CRC32_H
#define MAUL_AUDIO_SRC_CRC32_H

#include <stddef.h>
#include <stdint.h>

// The CRC-32 (ISO-HDLC, as zlib computes it) of count bytes.
uint32_t maudCrc32(const uint8_t* bytes, size_t count);

#endif // MAUL_AUDIO_SRC_CRC32_H
