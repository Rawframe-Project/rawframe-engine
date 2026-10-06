// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The command stream (mrhi-0011): 32-byte records in 4 KiB chunks of a
// frame's arena, each pass linking its own chunks. A command's payload
// records follow it in the same chunk.

#ifndef MAUL_RHI_SRC_COMMAND_H
#define MAUL_RHI_SRC_COMMAND_H

#include <stdint.h>

// What a command record does. Fields a to d hold its operands; a is 32
// bits, so a driver handle goes in b, c or d.
typedef enum mrhiCommandType
{
    // b: the pipeline's driver handle.
    mrhiCommandGraphicsPipeline = 1,
    mrhiCommandComputePipeline,
    // a: the offset; b: the size; the bytes follow.
    mrhiCommandRootBlock,
    // The viewport follows.
    mrhiCommandViewport,
    // a, b: x and y; c, d: width and height.
    mrhiCommandScissor,
    // The color follows.
    mrhiCommandBlendConstant,
    // a: the reference.
    mrhiCommandStencilReference,
    // b: the label's bytes, which follow.
    mrhiCommandPushDebugGroup,
    mrhiCommandPopDebugGroup,
    mrhiCommandDebugMarker,
    // a: the table; b: the bindings, an mrhiCommandBinding each, which
    // follow.
    mrhiCommandBindings,
    // a: the slot or the index format; b: the frame resource's slot plus
    // one; c: the offset; d: the size.
    mrhiCommandVertexBuffer,
    mrhiCommandIndexBuffer,
    // a: the vertices; b: the instances; c: the first vertex; d: the
    // first instance.
    mrhiCommandDraw,
    // a: the indices; b: the instances; c: the first index, and the base
    // vertex in the upper half; d: the first instance.
    mrhiCommandDrawIndexed,
    // a, b, c: the workgroups in x, y and z.
    mrhiCommandDispatch,
    // a: the frame resource's slot plus one; c: the offset of the
    // arguments, read on the GPU.
    mrhiCommandDrawIndirect,
    mrhiCommandDrawIndexedIndirect,
    mrhiCommandDispatchIndirect,
    // A counted multi-draw (mrhi-0020). a: the records' frame resource's slot
    // plus one; b: the count's slot plus one, and the most draws in the
    // upper half; c: the records' offset; d: the count's offset.
    mrhiCommandDrawIndirectCount,
    mrhiCommandDrawIndexedIndirectCount,
    // a: the query; b: the query set's driver handle.
    mrhiCommandBeginOcclusionQuery,
    mrhiCommandEndOcclusionQuery,
    // a: the query; b: the query set's driver handle, at its begin and
    // its end.
    mrhiCommandBeginStatisticsQuery,
    mrhiCommandEndStatisticsQuery,
    // a: the frame resource's slot plus one; b: the query set's driver
    // handle; c: the first query, and the count in the upper half; d: the
    // offset.
    mrhiCommandResolveQueries,
    // b, c, d: the width (the bytes of a copy between buffers), height
    // and depth or layers; the source's and destination's sides follow,
    // an mrhiCommandBufferSide or mrhiCommandTextureSide each.
    mrhiCommandCopyBuffer,
    mrhiCommandCopyBufferToTexture,
    mrhiCommandCopyTextureToBuffer,
    mrhiCommandCopyTexture,
    // As a copy from a buffer, the source's side naming the frame's
    // staging (object 0), its bytes placed at 512-byte boundaries and a
    // texture's rows at a 256-byte pitch.
    mrhiCommandWriteBuffer,
    mrhiCommandWriteTexture,
    // As a copy into a buffer, the destination's side naming the
    // device's readback ring (object 0), placed and pitched as uploads.
    mrhiCommandReadBuffer,
    mrhiCommandReadTexture,
    // Zeros a buffer's range (mrhi-0022). a: the frame resource's slot
    // plus one; c: the offset; d: the bytes, a positive multiple of 4.
    mrhiCommandClearBuffer,
    // One past the last type.
    mrhiCommandTypeEnd,
} mrhiCommandType;

typedef struct mrhiCommand
{
    uint16_t type;
    // The payload records that follow.
    uint16_t payload;
    uint32_t a;
    uint64_t b;
    uint64_t c;
    uint64_t d;
} mrhiCommand;

// One binding of a table as recorded: its slot and object (a frame
// resource's slot plus one, 0 for a sampler); a buffer's offset and
// resolved size, a sampler's driver handle, or a texture view's first
// layer and layers; a texture view's format, kind, aspect, first mip
// and mips; and the slot's binding kind, which says how a driver writes
// it (a buffer as uniform or storage, a texture as sampled or storage).
typedef struct mrhiCommandBinding
{
    uint32_t slot;
    uint32_t object;
    uint64_t offset;
    uint64_t size;
    uint16_t viewFormat;
    uint8_t viewKind;
    uint8_t aspect;
    uint8_t baseMip;
    uint8_t mipCount;
    uint8_t kind;
    uint8_t reserved;
} mrhiCommandBinding;

// A buffer's side of a copy as recorded: its frame resource's slot plus
// one, its offset, and for a copy with a texture its layout.
typedef struct mrhiCommandBufferSide
{
    uint32_t object;
    uint32_t bytesPerRow;
    uint64_t offset;
    uint32_t rowsPerImage;
    uint8_t reserved[12];
} mrhiCommandBufferSide;

// A texture's side of a copy as recorded: its frame resource's slot
// plus one, its mip, origin and aspect.
typedef struct mrhiCommandTextureSide
{
    uint32_t object;
    uint32_t mip;
    uint32_t x;
    uint32_t y;
    uint32_t z;
    uint8_t aspect;
    uint8_t reserved[11];
} mrhiCommandTextureSide;

// A chunk's bytes, and the records after its 32-byte header.
#define MRHI_CHUNK_BYTES    4096
#define MRHI_CHUNK_COMMANDS 127

// A chunk: the next chunk of its pass (its index plus one, 0 for none),
// its records in use, and the records.
typedef struct mrhiCommandChunk
{
    uint32_t next;
    uint32_t count;
    uint8_t reserved[24];
    mrhiCommand commands[MRHI_CHUNK_COMMANDS];
} mrhiCommandChunk;

static_assert(sizeof(mrhiCommand) == 32, "a command is one record");
static_assert(sizeof(mrhiCommandBinding) == 32, "a binding is one record");
static_assert(sizeof(mrhiCommandBufferSide) == 32, "a side is one record");
static_assert(sizeof(mrhiCommandTextureSide) == 32, "a side is one record");
static_assert(sizeof(mrhiCommandChunk) == MRHI_CHUNK_BYTES, "a chunk is 4 KiB");

#endif // MAUL_RHI_SRC_COMMAND_H
