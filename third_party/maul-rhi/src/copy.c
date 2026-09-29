// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Copies (mrhi-0011): between buffers, buffers and textures, and
// textures, in a pass without targets, each checked as WebGPU checks it
// and against the copy accesses the pass declares.

#include "copy_core.h"

#include <stdckdint.h>
#include <string.h>

mrhiFramePass* mrhiCopyPass(mrhiDevice* device, mrhiPassId id, mrhiResult* statusOut)
{
    mrhiFramePass* pass = mrhiRecordingPass(device, id, statusOut);
    if (pass != nullptr && mrhiWorkOf(pass) == mrhiWorkRender)
    {
        *statusOut = mrhiDeviceMisuse(device);
        return nullptr;
    }
    return pass;
}

uint32_t mrhiFindKind(const mrhiDevice* device, mrhiResourceId id, bool buffer,
                      mrhiResult* statusOut)
{
    uint32_t object = mrhiFindFrameResource(device, id);
    if (object == 0)
    {
        *statusOut = mrhi_errorStale;
        return 0;
    }
    mrhiFrameResourceKind kind = device->frameResources[object - 1].kind;
    if ((kind == mrhiFrameBuffer || kind == mrhiImportedBuffer) != buffer)
    {
        *statusOut = mrhi_errorInvalid;
        return 0;
    }
    return object;
}

mrhiResult mrhiRefuse(mrhiDevice* device, mrhiResult status)
{
    return status == mrhi_errorInvalid ? mrhiDeviceMisuse(device) : status;
}

mrhiCommand* mrhiTakeCopy(mrhiDevice* device, mrhiFramePass* pass, mrhiCommandType type,
                          uint64_t width, uint64_t height, uint64_t depth)
{
    mrhiCommand* records = mrhiTakeCommands(device, pass, 3);
    if (records != nullptr)
    {
        records[0] = (mrhiCommand){
            .type = (uint16_t)type,
            .payload = 2,
            .b = width,
            .c = height,
            .d = depth,
        };
    }
    return records;
}

mrhiResult mrhiCopyBuffer(mrhiDevice* device, mrhiPassId id, mrhiResourceId source,
                          uint64_t sourceOffset, mrhiResourceId destination,
                          uint64_t destinationOffset, uint64_t size)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = mrhiCopyPass(device, id, &status);
    if (pass == nullptr)
    {
        return status;
    }
    uint32_t from = mrhiFindKind(device, source, true, &status);
    uint32_t to = from == 0 ? 0 : mrhiFindKind(device, destination, true, &status);
    if (to == 0)
    {
        return mrhiRefuse(device, status);
    }
    uint64_t fromBytes = mrhiBufferBytesOf(&device->frameResources[from - 1]);
    uint64_t toBytes = mrhiBufferBytesOf(&device->frameResources[to - 1]);
    // One buffer as both is refused by the pass's declarations: a pass
    // never declares a buffer as both a copy source and destination.
    if (size % 4 != 0 || sourceOffset % 4 != 0 || destinationOffset % 4 != 0 ||
        sourceOffset > fromBytes || size > fromBytes - sourceOffset ||
        destinationOffset > toBytes || size > toBytes - destinationOffset ||
        !mrhiPassDeclares(device, pass, from, MRHI_KIND(mrhi_accessCopySource), nullptr) ||
        !mrhiPassDeclares(device, pass, to, MRHI_KIND(mrhi_accessCopyDestination), nullptr))
    {
        return mrhiDeviceMisuse(device);
    }
    mrhiCommand* records = mrhiTakeCopy(device, pass, mrhiCommandCopyBuffer, size, 1, 1);
    if (records == nullptr)
    {
        return mrhi_errorCapacity;
    }
    mrhiCommandBufferSide sides[2] = {
        {.object = from, .offset = sourceOffset},
        {.object = to, .offset = destinationOffset},
    };
    memcpy(&records[1], sides, sizeof(sides));
    return mrhi_success;
}

