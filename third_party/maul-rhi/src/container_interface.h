// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A shader container's interface records: vertex inputs, color outputs
// and inter-stage variables, 8 bytes each (docs/contract/container.md),
// read and checked.

#ifndef MAUL_RHI_SRC_CONTAINER_INTERFACE_H
#define MAUL_RHI_SRC_CONTAINER_INTERFACE_H

#include "maul-rhi/shader.h"

#include <stdbool.h>
#include <stdint.h>

#define MRHI_VARIABLE_BYTES 8

typedef struct mrhiShaderVariable
{
    uint32_t location;
    mrhiScalarType type;
    uint8_t components;
    mrhiInterpolation interpolation;
    mrhiSampling sampling;
    // A color output's blend source: 0, or 1 for the second source of
    // dual-source blending; 0 for other records.
    uint8_t blendSource;
} mrhiShaderVariable;

// The record of an interface section at an index.
mrhiShaderVariable mrhiReadInterfaceRecord(const uint8_t* records, uint32_t index);

// What an interface record is.
typedef enum mrhiInterfaceRole
{
    mrhi_interfaceInput,
    mrhi_interfaceOutput,
    mrhi_interfaceVariable,
} mrhiInterfaceRole;

// Whether every record of an interface section is well formed for its
// role, whether or not an entry names it; notes 16-bit floats.
bool mrhiAreInterfaceRecordsValid(const uint8_t* records, uint32_t count, mrhiInterfaceRole role,
                                  bool* float16Out);

// Whether the locations of a range of interface records are unique;
// color outputs may share location 0 as the two sources of dual-source
// blending, of one type and component count, and a second source never
// comes without its first.
bool mrhiAreInterfaceLocationsUnique(const uint8_t* records, uint32_t first, uint32_t count,
                                     bool outputs);

#endif // MAUL_RHI_SRC_CONTAINER_INTERFACE_H
