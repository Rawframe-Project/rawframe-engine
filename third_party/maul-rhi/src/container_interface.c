// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A container's interface records (container_interface.h).

#include "container_interface.h"

#include "bytes.h"

mrhiShaderVariable mrhiReadInterfaceRecord(const uint8_t* records, uint32_t index)
{
    const uint8_t* at = records + (size_t)index * MRHI_VARIABLE_BYTES;
    return (mrhiShaderVariable){
        .location = mrhiRead32(at),
        .type = at[4],
        .components = at[5],
        .interpolation = at[6],
        .sampling = at[7],
    };
}

// Whether an inter-stage variable is interpolated as WGSL allows:
// integers flat, flat from the first or either vertex, and the others
// sampled at the center, the centroid or per sample.
static bool IsInterpolationValid(mrhiShaderVariable variable)
{
    bool integer = variable.type == mrhi_scalarSint32 || variable.type == mrhi_scalarUint32;
    switch (variable.interpolation)
    {
    case mrhi_interpolationPerspective:
    case mrhi_interpolationLinear:
        return !integer && variable.sampling >= mrhi_samplingCenter &&
               variable.sampling <= mrhi_samplingSample;
    case mrhi_interpolationFlat:
        return variable.sampling == mrhi_samplingFirst || variable.sampling == mrhi_samplingEither;
    default:
        return false;
    }
}

// Whether every record of an interface section is well formed for its
// role, whether or not an entry names it; notes 16-bit floats.
bool mrhiAreInterfaceRecordsValid(const uint8_t* records, uint32_t count, mrhiInterfaceRole role,
                                  bool* float16Out)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        mrhiShaderVariable variable = mrhiReadInterfaceRecord(records, i);
        bool type = variable.type >= mrhi_scalarFloat32 && variable.type <= mrhi_scalarUint32 &&
                    !(role == mrhi_interfaceInput && variable.type == mrhi_scalarFloat16);
        // A color output's byte 6 is its blend source: the second at
        // location 0 only.
        bool interpolation = role == mrhi_interfaceVariable ? IsInterpolationValid(variable)
                             : role == mrhi_interfaceOutput
                                 ? variable.sampling == 0 &&
                                       (variable.interpolation == 0 ||
                                        (variable.interpolation == 1 && variable.location == 0))
                                 : variable.interpolation == 0 && variable.sampling == 0;
        bool location = role != mrhi_interfaceOutput || variable.location < MRHI_COLOR_TARGETS;
        if (!type || variable.components < 1 || variable.components > 4 || !interpolation ||
            !location)
        {
            return false;
        }
        *float16Out = *float16Out || variable.type == mrhi_scalarFloat16;
    }
    return true;
}

// Color outputs may share location 0 as the two sources of dual-source
// blending, of one type and component count.
bool mrhiAreInterfaceLocationsUnique(const uint8_t* records, uint32_t first, uint32_t count,
                                     bool outputs)
{
    uint32_t second = 0;
    uint32_t paired = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        mrhiShaderVariable record = mrhiReadInterfaceRecord(records, first + i);
        second += outputs && record.interpolation != 0 ? 1 : 0;
        for (uint32_t j = 0; j < i; ++j)
        {
            mrhiShaderVariable other = mrhiReadInterfaceRecord(records, first + j);
            bool sources = outputs && record.interpolation != other.interpolation &&
                           record.type == other.type && record.components == other.components;
            if (other.location == record.location && !sources)
            {
                return false;
            }
            paired += other.location == record.location ? 1 : 0;
        }
    }
    // A second source has its first beside it.
    return second == paired;
}
