// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Uploads (mrhi-0011): the program's bytes copied at the call into the
// open frame's staging, each upload at a 512-byte boundary and a
// texture's rows at a 256-byte pitch, and recorded as a copy from the
// staging, whose side names no resource.

#include "copy_core.h"

#include <stdatomic.h>
#include <stdckdint.h>
#include <string.h>

// The alignments every API's copy from staging accepts: D3D12's
// placement of a texture's data, and its row pitch.
#define PLACEMENT 512
#define PITCH     256

// Takes bytes of the open frame's staging at a placement boundary: a
// pointer to them and their offset in the frame's region, or NULL when
// the staging is full, which marks the pass as a full arena does.
static uint8_t* TakeStaging(mrhiDevice* device, mrhiFramePass* pass, uint64_t bytes,
                            uint64_t* offsetOut)
{
    uint64_t room = device->deviceLimits.frameUploadBytes;
    if (pass->overflowed || bytes > room)
    {
        pass->overflowed = true;
        return nullptr;
    }
    uint64_t rounded = (bytes + PLACEMENT - 1) / PLACEMENT * PLACEMENT;
    uint64_t at = atomic_fetch_add_explicit(&device->stagingTaken, rounded, memory_order_relaxed);
    if (at > room - bytes)
    {
        pass->overflowed = true;
        return nullptr;
    }
    *offsetOut = at;
    return device->frameStaging + (size_t)device->stagingRegion * room + at;
}

mrhiResult mrhiWriteBuffer(mrhiDevice* device, mrhiPassId id, mrhiResourceId resource,
                           uint64_t offset, const void* bytes, uint64_t size)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (bytes == nullptr && size > 0)
    {
        return mrhiDeviceMisuse(device);
    }
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = mrhiCopyPass(device, id, &status);
    if (pass == nullptr)
    {
        return status;
    }
    uint32_t object = mrhiFindKind(device, resource, true, &status);
    if (object == 0)
    {
        return mrhiRefuse(device, status);
    }
    uint64_t total = mrhiBufferBytesOf(&device->frameResources[object - 1]);
    if (offset % 4 != 0 || size % 4 != 0 || offset > total || size > total - offset ||
        !mrhiPassDeclares(device, pass, object, MRHI_KIND(mrhi_accessCopyDestination), nullptr))
    {
        return mrhiDeviceMisuse(device);
    }
    uint64_t staged = 0;
    uint8_t* staging = TakeStaging(device, pass, size, &staged);
    mrhiCommand* records = staging == nullptr
                               ? nullptr
                               : mrhiTakeCopy(device, pass, mrhiCommandWriteBuffer, size, 1, 1);
    if (records == nullptr)
    {
        return mrhi_errorCapacity;
    }
    if (size > 0)
    {
        memcpy(staging, bytes, size);
    }
    mrhiCommandBufferSide sides[2] = {
        {.offset = staged},
        {.object = object, .offset = offset},
    };
    memcpy(&records[1], sides, sizeof(sides));
    return mrhi_success;
}

// Copies texels from the program's layout into staging rows of a pitch.
static void Repack(uint8_t* staging, uint64_t pitch, const uint8_t* bytes,
                   const mrhiTexelLayout* layout, uint64_t rows, uint64_t rowBytes, uint32_t layers)
{
    for (uint64_t z = 0; z < layers; ++z)
    {
        for (uint64_t y = 0; y < rows; ++y)
        {
            const uint8_t* from =
                bytes + layout->offset + (z * layout->rowsPerImage + y) * layout->bytesPerRow;
            memcpy(staging + (z * rows + y) * pitch, from, rowBytes);
        }
    }
}

mrhiResult mrhiWriteTexture(mrhiDevice* device, mrhiPassId id, const mrhiTextureCopy* destination,
                            const void* bytes, size_t byteCount, const mrhiTexelLayout* layout,
                            const mrhiExtent3d* size)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (destination == nullptr || layout == nullptr || size == nullptr ||
        (bytes == nullptr && byteCount > 0))
    {
        return mrhiDeviceMisuse(device);
    }
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = mrhiCopyPass(device, id, &status);
    if (pass == nullptr)
    {
        return status;
    }
    mrhiTextureSide side;
    mrhiFormatCopy facts;
    status = mrhiCheckTextureTransfer(device, destination, size, false, &side, &facts);
    if (status != mrhi_success)
    {
        return mrhiRefuse(device, status);
    }
    mrhiFormatBlock block = mrhiGetFormatBlock(side.def->format);
    if (!mrhiIsLayoutValid(layout->offset, layout->bytesPerRow, layout->rowsPerImage, byteCount,
                           block, facts.bytes, size, false) ||
        !mrhiPassDeclares(device, pass, side.object, MRHI_KIND(mrhi_accessCopyDestination),
                          &side.part))
    {
        return mrhiDeviceMisuse(device);
    }
    uint64_t rows = size->height / block.height;
    uint64_t rowBytes = (uint64_t)(size->width / block.width) * facts.bytes;
    uint64_t pitch = (rowBytes + PITCH - 1) / PITCH * PITCH;
    // Bytes past 64 bits are past any staging.
    uint64_t stagedBytes = 0;
    if (ckd_mul(&stagedBytes, pitch * rows, (uint64_t)size->depthOrLayers))
    {
        stagedBytes = UINT64_MAX;
    }
    uint64_t staged = 0;
    uint8_t* staging = TakeStaging(device, pass, stagedBytes, &staged);
    mrhiCommand* records = staging == nullptr
                               ? nullptr
                               : mrhiTakeCopy(device, pass, mrhiCommandWriteTexture, size->width,
                                              size->height, size->depthOrLayers);
    if (records == nullptr)
    {
        return mrhi_errorCapacity;
    }
    // No bytes means none to copy: the layout fit zero of them.
    if (bytes != nullptr)
    {
        Repack(staging, pitch, bytes, layout, rows, rowBytes, size->depthOrLayers);
    }
    mrhiCommandBufferSide from = {
        .bytesPerRow = (uint32_t)pitch,
        .offset = staged,
        .rowsPerImage = (uint32_t)rows,
    };
    mrhiCommandTextureSide to = {
        .object = side.object,
        .mip = destination->mip,
        .x = destination->x,
        .y = destination->y,
        .z = destination->z,
        .aspect = destination->aspect,
    };
    memcpy(&records[1], &from, sizeof(from));
    memcpy(&records[2], &to, sizeof(to));
    return mrhi_success;
}
