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

#include <limits.h>
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

// Takes a clear's views, its CPU-only one and a shader-visible one:
// false, with neither taken, when either ring has run out.
static bool TakeClearViews(mrhiD3d12Recorder* recorder, D3D12_CPU_DESCRIPTOR_HANDLE* cpuOut,
                           D3D12_CPU_DESCRIPTOR_HANDLE* visibleOut,
                           D3D12_GPU_DESCRIPTOR_HANDLE* gpuOut)
{
    mrhiD3d12Ring* clears = &recorder->clears;
    mrhiD3d12GpuRing* views = &recorder->views;
    if (clears->taken == clears->capacity || views->taken == views->capacity)
    {
        return false;
    }
    cpuOut->ptr = clears->start.ptr + (SIZE_T)clears->step * clears->taken++;
    visibleOut->ptr = views->cpu.ptr + (SIZE_T)views->step * views->taken;
    gpuOut->ptr = views->gpu.ptr + (UINT64)views->step * views->taken++;
    return true;
}

// Zeroes a buffer's range with ClearUnorderedAccessViewUint, many times
// faster than copies of zeros, on the lists that take it: every lane but
// the copy lane, which no pass that clears runs on. A clear
// past the slot's views, or of more words than one view holds, copies
// zeros instead. Either way it ends in the copy destination state the
// frame's plan gives it, so the frame's next use of the buffer waits
// for it.
static void Clear(mrhiD3d12Recorder* recorder, const mrhiCommand* command)
{
    MRHI_ASSERT(recorder->lane != mrhiD3d12LaneCopy);
    uint64_t words = command->d / sizeof(uint32_t);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE visible;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu;
    if (words > (UINT64_C(1) << D3D12_REQ_BUFFER_RESOURCE_TEXEL_COUNT_2_TO_EXP) ||
        !TakeClearViews(recorder, &cpu, &visible, &gpu))
    {
        ID3D12Resource* target =
            BufferOf(recorder, command->a, false, D3D12_RESOURCE_STATE_COPY_DEST);
        mrhiD3d12FlushBarriers(recorder);
        mrhiD3d12CopyZeros(recorder, target, command->c, command->d);
        return;
    }
    ID3D12Resource* target =
        BufferOf(recorder, command->a, false, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    mrhiD3d12FlushBarriers(recorder);
    const D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {
        .Format = DXGI_FORMAT_R32_UINT,
        .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
        .Buffer = {.FirstElement = command->c / sizeof(uint32_t), .NumElements = (UINT)words},
    };
    ID3D12Device_CreateUnorderedAccessView(recorder->device, target, nullptr, &desc, cpu);
    ID3D12Device_CreateUnorderedAccessView(recorder->device, target, nullptr, &desc, visible);
    static const UINT zeros[4] = {0, 0, 0, 0};
    ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(recorder->list, gpu, cpu, target, zeros,
                                                           0, nullptr);
    (void)BufferOf(recorder, command->a, false, D3D12_RESOURCE_STATE_COPY_DEST);
    mrhiD3d12FlushBarriers(recorder);
}

bool mrhiD3d12ClearsOnDirect(const mrhiTextureDef* def)
{
    return mrhiFormatHasDepth(def->format) || (def->usage & mrhi_textureRenderTarget) != 0;
}

// The descriptor one past a ring's, which a slot's heap holds for the
// clears of new textures: D3D12 reads a target or depth view when a
// clear is recorded, so each clear takes the same one again.
static D3D12_CPU_DESCRIPTOR_HANDLE PastRing(const mrhiD3d12Ring* ring)
{
    return (D3D12_CPU_DESCRIPTOR_HANDLE){.ptr =
                                             ring->start.ptr + (SIZE_T)ring->step * ring->capacity};
}

static uint32_t LayersOf(const mrhiTextureDef* def)
{
    return def->kind == mrhi_texture3d ? 1 : def->depthOrLayers;
}

// Clears a depth texture's mip, both its planes, through a view of every
// layer.
static void ClearDepthMip(const mrhiD3d12Recorder* recorder, ID3D12Resource* resource,
                          const mrhiTextureDef* def, uint32_t mip)
{
    D3D12_DEPTH_STENCIL_VIEW_DESC desc = {.Format = mrhiD3d12Format(def->format)};
    if (def->sampleCount > 1)
    {
        desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
        desc.Texture2DMSArray = (D3D12_TEX2DMS_ARRAY_DSV){.ArraySize = LayersOf(def)};
    }
    else
    {
        desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray = (D3D12_TEX2D_ARRAY_DSV){.MipSlice = mip, .ArraySize = LayersOf(def)};
    }
    D3D12_CPU_DESCRIPTOR_HANDLE view = PastRing(&recorder->depths);
    ID3D12Device_CreateDepthStencilView(recorder->device, resource, &desc, view);
    D3D12_CLEAR_FLAGS flags =
        D3D12_CLEAR_FLAG_DEPTH |
        (mrhiFormatHasStencil(def->format) ? D3D12_CLEAR_FLAG_STENCIL : (D3D12_CLEAR_FLAGS)0);
    ID3D12GraphicsCommandList_ClearDepthStencilView(recorder->list, view, flags, 0.0F, 0, 0,
                                                    nullptr);
}

// Clears a render target's mip through a view of every layer or depth
// slice.
static void ClearColorMip(const mrhiD3d12Recorder* recorder, ID3D12Resource* resource,
                          const mrhiTextureDef* def, uint32_t mip)
{
    D3D12_RENDER_TARGET_VIEW_DESC desc = {.Format = mrhiD3d12Format(def->format)};
    if (def->kind == mrhi_texture3d)
    {
        desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
        desc.Texture3D = (D3D12_TEX3D_RTV){.MipSlice = mip, .WSize = UINT_MAX};
    }
    else if (def->sampleCount > 1)
    {
        desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
        desc.Texture2DMSArray = (D3D12_TEX2DMS_ARRAY_RTV){.ArraySize = LayersOf(def)};
    }
    else
    {
        desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray = (D3D12_TEX2D_ARRAY_RTV){.MipSlice = mip, .ArraySize = LayersOf(def)};
    }
    D3D12_CPU_DESCRIPTOR_HANDLE view = PastRing(&recorder->targets);
    ID3D12Device_CreateRenderTargetView(recorder->device, resource, &desc, view);
    static const FLOAT zero[4] = {0.0F, 0.0F, 0.0F, 0.0F};
    ID3D12GraphicsCommandList_ClearRenderTargetView(recorder->list, view, zero, 0, nullptr);
}

// Clears a storage texture's mip through a view of every layer or depth
// slice. ClearUnorderedAccessViewUint copies the bits of its values
// into any format, and zero bits are zero in each.
static void ClearStorageMip(mrhiD3d12Recorder* recorder, ID3D12Resource* resource,
                            const mrhiTextureDef* def, uint32_t mip)
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE visible;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu;
    bool taken = TakeClearViews(recorder, &cpu, &visible, &gpu);
    MRHI_ASSERT(taken);
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {.Format = mrhiD3d12Format(def->format)};
    if (def->kind == mrhi_texture3d)
    {
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        desc.Texture3D = (D3D12_TEX3D_UAV){.MipSlice = mip, .WSize = UINT_MAX};
    }
    else
    {
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray = (D3D12_TEX2D_ARRAY_UAV){.MipSlice = mip, .ArraySize = LayersOf(def)};
    }
    ID3D12Device_CreateUnorderedAccessView(recorder->device, resource, nullptr, &desc, cpu);
    ID3D12Device_CreateUnorderedAccessView(recorder->device, resource, nullptr, &desc, visible);
    static const UINT zeros[4] = {0, 0, 0, 0};
    ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(recorder->list, gpu, cpu, resource,
                                                           zeros, 0, nullptr);
}

