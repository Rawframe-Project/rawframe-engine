// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Format capabilities where an API's are WebGPU's (mrhi-0006): the
// floor, which is WebGPU's guarantee, and what an adapter's features
// add. The test driver and the WebGPU driver report them.

#ifndef MAUL_RHI_SRC_FORMAT_CAPS_H
#define MAUL_RHI_SRC_FORMAT_CAPS_H

#include "maul-rhi/capabilities.h"

// The floor, with filtering of 32-bit floats, rendering to rg11b10 and
// sampling of a compressed family where the features grant them.
mrhiFormatCaps mrhiGrantedFormatCaps(mrhiFormat format, const mrhiFeatures* features);

#endif // MAUL_RHI_SRC_FORMAT_CAPS_H
