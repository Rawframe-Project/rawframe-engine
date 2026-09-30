// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's copies (d3d12_copy.h). A copy between a buffer and
// a texture reads or writes the buffer through a placed footprint, which
// D3D12 places at 512-byte boundaries: a copy placed elsewhere goes a
// row of blocks at a time, each placed at the boundary below it and
// moved along its footprint's first row. An array texture's layers are
// its subresources, copied one at a time, a stride apart in the buffer;
// a 3D texture's depth is copied at once. A depth and stencil texture's
// aspect is its plane: depth the first, stencil the second.

#include "d3d12_copy.h"

#include "capabilities_core.h"
#include "d3d12_barrier.h"
#include "d3d12_names.h"
#include "invariant.h"

#include <string.h>

// Where a placed footprint must start.
#define PLACEMENT D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT

// A copy side's buffer: the frame's object, made ready for the copy, or
// for object 0 the staging buffer uploads read or the readback buffer
// readbacks write.
static ID3D12Resource* BufferOf(mrhiD3d12Recorder* recorder, uint32_t object, bool readback,
                                D3D12_RESOURCE_STATES needed)
{
    if (object == 0)
    {
        return readback ? recorder->readback : recorder->staging;
    }
    mrhiD3d12Use(recorder, object, needed);
    return recorder->table[object - 1].resource;
}

// Notes a range of the readback ring the frame fills; the core gives a
// frame no more readbacks than its limit.
static void Note(mrhiD3d12Recorder* recorder, uint64_t offset, uint64_t bytes)
{
    MRHI_ASSERT(recorder->readbackCount < recorder->readbackLimit);
    recorder->readbacks[recorder->readbackCount++] =
        (mrhiD3d12Range){.offset = offset, .size = bytes};
}

// The format a footprint of a texture's aspect is in.
static DXGI_FORMAT CopyFormatOf(mrhiFormat format, mrhiTextureAspect aspect)
{
    if (format == mrhi_formatDepthStencil)
    {
        return aspect == mrhi_aspectStencilOnly ? DXGI_FORMAT_R8_TYPELESS
                                                : DXGI_FORMAT_R32_TYPELESS;
    }
    return format == mrhi_formatDepth32Float ? DXGI_FORMAT_R32_TYPELESS : mrhiD3d12Format(format);
}

static UINT SubresourceOf(const mrhiTextureDef* def, uint32_t mip, uint32_t layer, uint8_t aspect)
{
    uint32_t layers = def->kind == mrhi_texture3d ? 1 : def->depthOrLayers;
    uint32_t plane = def->format == mrhi_formatDepthStencil && aspect == mrhi_aspectStencilOnly;
    return mip + (layer + plane * layers) * def->mipLevels;
}

static D3D12_TEXTURE_COPY_LOCATION TextureAt(ID3D12Resource* texture, UINT subresource)
{
    return (D3D12_TEXTURE_COPY_LOCATION){
        .pResource = texture,
        .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
        .SubresourceIndex = subresource,
    };
}

// A buffer's placed footprint of blocks, starting at an offset
// PLACEMENT allows: its location, and in dx the blocks its copy moves
// along the first row, the bytes before the offset asked for.
typedef struct Footprint
{
    D3D12_TEXTURE_COPY_LOCATION location;
    UINT dx;
} Footprint;

static Footprint FootprintOf(ID3D12Resource* buffer, const mrhiTextureDef* def, uint8_t aspect,
                             uint64_t offset, uint32_t rowPitch, const uint32_t size[3])
{
    mrhiFormatBlock block = mrhiGetFormatBlock(def->format);
    uint32_t blockBytes = mrhiGetFormatCopy(def->format, aspect).bytes;
    uint64_t placed = offset / PLACEMENT * PLACEMENT;
    UINT dx = (UINT)((offset - placed) / blockBytes);
    uint32_t widthBytes = (dx + size[0] / block.width) * blockBytes;
    UINT pitch = rowPitch >= widthBytes
                     ? rowPitch
                     : (widthBytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) /
                           D3D12_TEXTURE_DATA_PITCH_ALIGNMENT * D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    return (Footprint){
        .location = {.pResource = buffer,
                     .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
                     .PlacedFootprint = {.Offset = placed,
                                         .Footprint = {.Format = CopyFormatOf(def->format, aspect),
                                                       .Width = dx * block.width + size[0],
                                                       .Height = size[1],
                                                       .Depth = size[2],
                                                       .RowPitch = pitch}}},
        .dx = dx * block.width,
    };
}

