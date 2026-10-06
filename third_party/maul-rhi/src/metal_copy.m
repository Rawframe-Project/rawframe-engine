// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Metal driver's copies (mrhi-0003). Metal copies one slice of an
// array or cube texture at a time, so such a copy is a copy per layer,
// its images a stride apart in the buffer; a 3D texture's copy takes its
// depth at once. A depth and stencil texture's aspect is a blit option.
// An occlusion query's result is 8 bytes, as the contract's.

#include "metal_copy.h"

#include "capabilities_core.h"
#include "invariant.h"
#include "metal_resource.h"

#include <string.h>

// A copy side's buffer: the frame's object, or for object 0 the staging
// buffer uploads read or the readback buffer readbacks write.
static id<MTLBuffer> BufferOf(const mrhiMetalEncoder* encoder, uint32_t object, bool readback)
{
    if (object == 0)
    {
        return readback ? encoder->readback : encoder->staging;
    }
    return encoder->objects[object - 1];
}

static const mrhiTextureDef* TextureOf(const mrhiMetalEncoder* encoder, uint32_t object)
{
    MRHI_ASSERT(object != 0 && object <= encoder->frame->resourceCount);
    return encoder->frame->resources[object - 1].texture;
}

static void Widen(mrhiMetalEncoder* encoder, uint64_t offset, uint64_t bytes)
{
    encoder->low = offset < encoder->low ? offset : encoder->low;
    encoder->high = offset + bytes > encoder->high ? offset + bytes : encoder->high;
}

// The option that picks one aspect of a depth and stencil texture.
static MTLBlitOption AspectOf(const mrhiTextureDef* def, uint8_t aspect)
{
    if (def->format != mrhi_formatDepthStencil)
    {
        return MTLBlitOptionNone;
    }
    return aspect == mrhi_aspectStencilOnly ? MTLBlitOptionStencilFromDepthStencil
                                            : MTLBlitOptionDepthFromDepthStencil;
}

// The bytes a copy into a buffer spans there, its last row only as long
// as the texels it holds.
static uint64_t SpanOf(const mrhiTextureDef* def, const mrhiCommandBufferSide* buffer,
                       const mrhiCommandTextureSide* texture, const mrhiCommand* command)
{
    mrhiFormatBlock block = mrhiGetFormatBlock(def->format);
    uint32_t texelBytes = mrhiGetFormatCopy(def->format, texture->aspect).bytes;
    MRHI_ASSERT(texelBytes > 0);
    uint64_t rows = (uint32_t)command->c / block.height;
    uint64_t rowBytes = (uint64_t)((uint32_t)command->b / block.width) * texelBytes;
    uint64_t depth = (uint32_t)command->d;
    return (uint64_t)buffer->bytesPerRow * buffer->rowsPerImage * (depth - 1) +
           (uint64_t)buffer->bytesPerRow * (rows - 1) + rowBytes;
}

static void CopyWithTexture(mrhiMetalEncoder* encoder, const mrhiCommand* command)
{
    bool toTexture =
        command->type == mrhiCommandCopyBufferToTexture || command->type == mrhiCommandWriteTexture;
    bool readback = command->type == mrhiCommandReadTexture;
    mrhiCommandBufferSide buffer;
    mrhiCommandTextureSide texture;
    memcpy(&buffer, toTexture ? &command[1] : &command[2], sizeof(buffer));
    memcpy(&texture, toTexture ? &command[2] : &command[1], sizeof(texture));
    const mrhiTextureDef* def = TextureOf(encoder, texture.object);
    id<MTLTexture> image = encoder->objects[texture.object - 1];
    id<MTLBuffer> bytes = BufferOf(encoder, buffer.object, readback);
    bool volume = def->kind == mrhi_texture3d;
    uint32_t depth = (uint32_t)command->d;
    MTLSize size = MTLSizeMake((uint32_t)command->b, (uint32_t)command->c, volume ? depth : 1);
    // A layout of 0, for a copy of one row or one layer, is the copy's
    // own rows packed, as Metal takes no 0.
    mrhiFormatBlock block = mrhiGetFormatBlock(def->format);
    uint32_t rowBytes =
        (uint32_t)command->b / block.width * mrhiGetFormatCopy(def->format, texture.aspect).bytes;
    uint32_t pitch = buffer.bytesPerRow != 0 ? buffer.bytesPerRow : rowBytes;
    uint32_t rows =
        buffer.rowsPerImage != 0 ? buffer.rowsPerImage : (uint32_t)command->c / block.height;
    uint64_t imageBytes = (uint64_t)pitch * rows;
    MTLBlitOption option = AspectOf(def, texture.aspect);
    if (readback)
    {
        Widen(encoder, buffer.offset, SpanOf(def, &buffer, &texture, command));
    }
    for (uint32_t layer = 0; layer < (volume ? 1 : depth); ++layer)
    {
        NSUInteger slice = volume ? 0 : texture.z + layer;
        MTLOrigin origin = MTLOriginMake(texture.x, texture.y, volume ? texture.z : 0);
        NSUInteger offset = buffer.offset + layer * imageBytes;
        NSUInteger stride = volume ? imageBytes : 0;
        if (toTexture)
        {
            [encoder->blit copyFromBuffer:bytes
                             sourceOffset:offset
                        sourceBytesPerRow:pitch
                      sourceBytesPerImage:stride
                               sourceSize:size
                                toTexture:image
                         destinationSlice:slice
                         destinationLevel:texture.mip
                        destinationOrigin:origin
                                  options:option];
        }
        else
        {
            [encoder->blit copyFromTexture:image
                               sourceSlice:slice
                               sourceLevel:texture.mip
                              sourceOrigin:origin
                                sourceSize:size
                                  toBuffer:bytes
                         destinationOffset:offset
                    destinationBytesPerRow:pitch
                  destinationBytesPerImage:stride
                                   options:option];
        }
    }
}

