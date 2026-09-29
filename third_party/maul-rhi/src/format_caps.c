// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Format capabilities from the floor and the features.

#include "format_caps.h"

#include "capabilities_core.h"

mrhiFormatCaps mrhiGrantedFormatCaps(mrhiFormat format, const mrhiFeatures* features)
{
    mrhiFormatCaps caps = mrhiFloorFormatCaps(format);
    bool float32 = format == mrhi_formatR32Float || format == mrhi_formatRg32Float ||
                   format == mrhi_formatRgba32Float;
    caps.filtering = caps.filtering || (float32 && features->float32Filterable);
    caps.rendering =
        caps.rendering || (format == mrhi_formatRg11b10Ufloat && features->rg11b10Renderable);
    if (!caps.sampling && mrhiIsFormatKnown(format) && mrhiFormatFamilyGranted(format, features))
    {
        caps.sampling = true;
        caps.filtering = true;
        caps.sampleCounts = 1;
    }
    return caps;
}
