// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// SHA-256 as the Secure Hash Standard (FIPS 180) states it: 64-byte
// blocks, the message padded with a one bit, zeros and its length in
// bits.

#include "sha256.h"

#include <string.h>

static const uint32_t kRounds[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u,
};

static uint32_t Rotate(uint32_t value, int bits)
{
    return (value >> bits) | (value << (32 - bits));
}

// Mixes one 64-byte block into the state.
static void Block(uint32_t state[8], const uint8_t block[64])
{
    uint32_t words[64];
    for (int i = 0; i < 16; ++i)
    {
        words[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 |
                   (uint32_t)block[i * 4 + 2] << 8 | (uint32_t)block[i * 4 + 3];
    }
    for (int i = 16; i < 64; ++i)
    {
        uint32_t s0 = Rotate(words[i - 15], 7) ^ Rotate(words[i - 15], 18) ^ (words[i - 15] >> 3);
        uint32_t s1 = Rotate(words[i - 2], 17) ^ Rotate(words[i - 2], 19) ^ (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    uint32_t v[8];
    memcpy(v, state, sizeof(v));
    for (int i = 0; i < 64; ++i)
    {
        uint32_t s1 = Rotate(v[4], 6) ^ Rotate(v[4], 11) ^ Rotate(v[4], 25);
        uint32_t choose = (v[4] & v[5]) ^ (~v[4] & v[6]);
        uint32_t first = v[7] + s1 + choose + kRounds[i] + words[i];
        uint32_t s0 = Rotate(v[0], 2) ^ Rotate(v[0], 13) ^ Rotate(v[0], 22);
        uint32_t majority = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        memmove(v + 1, v, 7 * sizeof(uint32_t));
        v[4] += first;
        v[0] = first + s0 + majority;
    }
    for (int i = 0; i < 8; ++i)
    {
        state[i] += v[i];
    }
}

void mrhiSha256(const void* bytes, size_t length, uint8_t digestOut[32])
{
    uint32_t state[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                         0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    const uint8_t* data = bytes;
    size_t whole = length / 64;
    for (size_t i = 0; i < whole; ++i)
    {
        Block(state, data + i * 64);
    }
    uint8_t tail[128] = {0};
    size_t rest = length - whole * 64;
    if (rest > 0)
    {
        memcpy(tail, data + whole * 64, rest);
    }
    tail[rest] = 0x80;
    size_t tailLength = rest < 56 ? 64 : 128;
    uint64_t bits = (uint64_t)length * 8;
    for (int i = 0; i < 8; ++i)
    {
        tail[tailLength - 1 - i] = (uint8_t)(bits >> (i * 8));
    }
    Block(state, tail);
    if (tailLength == 128)
    {
        Block(state, tail + 64);
    }
    for (int i = 0; i < 8; ++i)
    {
        digestOut[i * 4] = (uint8_t)(state[i] >> 24);
        digestOut[i * 4 + 1] = (uint8_t)(state[i] >> 16);
        digestOut[i * 4 + 2] = (uint8_t)(state[i] >> 8);
        digestOut[i * 4 + 3] = (uint8_t)state[i];
    }
}