// Copies one box between a buffer placed at an offset and a texture's
// subresource at an origin.
static void CopyBox(const mrhiD3d12Recorder* recorder, bool toTexture, ID3D12Resource* buffer,
                    uint64_t offset, uint32_t rowPitch, ID3D12Resource* texture,
                    const mrhiTextureDef* def, uint8_t aspect, UINT subresource,
                    const uint32_t origin[3], const uint32_t size[3])
{
    Footprint footprint = FootprintOf(buffer, def, aspect, offset, rowPitch, size);
    D3D12_TEXTURE_COPY_LOCATION image = TextureAt(texture, subresource);
    if (toTexture)
    {
        D3D12_BOX box = {footprint.dx, 0, 0, footprint.dx + size[0], size[1], size[2]};
        ID3D12GraphicsCommandList_CopyTextureRegion(recorder->list, &image, origin[0], origin[1],
                                                    origin[2], &footprint.location, &box);
        return;
    }
    D3D12_BOX box = {origin[0],           origin[1],           origin[2],
                     origin[0] + size[0], origin[1] + size[1], origin[2] + size[2]};
    ID3D12GraphicsCommandList_CopyTextureRegion(recorder->list, &footprint.location, footprint.dx,
                                                0, 0, &image, &box);
}

// Copies a subresource's box a row of blocks at a time, for a buffer
// offset D3D12 cannot place.
static void CopyRows(const mrhiD3d12Recorder* recorder, bool toTexture, ID3D12Resource* buffer,
                     const mrhiCommandBufferSide* side, uint64_t offset, ID3D12Resource* texture,
                     const mrhiTextureDef* def, uint8_t aspect, UINT subresource,
                     const uint32_t origin[3], const uint32_t size[3])
{
    mrhiFormatBlock block = mrhiGetFormatBlock(def->format);
    for (uint32_t z = 0; z < size[2]; ++z)
    {
        for (uint32_t row = 0; row < size[1] / block.height; ++row)
        {
            uint64_t at = offset + (uint64_t)z * side->bytesPerRow * side->rowsPerImage +
                          (uint64_t)row * side->bytesPerRow;
            const uint32_t place[3] = {origin[0], origin[1] + row * block.height, origin[2] + z};
            const uint32_t one[3] = {size[0], block.height, 1};
            CopyBox(recorder, toTexture, buffer, at, side->bytesPerRow, texture, def, aspect,
                    subresource, place, one);
        }
    }
}

