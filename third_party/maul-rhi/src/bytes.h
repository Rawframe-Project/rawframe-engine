// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Little-endian numbers in byte arrays, read and written a byte at a
// time, so that nothing depends on the host's byte order or on the
// array's alignment.

#ifndef MAUL_RHI_SRC_BYTES_H
#define MAUL_RHI_SRC_BYTES_H

#include <stdint.h>

static inline uint16_t mrhiRead16(const uint8_t* at)
{
    return (uint16_t)(at[0] | at[1] << 8);
}

static inline uint32_t mrhiRead32(const uint8_t* at)
{
    return (uint32_t)at[0] | (uint32_t)at[1] << 8 | (uint32_t)at[2] << 16 | (uint32_t)at[3] << 24;
}

static inline uint64_t mrhiRead64(const uint8_t* at)
{
    return (uint64_t)mrhiRead32(at) | (uint64_t)mrhiRead32(at + 4) << 32;
}

static inline void mrhiWrite32(uint8_t* at, uint32_t value)
{
    at[0] = (uint8_t)value;
    at[1] = (uint8_t)(value >> 8);
    at[2] = (uint8_t)(value >> 16);
    at[3] = (uint8_t)(value >> 24);
}

static inline void mrhiWrite64(uint8_t* at, uint64_t value)
{
    mrhiWrite32(at, (uint32_t)value);
    mrhiWrite32(at + 4, (uint32_t)(value >> 32));
}

#endif // MAUL_RHI_SRC_BYTES_H
