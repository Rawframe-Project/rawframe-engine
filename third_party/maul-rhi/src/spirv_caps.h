// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The device features a SPIR-V module's code needs, read from the
// capabilities it declares (docs/contract/container.md).

#ifndef MAUL_RHI_SRC_SPIRV_CAPS_H
#define MAUL_RHI_SRC_SPIRV_CAPS_H

#include <stdbool.h>
#include <stdint.h>

// Reads the capabilities a module of bytes (whole words, its 5-word
// header first) lists before any other instruction, setting each flag
// whose feature one needs and leaving the others: 16-bit floats or
// storage (shaderF16), 64-bit integers (shaderInt64), subgroup
// operations (subgroups). A word count other than two ends the list, as
// does the module's end.
void mrhiReadSpirvCapabilities(const uint8_t* spirv, uint64_t bytes, bool* float16, bool* subgroups,
                               bool* int64);

#endif // MAUL_RHI_SRC_SPIRV_CAPS_H
