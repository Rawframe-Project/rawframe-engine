// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Metal driver's render pipeline descriptions (mrhi-0003): a
// graphics pipeline def as a render pipeline descriptor, a depth and
// stencil state, and the state render encoders set per pipeline.
// Included by the driver's Objective-C files only.

#ifndef MAUL_RHI_SRC_METAL_GRAPHICS_H
#define MAUL_RHI_SRC_METAL_GRAPHICS_H

#include "maul-rhi/pipeline.h"

#import <Metal/Metal.h>

// The index of vertex buffer 0; buffer n lies at this minus n.
#define MRHI_METAL_VERTEX_BUFFER_TOP 30

// What a render encoder sets for a pipeline besides its states.
typedef struct mrhiMetalRaster
{
    MTLPrimitiveType primitive;
    MTLCullMode cull;
    MTLWinding winding;
    MTLDepthClipMode clip;
    float depthBias;
    float depthBiasSlope;
    float depthBiasClamp;
} mrhiMetalRaster;

// A def's render pipeline descriptor without its functions, in the
// caller's autorelease pool: its vertex layout, targets, blending and
// multisampling.
MTLRenderPipelineDescriptor* mrhiMetalDescribeRender(const mrhiGraphicsPipelineDef* def);

// A def's depth and stencil state, retained; nil without a depth or
// stencil format.
id<MTLDepthStencilState> mrhiMetalNewDepthStencil(id<MTLDevice> device,
                                                  const mrhiGraphicsPipelineDef* def);

mrhiMetalRaster mrhiMetalRasterOf(const mrhiGraphicsPipelineDef* def);

#endif // MAUL_RHI_SRC_METAL_GRAPHICS_H
