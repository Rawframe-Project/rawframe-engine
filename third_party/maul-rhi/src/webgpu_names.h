// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The WebGPU names of the contract's features, limits and formats, as
// its WebGPU rows give them (mrhi-0006).

#ifndef MAUL_RHI_SRC_WEBGPU_NAMES_H
#define MAUL_RHI_SRC_WEBGPU_NAMES_H

#include "maul-rhi/capabilities.h"
#include "maul-rhi/resources.h"

#include <stddef.h>

// A feature as the browser names it, and its flag in mrhiFeatures.
typedef struct mrhiWebGpuFeature
{
    const char* name;
    size_t offset;
} mrhiWebGpuFeature;

// A limit as the browser names it, its field in mrhiLimits, and whether
// the field is 64-bit.
typedef struct mrhiWebGpuLimit
{
    const char* name;
    size_t offset;
    bool wide;
} mrhiWebGpuLimit;

// The features whose WebGPU rows are not absent, and every limit WebGPU
// has; the root block is immediates.
extern const mrhiWebGpuFeature mrhiWebGpuFeatures[];
extern const size_t mrhiWebGpuFeatureCount;
extern const mrhiWebGpuLimit mrhiWebGpuLimits[];
extern const size_t mrhiWebGpuLimitCount;

// A limit's value as its field holds it.
double mrhiWebGpuLimitValue(const mrhiLimits* limits, const mrhiWebGpuLimit* limit);

// Stores a limit's value in its field, clamped to its width.
void mrhiSetWebGpuLimit(mrhiLimits* limits, const mrhiWebGpuLimit* limit, double value);

// A known format's WebGPU name.
const char* mrhiWebGpuFormat(mrhiFormat format);

// The bytes a texture's view formats take as a list; every WebGPU format
// name is shorter than 32 bytes.
#define MRHI_WEBGPU_VIEW_FORMAT_BYTES (MRHI_VIEW_FORMATS * 32)

// Writes the formats a texture's views may take besides its own, their
// names comma-separated and NUL-terminated.
void mrhiWebGpuViewFormats(const mrhiTextureDef* def, char out[MRHI_WEBGPU_VIEW_FORMAT_BYTES]);

#endif // MAUL_RHI_SRC_WEBGPU_NAMES_H
