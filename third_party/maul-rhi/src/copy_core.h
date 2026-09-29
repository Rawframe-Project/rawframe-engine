// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What copies and uploads share (mrhi-0011): the pass they record in,
// the resources they name, the texture side of a transfer checked as
// WebGPU checks it, and linear texel layouts.

#ifndef MAUL_RHI_SRC_COPY_CORE_H
#define MAUL_RHI_SRC_COPY_CORE_H

#include "capabilities_core.h"
#include "encoder_core.h"

// A texture side of a transfer, checked: its frame resource's slot, its
// def, the aspect named, and the part of the texture it covers.
typedef struct mrhiTextureSide
{
    uint32_t object;
    const mrhiTextureDef* def;
    mrhiTextureAspect aspect;
    mrhiFrameUse part;
} mrhiTextureSide;

// The pass a copy or upload records in: recording and without targets,
// or NULL with the refusal.
mrhiFramePass* mrhiCopyPass(mrhiDevice* device, mrhiPassId id, mrhiResult* statusOut);

// Finds a resource of the open frame that is a buffer, or not: its
// slot, or 0 with the refusal.
uint32_t mrhiFindKind(const mrhiDevice* device, mrhiResourceId id, bool buffer,
                      mrhiResult* statusOut);

// Refuses a status, counting invalid input as misuse.
mrhiResult mrhiRefuse(mrhiDevice* device, mrhiResult status);

// Takes a transfer's records, the command's and its two sides', filling
// the command: the records, or NULL when the arena is full.
mrhiCommand* mrhiTakeCopy(mrhiDevice* device, mrhiFramePass* pass, mrhiCommandType type,
                          uint64_t width, uint64_t height, uint64_t depth);

// Checks a texture side of a transfer of a size and resolves it:
// success, or the refusal. The region is in whole blocks within the
// mip's physical size, and a depth format or multisampled texture is
// transferred whole.
mrhiResult mrhiCheckTextureSide(const mrhiDevice* device, const mrhiTextureCopy* copy,
                                const mrhiExtent3d* size, mrhiTextureSide* sideOut);

// The copy facts of a texture side transferred with a buffer: of its
// color, or of the one aspect of a depth format it names. All zero for
// both aspects of a format that has two.
mrhiFormatCopy mrhiSideCopyFacts(const mrhiTextureSide* side);

// Checks the texture side of a transfer with a buffer, the texture
// being the source when asked: a single-sampled texture whose aspect
// copies that way. Success with the side and its copy facts, or the
// refusal.
mrhiResult mrhiCheckTextureTransfer(const mrhiDevice* device, const mrhiTextureCopy* texture,
                                    const mrhiExtent3d* size, bool fromTexture,
                                    mrhiTextureSide* sideOut, mrhiFormatCopy* factsOut);

// Whether a linear layout of texels, blocks of bytes each, fits total
// bytes for a transfer of a size, as WebGPU validates linear texture
// data: rows and images given where the transfer has several, each at
// least what the transfer needs; with aligned, WebGPU's buffer rule of
// bytes per row in multiples of 256.
bool mrhiIsLayoutValid(uint64_t offset, uint32_t bytesPerRow, uint32_t rowsPerImage, uint64_t total,
                       mrhiFormatBlock block, uint32_t bytes, const mrhiExtent3d* size,
                       bool aligned);

#endif // MAUL_RHI_SRC_COPY_CORE_H