// The layers of a texture at a mip: its depth there for a 3D texture.
static uint32_t LayersAt(const mrhiTextureDef* def, uint32_t mip)
{
    if (def->kind != mrhi_texture3d)
    {
        return def->depthOrLayers;
    }
    uint32_t depth = def->depthOrLayers >> mip;
    return depth > 0 ? depth : 1;
}

// A mip's size rounded up to whole blocks.
static uint32_t Physical(uint32_t size, uint32_t mip, uint32_t block)
{
    uint32_t texels = size >> mip > 0 ? size >> mip : 1;
    return (uint32_t)(((uint64_t)texels + block - 1) / block * block);
}

mrhiResult mrhiCheckTextureSide(const mrhiDevice* device, const mrhiTextureCopy* copy,
                                const mrhiExtent3d* size, mrhiTextureSide* sideOut)
{
    mrhiResult status = mrhi_success;
    uint32_t object = mrhiFindKind(device, copy->resource, false, &status);
    if (object == 0)
    {
        return status;
    }
    const mrhiTextureDef* def = mrhiFrameTextureOf(&device->frameResources[object - 1]);
    // A mip past the texture's is refused by the pass's declarations,
    // which never name one.
    if (copy->aspect > mrhi_aspectStencilOnly || !mrhiFormatHasAspect(def->format, copy->aspect))
    {
        return mrhi_errorInvalid;
    }
    mrhiFormatBlock block = mrhiGetFormatBlock(def->format);
    uint64_t width = Physical(def->width, copy->mip, block.width);
    uint64_t height = Physical(def->height, copy->mip, block.height);
    uint64_t layers = LayersAt(def, copy->mip);
    bool whole = mrhiFormatHasDepth(def->format) || def->sampleCount > 1;
    if (copy->x % block.width != 0 || copy->y % block.height != 0 ||
        size->width % block.width != 0 || size->height % block.height != 0 ||
        (uint64_t)copy->x + size->width > width || (uint64_t)copy->y + size->height > height ||
        (uint64_t)copy->z + size->depthOrLayers > layers ||
        (whole &&
         (size->width != width || size->height != height || size->depthOrLayers != layers)))
    {
        return mrhi_errorInvalid;
    }
    bool flat = def->kind != mrhi_texture3d;
    *sideOut = (mrhiTextureSide){
        .object = object,
        .def = def,
        .aspect = copy->aspect,
        .part =
            {
                .planes = copy->aspect == mrhi_aspectStencilOnly ? 2
                          : copy->aspect == mrhi_aspectDepthOnly ? 1
                                                                 : mrhiFormatPlanes(def->format),
                .baseMip = copy->mip,
                .mipCount = 1,
                .baseLayer = flat ? copy->z : 0,
                .layerCount = flat ? size->depthOrLayers : 1,
            },
    };
    return mrhi_success;
}

mrhiFormatCopy mrhiSideCopyFacts(const mrhiTextureSide* side)
{
    mrhiFormat format = side->def->format;
    if (!mrhiFormatHasDepth(format))
    {
        return mrhiGetFormatCopy(format, mrhi_aspectAll);
    }
    mrhiTextureAspect aspect = side->aspect;
    if (aspect == mrhi_aspectAll && !mrhiFormatHasStencil(format))
    {
        aspect = mrhi_aspectDepthOnly;
    }
    return mrhiGetFormatCopy(format, aspect);
}

bool mrhiIsLayoutValid(uint64_t offset, uint32_t bytesPerRow, uint32_t rowsPerImage, uint64_t total,
                       mrhiFormatBlock block, uint32_t bytes, const mrhiExtent3d* size,
                       bool aligned)
{
    uint64_t rows = size->height / block.height;
    uint64_t lastRow = (uint64_t)(size->width / block.width) * bytes;
    uint64_t perRow = bytesPerRow;
    uint64_t perImage = rowsPerImage;
    if ((aligned && perRow % 256 != 0) || (rows > 1 && perRow == 0) ||
        (size->depthOrLayers > 1 && (perRow == 0 || perImage == 0)) ||
        (perRow != 0 && perRow < lastRow) || (perImage != 0 && perImage < rows))
    {
        return false;
    }
    // Rows and images of 32-bit counts multiply within 64 bits; their
    // sums may not.
    uint64_t required = 0;
    bool overflow = false;
    if (size->depthOrLayers > 0)
    {
        overflow = ckd_mul(&required, perRow * perImage, (uint64_t)size->depthOrLayers - 1);
        if (rows > 0)
        {
            overflow = overflow || ckd_add(&required, required, perRow * (rows - 1)) ||
                       ckd_add(&required, required, lastRow);
        }
    }
    return !overflow && offset <= total && required <= total - offset;
}

