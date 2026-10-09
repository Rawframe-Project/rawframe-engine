// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The CRC-32 (crc32.h), a bit at a time: the files are read once, at
// load, so a table buys nothing worth its size.

#include "crc32.h"

#define POLYNOMIAL 0xEDB88320u

uint32_t maudCrc32(const uint8_t* bytes, size_t count)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < count; ++i)
    {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc >> 1) ^ (POLYNOMIAL & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}
