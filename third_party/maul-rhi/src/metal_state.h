// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The state of a Metal frame's encoding (mrhi-0003): its command buffer
// and current encoder, the frame's objects by slot, the staging and
// readback buffers copies name as object 0, and what the pass has set,
// applied before each draw or dispatch. Included by the driver's
// Objective-C files only.

#ifndef MAUL_RHI_SRC_METAL_STATE_H
#define MAUL_RHI_SRC_METAL_STATE_H

#include "driver.h"
#include "metal_pipeline.h"

#import <Metal/Metal.h>

// The binding tables a pipeline declares.
#define MRHI_METAL_TABLES 4
// Metal's buffer indices, each with its size for SPIRV-Cross's buffer
// sizes.
#define MRHI_METAL_BUFFERS 31
// The debug groups a pass holds open at once.
#define MRHI_METAL_LABELS 32
// The root block's bytes at most.
#define MRHI_METAL_ROOT_BYTES 256

// A table's binding as bound: its slot, kind, Metal object (a buffer, a
// texture or its view, or a sampler state), and a buffer's offset and
// size.
typedef struct mrhiMetalBound
{
    uint32_t slot;
    uint8_t kind;
    id object;
    uint64_t offset;
    uint64_t size;
} mrhiMetalBound;

typedef struct mrhiMetalEncoder
{
    const mrhiDriverFrame* frame;
    const mrhiDriverPass* pass;
    // The frame's objects by slot less one; nil for one no pass uses.
    id* objects;
    id<MTLBuffer> staging;
    id<MTLBuffer> readback;
    id<MTLCommandBuffer> commands;
    id<MTLDepthStencilState> noDepth;
    id<MTLRenderCommandEncoder> render;
    id<MTLComputeCommandEncoder> compute;
    id<MTLBlitCommandEncoder> blit;
    const mrhiMetalPipeline* pipeline;
    uint8_t root[MRHI_METAL_ROOT_BYTES];
    bool dirty;
    mrhiMetalBound tables[MRHI_METAL_TABLES][MRHI_TABLE_BINDINGS];
    uint32_t tableCounts[MRHI_METAL_TABLES];
    uint32_t sizes[MRHI_METAL_BUFFERS];
    id<MTLBuffer> indexBuffer;
    uint64_t indexOffset;
    MTLIndexType indexType;
    NSString* labels[MRHI_METAL_LABELS];
    uint32_t labelCount;
    // The readback bytes the frame writes, from low to high.
    uint64_t low;
    uint64_t high;
} mrhiMetalEncoder;

#endif // MAUL_RHI_SRC_METAL_STATE_H