mrhiResult mrhiCheckTextureTransfer(const mrhiDevice* device, const mrhiTextureCopy* texture,
                                    const mrhiExtent3d* size, bool fromTexture,
                                    mrhiTextureSide* sideOut, mrhiFormatCopy* factsOut)
{
    mrhiResult status = mrhiCheckTextureSide(device, texture, size, sideOut);
    if (status != mrhi_success)
    {
        return status;
    }
    *factsOut = mrhiSideCopyFacts(sideOut);
    bool direction = fromTexture ? factsOut->source : factsOut->destination;
    return sideOut->def->sampleCount == 1 && direction ? mrhi_success : mrhi_errorInvalid;
}

// Checks a copy between a buffer and a texture, the texture being the
// source when asked: success with the sides resolved, or the refusal.
static mrhiResult CheckBufferTexture(const mrhiDevice* device, const mrhiFramePass* pass,
                                     const mrhiBufferCopy* buffer, const mrhiTextureCopy* texture,
                                     const mrhiExtent3d* size, bool fromTexture,
                                     uint32_t* bufferOut, mrhiTextureSide* textureOut)
{
    mrhiResult status = mrhi_success;
    uint32_t object = mrhiFindKind(device, buffer->resource, true, &status);
    if (object == 0)
    {
        return status;
    }
    mrhiFormatCopy facts;
    status = mrhiCheckTextureTransfer(device, texture, size, fromTexture, textureOut, &facts);
    if (status != mrhi_success)
    {
        return status;
    }
    const mrhiTextureDef* def = textureOut->def;
    uint32_t alignment = mrhiFormatHasDepth(def->format) ? 4 : facts.bytes;
    uint64_t total = mrhiBufferBytesOf(&device->frameResources[object - 1]);
    bool valid = buffer->offset % alignment == 0 &&
                 mrhiIsLayoutValid(buffer->offset, buffer->bytesPerRow, buffer->rowsPerImage, total,
                                   mrhiGetFormatBlock(def->format), facts.bytes, size, true);
    mrhiAccessKind bufferKind = fromTexture ? mrhi_accessCopyDestination : mrhi_accessCopySource;
    mrhiAccessKind textureKind = fromTexture ? mrhi_accessCopySource : mrhi_accessCopyDestination;
    if (!valid || !mrhiPassDeclares(device, pass, object, MRHI_KIND(bufferKind), nullptr) ||
        !mrhiPassDeclares(device, pass, textureOut->object, MRHI_KIND(textureKind),
                          &textureOut->part))
    {
        return mrhi_errorInvalid;
    }
    *bufferOut = object;
    return mrhi_success;
}

// Records a copy between a buffer and a texture: success, or the
// refusal.
static mrhiResult CopyBufferTexture(mrhiDevice* device, mrhiPassId id, const mrhiBufferCopy* buffer,
                                    const mrhiTextureCopy* texture, const mrhiExtent3d* size,
                                    bool fromTexture)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (buffer == nullptr || texture == nullptr || size == nullptr)
    {
        return mrhiDeviceMisuse(device);
    }
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = mrhiCopyPass(device, id, &status);
    if (pass == nullptr)
    {
        return status;
    }
    uint32_t object = 0;
    mrhiTextureSide side;
    status = CheckBufferTexture(device, pass, buffer, texture, size, fromTexture, &object, &side);
    if (status != mrhi_success)
    {
        return mrhiRefuse(device, status);
    }
    mrhiCommandType type =
        fromTexture ? mrhiCommandCopyTextureToBuffer : mrhiCommandCopyBufferToTexture;
    mrhiCommand* records =
        mrhiTakeCopy(device, pass, type, size->width, size->height, size->depthOrLayers);
    if (records == nullptr)
    {
        return mrhi_errorCapacity;
    }
    mrhiCommandBufferSide bufferSide = {
        .object = object,
        .bytesPerRow = buffer->bytesPerRow,
        .offset = buffer->offset,
        .rowsPerImage = buffer->rowsPerImage,
    };
    mrhiCommandTextureSide textureSide = {
        .object = side.object,
        .mip = texture->mip,
        .x = texture->x,
        .y = texture->y,
        .z = texture->z,
        .aspect = texture->aspect,
    };
    memcpy(&records[fromTexture ? 2 : 1], &bufferSide, sizeof(bufferSide));
    memcpy(&records[fromTexture ? 1 : 2], &textureSide, sizeof(textureSide));
    return mrhi_success;
}