static void CopyTexture(mrhiMetalEncoder* encoder, const mrhiCommand* command)
{
    mrhiCommandTextureSide sides[2];
    memcpy(sides, &command[1], sizeof(sides));
    id<MTLTexture> source = encoder->objects[sides[0].object - 1];
    id<MTLTexture> target = encoder->objects[sides[1].object - 1];
    bool volume = TextureOf(encoder, sides[0].object)->kind == mrhi_texture3d;
    uint32_t depth = (uint32_t)command->d;
    MTLSize size = MTLSizeMake((uint32_t)command->b, (uint32_t)command->c, volume ? depth : 1);
    for (uint32_t layer = 0; layer < (volume ? 1 : depth); ++layer)
    {
        [encoder->blit
              copyFromTexture:source
                  sourceSlice:volume ? 0 : sides[0].z + layer
                  sourceLevel:sides[0].mip
                 sourceOrigin:MTLOriginMake(sides[0].x, sides[0].y, volume ? sides[0].z : 0)
                   sourceSize:size
                    toTexture:target
             destinationSlice:volume ? 0 : sides[1].z + layer
             destinationLevel:sides[1].mip
            destinationOrigin:MTLOriginMake(sides[1].x, sides[1].y, volume ? sides[1].z : 0)];
    }
}

void mrhiMetalCopy(mrhiMetalEncoder* encoder, const mrhiCommand* command)
{
    MRHI_ASSERT(encoder->blit != nil);
    switch (command->type)
    {
    case mrhiCommandCopyBuffer:
    case mrhiCommandWriteBuffer:
    case mrhiCommandReadBuffer:
    {
        mrhiCommandBufferSide sides[2];
        memcpy(sides, &command[1], sizeof(sides));
        bool readback = command->type == mrhiCommandReadBuffer;
        if (readback)
        {
            Widen(encoder, sides[1].offset, command->b);
        }
        [encoder->blit copyFromBuffer:BufferOf(encoder, sides[0].object, false)
                         sourceOffset:sides[0].offset
                             toBuffer:BufferOf(encoder, sides[1].object, readback)
                    destinationOffset:sides[1].offset
                                 size:command->b];
        break;
    }
    case mrhiCommandCopyTexture:
        CopyTexture(encoder, command);
        break;
    case mrhiCommandClearBuffer:
        [encoder->blit fillBuffer:BufferOf(encoder, command->a, false)
                            range:NSMakeRange((NSUInteger)command->c, (NSUInteger)command->d)
                            value:0];
        break;
    default:
        CopyWithTexture(encoder, command);
        break;
    }
}

void mrhiMetalResolve(mrhiMetalEncoder* encoder, const mrhiCommand* command)
{
    MRHI_ASSERT(encoder->blit != nil);
    uint64_t first = (uint32_t)command->c;
    uint64_t count = (uint32_t)(command->c >> 32);
    [encoder->blit copyFromBuffer:mrhiMetalObject(command->b)
                     sourceOffset:first * 8
                         toBuffer:encoder->objects[command->a - 1]
                destinationOffset:command->d
                             size:count * 8];
}
