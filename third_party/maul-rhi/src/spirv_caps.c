// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// SPIR-V capabilities (spirv_caps.h).

#include "spirv_caps.h"

#include "bytes.h"

// The SPIR-V words before the first instruction, and OpCapability.
#define SPIRV_HEADER_WORDS  5u
#define SPIRV_OP_CAPABILITY 17u
// The capabilities a device feature grants: 16-bit floats and 16-bit
// storage (shaderF16), 64-bit integers (shaderInt64) and the subgroup
// operations, GroupNonUniform to GroupNonUniformQuad (subgroups).
#define SPIRV_FLOAT16         9u
#define SPIRV_INT64           11u
#define SPIRV_GROUP_FIRST     61u
#define SPIRV_GROUP_LAST      68u
#define SPIRV_STORAGE16_FIRST 4433u
#define SPIRV_STORAGE16_LAST  4436u

void mrhiReadSpirvCapabilities(const uint8_t* spirv, uint64_t bytes, bool* float16, bool* subgroups,
                               bool* int64)
{
    uint64_t words = bytes / 4;
    for (uint64_t at = SPIRV_HEADER_WORDS; at + 1 < words;)
    {
        uint32_t word = mrhiRead32(spirv + at * 4);
        uint32_t length = word >> 16;
        if ((word & 0xFFFFu) != SPIRV_OP_CAPABILITY || length != 2)
        {
            return;
        }
        uint32_t capability = mrhiRead32(spirv + (at + 1) * 4);
        *float16 = *float16 || capability == SPIRV_FLOAT16 ||
                   (capability >= SPIRV_STORAGE16_FIRST && capability <= SPIRV_STORAGE16_LAST);
        *int64 = *int64 || capability == SPIRV_INT64;
        *subgroups =
            *subgroups || (capability >= SPIRV_GROUP_FIRST && capability <= SPIRV_GROUP_LAST);
        at += length;
    }
}
