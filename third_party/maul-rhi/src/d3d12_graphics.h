// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's graphics state (mrhi-0003): a graphics pipeline def
// as a pipeline state desc, from a def the core has checked. Included by
// the driver's files only.

#ifndef MAUL_RHI_SRC_D3D12_GRAPHICS_H
#define MAUL_RHI_SRC_D3D12_GRAPHICS_H

#include "d3d12_api.h"
#include "driver.h"

// A graphics pipeline state desc and the input elements it points to,
// with the topology draws set.
typedef struct mrhiD3d12Graphics
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc;
    D3D12_INPUT_ELEMENT_DESC elements[D3D12_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT];
    D3D_PRIMITIVE_TOPOLOGY topology;
} mrhiD3d12Graphics;

// Describes a def's fixed state, leaving the root signature and code to
// the caller.
void mrhiD3d12DescribeGraphics(const mrhiGraphicsPipelineDef* def, mrhiD3d12Graphics* graphicsOut);

#endif // MAUL_RHI_SRC_D3D12_GRAPHICS_H