mrhiResult mrhiCopyBufferToTexture(mrhiDevice* device, mrhiPassId pass,
                                   const mrhiBufferCopy* source, const mrhiTextureCopy* destination,
                                   const mrhiExtent3d* size)
{
    return CopyBufferTexture(device, pass, source, destination, size, false);
}

mrhiResult mrhiCopyTextureToBuffer(mrhiDevice* device, mrhiPassId pass,
                                   const mrhiTextureCopy* source, const mrhiBufferCopy* destination,
                                   const mrhiExtent3d* size)
{
    return CopyBufferTexture(device, pass, destination, source, size, true);
}

// Whether a texture side names every aspect of its format: all of them,
// or the only one.
static bool IsEveryAspect(const mrhiTextureSide* side)
{
    mrhiFormat format = side->def->format;
    bool single = !(mrhiFormatHasDepth(format) && mrhiFormatHasStencil(format));
    return side->aspect == mrhi_aspectAll || single;
}

mrhiResult mrhiCopyTexture(mrhiDevice* device, mrhiPassId id, const mrhiTextureCopy* source,
                           const mrhiTextureCopy* destination, const mrhiExtent3d* size)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (source == nullptr || destination == nullptr || size == nullptr)
    {
        return mrhiDeviceMisuse(device);
    }
    mrhiResult status = mrhi_success;
    mrhiFramePass* pass = mrhiCopyPass(device, id, &status);
    if (pass == nullptr)
    {
        return status;
    }
    mrhiTextureSide from;
    mrhiTextureSide to;
    status = mrhiCheckTextureSide(device, source, size, &from);
    status = status == mrhi_success ? mrhiCheckTextureSide(device, destination, size, &to) : status;
    if (status != mrhi_success)
    {
        return mrhiRefuse(device, status);
    }
    // Overlapping parts of one texture are refused by the pass's
    // declarations, which never name a part as both a copy source and
    // destination.
    mrhiFormat a = from.def->format;
    mrhiFormat b = to.def->format;
    bool compatible = a == b || mrhiFormatSrgbPair(a) == b;
    if (!compatible || from.def->sampleCount != to.def->sampleCount || !IsEveryAspect(&from) ||
        !IsEveryAspect(&to) ||
        !mrhiPassDeclares(device, pass, from.object, MRHI_KIND(mrhi_accessCopySource),
                          &from.part) ||
        !mrhiPassDeclares(device, pass, to.object, MRHI_KIND(mrhi_accessCopyDestination), &to.part))
    {
        return mrhiDeviceMisuse(device);
    }
    mrhiCommand* records = mrhiTakeCopy(device, pass, mrhiCommandCopyTexture, size->width,
                                        size->height, size->depthOrLayers);
    if (records == nullptr)
    {
        return mrhi_errorCapacity;
    }
    mrhiCommandTextureSide sides[2] = {
        {.object = from.object,
         .mip = source->mip,
         .x = source->x,
         .y = source->y,
         .z = source->z,
         .aspect = source->aspect},
        {.object = to.object,
         .mip = destination->mip,
         .x = destination->x,
         .y = destination->y,
         .z = destination->z,
         .aspect = destination->aspect},
    };
    memcpy(&records[1], sides, sizeof(sides));
    return mrhi_success;
}