// Copies the device's zeros over a mip's layer or depth slice, in boxes
// of as many rows of blocks as the zeros hold at D3D12's row pitch:
// they hold a row of the widest texture D3D12 takes, 16384 texels of 16
// bytes.
static void CopyZerosToMip(const mrhiD3d12Recorder* recorder, ID3D12Resource* resource,
                           const mrhiTextureDef* def, uint32_t mip, uint32_t z)
{
    mrhiFormatBlock block = mrhiGetFormatBlock(def->format);
    bool volume = def->kind == mrhi_texture3d;
    uint32_t across =
        ((def->width >> mip > 0 ? def->width >> mip : 1) + block.width - 1) / block.width;
    uint32_t down =
        ((def->height >> mip > 0 ? def->height >> mip : 1) + block.height - 1) / block.height;
    uint32_t pitch = (across * mrhiGetFormatCopy(def->format, mrhi_aspectAll).bytes +
                      D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) /
                     D3D12_TEXTURE_DATA_PITCH_ALIGNMENT * D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    MRHI_ASSERT(pitch > 0 && pitch <= MRHI_D3D12_ZERO_BYTES);
    uint32_t rows = (uint32_t)(MRHI_D3D12_ZERO_BYTES / pitch);
    UINT subresource = SubresourceOf(def, mip, volume ? 0 : z, mrhi_aspectAll);
    for (uint32_t y = 0; y < down; y += rows)
    {
        const uint32_t origin[3] = {0, y * block.height, volume ? z : 0};
        const uint32_t size[3] = {across * block.width,
                                  (down - y < rows ? down - y : rows) * block.height, 1};
        CopyBox(recorder, true, recorder->zeros, 0, pitch, resource, def, mrhi_aspectAll,
                subresource, origin, size);
    }
}