static void CopyWithTexture(mrhiD3d12Recorder* recorder, const mrhiCommand* command)
{
    bool toTexture =
        command->type == mrhiCommandCopyBufferToTexture || command->type == mrhiCommandWriteTexture;
    bool readback = command->type == mrhiCommandReadTexture;
    mrhiCommandBufferSide side;
    mrhiCommandTextureSide texel;
    memcpy(&side, toTexture ? &command[1] : &command[2], sizeof(side));
    memcpy(&texel, toTexture ? &command[2] : &command[1], sizeof(texel));
    MRHI_ASSERT(texel.object != 0 && texel.object <= recorder->frame->resourceCount);
    const mrhiD3d12Object* image = &recorder->table[texel.object - 1];
    const mrhiTextureDef* def = image->texture;
    ID3D12Resource* buffer =
        BufferOf(recorder, side.object, readback,
                 toTexture ? D3D12_RESOURCE_STATE_COPY_SOURCE : D3D12_RESOURCE_STATE_COPY_DEST);
    mrhiD3d12FlushBarriers(recorder);
    bool volume = def->kind == mrhi_texture3d;
    uint32_t depth = (uint32_t)command->d;
    uint64_t imageBytes = (uint64_t)side.bytesPerRow * side.rowsPerImage;
    if (readback)
    {
        mrhiFormatBlock block = mrhiGetFormatBlock(def->format);
        uint64_t rows = (uint32_t)command->c / block.height;
        uint64_t rowBytes = (uint64_t)((uint32_t)command->b / block.width) *
                            mrhiGetFormatCopy(def->format, texel.aspect).bytes;
        Note(recorder, side.offset,
             imageBytes * (depth - 1) + (uint64_t)side.bytesPerRow * (rows - 1) + rowBytes);
    }
    const uint32_t size[3] = {(uint32_t)command->b, (uint32_t)command->c, volume ? depth : 1};
    for (uint32_t layer = 0; layer < (volume ? 1 : depth); ++layer)
    {
        UINT subresource =
            SubresourceOf(def, texel.mip, volume ? 0 : texel.z + layer, texel.aspect);
        const uint32_t origin[3] = {texel.x, texel.y, volume ? texel.z : 0};
        uint64_t offset = side.offset + layer * imageBytes;
        if (offset % PLACEMENT == 0)
        {
            CopyBox(recorder, toTexture, buffer, offset, side.bytesPerRow, image->resource, def,
                    texel.aspect, subresource, origin, size);
        }
        else
        {
            CopyRows(recorder, toTexture, buffer, &side, offset, image->resource, def, texel.aspect,
                     subresource, origin, size);
        }
    }
}

static void CopyTexture(const mrhiD3d12Recorder* recorder, const mrhiCommand* command)
{
    mrhiCommandTextureSide sides[2];
    memcpy(sides, &command[1], sizeof(sides));
    const mrhiD3d12Object* source = &recorder->table[sides[0].object - 1];
    const mrhiD3d12Object* target = &recorder->table[sides[1].object - 1];
    bool volume = source->texture->kind == mrhi_texture3d;
    uint32_t depth = (uint32_t)command->d;
    for (uint32_t layer = 0; layer < (volume ? 1 : depth); ++layer)
    {
        D3D12_TEXTURE_COPY_LOCATION from = TextureAt(
            source->resource, SubresourceOf(source->texture, sides[0].mip,
                                            volume ? 0 : sides[0].z + layer, sides[0].aspect));
        D3D12_TEXTURE_COPY_LOCATION to = TextureAt(
            target->resource, SubresourceOf(target->texture, sides[1].mip,
                                            volume ? 0 : sides[1].z + layer, sides[1].aspect));
        uint32_t z = volume ? sides[0].z : 0;
        D3D12_BOX box = {sides[0].x,
                         sides[0].y,
                         z,
                         sides[0].x + (uint32_t)command->b,
                         sides[0].y + (uint32_t)command->c,
                         z + (volume ? depth : 1)};
        ID3D12GraphicsCommandList_CopyTextureRegion(recorder->list, &to, sides[1].x, sides[1].y,
                                                    volume ? sides[1].z : 0, &from, &box);
    }
}

void mrhiD3d12Copy(mrhiD3d12Recorder* recorder, const mrhiCommand* command)
{
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
            Note(recorder, sides[1].offset, command->b);
        }
        ID3D12Resource* source =
            BufferOf(recorder, sides[0].object, false, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ID3D12Resource* target =
            BufferOf(recorder, sides[1].object, readback, D3D12_RESOURCE_STATE_COPY_DEST);
        mrhiD3d12FlushBarriers(recorder);
        ID3D12GraphicsCommandList_CopyBufferRegion(recorder->list, target, sides[1].offset, source,
                                                   sides[0].offset, command->b);
        break;
    }
    case mrhiCommandCopyTexture:
        CopyTexture(recorder, command);
        break;
    default:
        CopyWithTexture(recorder, command);
        break;
    }
}