// Zeroes a texture's mip in the state its clear takes.
static void ZeroMip(mrhiD3d12Recorder* recorder, const mrhiD3d12Object* object,
                    D3D12_RESOURCE_STATES state, uint32_t mip)
{
    const mrhiTextureDef* def = object->texture;
    switch (state)
    {
    case D3D12_RESOURCE_STATE_DEPTH_WRITE:
        ClearDepthMip(recorder, object->resource, def, mip);
        break;
    case D3D12_RESOURCE_STATE_RENDER_TARGET:
        ClearColorMip(recorder, object->resource, def, mip);
        break;
    case D3D12_RESOURCE_STATE_UNORDERED_ACCESS:
        ClearStorageMip(recorder, object->resource, def, mip);
        break;
    default:
    {
        uint32_t depth = def->depthOrLayers >> mip > 0 ? def->depthOrLayers >> mip : 1;
        uint32_t slices = def->kind == mrhi_texture3d ? depth : def->depthOrLayers;
        for (uint32_t z = 0; z < slices; ++z)
        {
            CopyZerosToMip(recorder, object->resource, def, mip, z);
        }
        break;
    }
    }
}

// Zeroes a new texture whole, every mip, layer and plane, in the copy
// destination state the frame's plan gives it, by the usage it was made
// with: a render target and a depth texture through views of each mip,
// on the direct list that alone clears them; a storage texture through
// views of each mip while the slot's views last; any other by copies of
// zeros, which take no usage.
static void ClearTexture(mrhiD3d12Recorder* recorder, const mrhiCommand* command)
{
    MRHI_ASSERT(command->a != 0 && command->a <= recorder->frame->resourceCount);
    const mrhiD3d12Object* object = &recorder->table[command->a - 1];
    const mrhiTextureDef* def = object->texture;
    bool depth = mrhiFormatHasDepth(def->format);
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
    if (mrhiD3d12ClearsOnDirect(def))
    {
        state = depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    else if ((def->usage & mrhi_textureStorage) != 0 &&
             recorder->clears.capacity - recorder->clears.taken >= def->mipLevels &&
             recorder->views.capacity - recorder->views.taken >= def->mipLevels)
    {
        state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    mrhiD3d12Transition(recorder, object->resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                        D3D12_RESOURCE_STATE_COPY_DEST, state);
    mrhiD3d12FlushBarriers(recorder);
    for (uint32_t mip = 0; mip < def->mipLevels; ++mip)
    {
        ZeroMip(recorder, object, state, mip);
    }
    mrhiD3d12Transition(recorder, object->resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, state,
                        D3D12_RESOURCE_STATE_COPY_DEST);
    mrhiD3d12FlushBarriers(recorder);
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
    case mrhiCommandClearBuffer:
        Clear(recorder, command);
        break;
    case mrhiCommandClearTexture:
        ClearTexture(recorder, command);
        break;
    default:
        CopyWithTexture(recorder, command);
        break;
    }
}

void mrhiD3d12CopyZeros(const mrhiD3d12Recorder* recorder, ID3D12Resource* target, uint64_t offset,
                        uint64_t bytes)
{
    // The zeros are read where buffers promote to a copy source on their
    // own.
    for (uint64_t done = 0; done < bytes; done += MRHI_D3D12_ZERO_BYTES)
    {
        uint64_t piece =
            bytes - done < MRHI_D3D12_ZERO_BYTES ? bytes - done : MRHI_D3D12_ZERO_BYTES;
        ID3D12GraphicsCommandList_CopyBufferRegion(recorder->list, target, offset + done,
                                                   recorder->zeros, 0, piece);
    }
}
