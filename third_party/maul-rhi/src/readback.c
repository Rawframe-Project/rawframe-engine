// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Readbacks (mrhi-0011): a copy into the device's readback ring,
// recorded with a request that the frame's finish answers, and taken out
// once by the program. Records and ring bytes are used in order, taken
// under a lock since passes record in parallel, and freed as the oldest
// are taken.

#include "copy_core.h"

#include <stdatomic.h>
#include <stdckdint.h>
#include <string.h>

// The alignments every API's copy into a buffer accepts: D3D12's
// placement of a texture's data, and its row pitch.
#define PLACEMENT 512
#define PITCH     256

// The record a count of records taken names.
static mrhiReadback* RecordAt(mrhiDevice* device, uint32_t count)
{
    return &device->readbacks[count % device->deviceLimits.readbacks];
}

void mrhiMarkReadbacks(mrhiDevice* device)
{
    device->frameReadbackFirst = device->readbackHead;
    device->frameRingFirst = device->ringHead;
}

// Records past the head are never read: taking a record overwrites it.
void mrhiDropReadbacks(mrhiDevice* device)
{
    device->readbackPending -= device->readbackHead - device->frameReadbackFirst;
    device->readbackHead = device->frameReadbackFirst;
    device->ringHead = device->frameRingFirst;
}

void mrhiAnswerReadbacks(mrhiDevice* device, uint32_t first, uint32_t count, mrhiResult outcome)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        mrhiReadback* readback = RecordAt(device, first + i);
        readback->state = mrhiReadbackAnswered;
        readback->outcome = outcome;
        mrhiQueueAnswer(device, mrhi_deviceReadbackReady, readback->request, outcome);
    }
    device->readbackPending -= count;
}

// Takes bytes of the ring after its head, wrapping to its start rather
// than splitting them: their position, or false when the ring lacks
// room. No bytes take no room.
static bool TakeRing(mrhiDevice* device, uint64_t bytes, uint64_t* positionOut)
{
    uint64_t ring = device->deviceLimits.readbackBytes;
    uint64_t at = device->ringHead;
    if (bytes == 0)
    {
        *positionOut = at;
        return true;
    }
    if (bytes > ring)
    {
        return false;
    }
    uint64_t physical = at % ring;
    at += physical + bytes > ring ? ring - physical : 0;
    if (at + bytes - device->ringTail > ring)
    {
        return false;
    }
    device->ringHead = at + bytes;
    *positionOut = at;
    return true;
}

// Records a readback of ring bytes that the program takes as size bytes
// of rows: its record, or NULL when the device lacks a record, the
// bytes, or answer room for it and its frame.
static mrhiReadback* Reserve(mrhiDevice* device, uint64_t ringBytes, uint64_t size, uint32_t pitch,
                             uint32_t rowBytes, uint32_t rows)
{
    while (atomic_flag_test_and_set_explicit(&device->readbackLock, memory_order_acquire))
    {
    }
    uint64_t rounded = (ringBytes + PLACEMENT - 1) / PLACEMENT * PLACEMENT;
    uint64_t position = 0;
    mrhiReadback* readback = nullptr;
    if (device->readbackHead - device->readbackTail < device->deviceLimits.readbacks &&
        mrhiAnswerRoom(device) >= 2 && ringBytes <= UINT64_MAX - PLACEMENT &&
        TakeRing(device, rounded, &position))
    {
        readback = RecordAt(device, device->readbackHead++);
        *readback = (mrhiReadback){
            .request = ++device->lastRequest,
            .state = mrhiReadbackRecorded,
            .pitch = pitch,
            .rowBytes = rowBytes,
            .rows = rows,
            .position = position,
            .bytes = rounded,
            .size = size,
        };
        ++device->readbackPending;
    }
    atomic_flag_clear_explicit(&device->readbackLock, memory_order_release);
    return readback;
}

// The offset in the ring of a readback's bytes.
static uint64_t RingOffset(const mrhiDevice* device, const mrhiReadback* readback)
{
    uint64_t ring = device->deviceLimits.readbackBytes;
    return ring == 0 ? 0 : readback->position % ring;
}

mrhiResult mrhiReadBuffer(mrhiDevice* device, mrhiPassId id, mrhiResourceId resource,
                          uint64_t offset, uint64_t size, mrhiRequestId* requestOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (requestOut == nullptr)
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
        !mrhiPassDeclares(device, pass, object, MRHI_KIND(mrhi_accessCopySource), nullptr))
    {
        return mrhiDeviceMisuse(device);
    }
    mrhiReadback* readback = Reserve(device, size, size, 0, 0, 0);
    mrhiCommand* records = readback == nullptr
                               ? nullptr
                               : mrhiTakeCopy(device, pass, mrhiCommandReadBuffer, size, 1, 1);
    if (records == nullptr)
    {
        return mrhi_errorCapacity;
    }
    mrhiCommandBufferSide sides[2] = {
        {.object = object, .offset = offset},
        {.offset = RingOffset(device, readback)},
    };
    memcpy(&records[1], sides, sizeof(sides));
    *requestOut = (mrhiRequestId){readback->request, 1};
    return mrhi_success;
}

mrhiResult mrhiReadTexture(mrhiDevice* device, mrhiPassId id, const mrhiTextureCopy* source,
                           const mrhiExtent3d* size, mrhiRequestId* requestOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (source == nullptr || size == nullptr || requestOut == nullptr)
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
    status = mrhiCheckTextureTransfer(device, source, size, true, &side, &facts);
    if (status != mrhi_success)
    {
        return mrhiRefuse(device, status);
    }
    if (!mrhiPassDeclares(device, pass, side.object, MRHI_KIND(mrhi_accessCopySource), &side.part))
    {
        return mrhiDeviceMisuse(device);
    }
    mrhiFormatBlock block = mrhiGetFormatBlock(side.def->format);
    uint64_t rows = size->height / block.height;
    uint64_t rowBytes = (uint64_t)(size->width / block.width) * facts.bytes;
    uint64_t pitch = (rowBytes + PITCH - 1) / PITCH * PITCH;
    // Bytes past 64 bits are past any ring.
    uint64_t ringBytes = 0;
    uint64_t tight = 0;
    if (ckd_mul(&ringBytes, pitch * rows, (uint64_t)size->depthOrLayers) ||
        ckd_mul(&tight, rowBytes * rows, (uint64_t)size->depthOrLayers))
    {
        ringBytes = UINT64_MAX;
    }
    mrhiReadback* readback =
        Reserve(device, ringBytes, tight, (uint32_t)pitch, (uint32_t)rowBytes, (uint32_t)rows);
    mrhiCommand* records = readback == nullptr
                               ? nullptr
                               : mrhiTakeCopy(device, pass, mrhiCommandReadTexture, size->width,
                                              size->height, size->depthOrLayers);
    if (records == nullptr)
    {
        return mrhi_errorCapacity;
    }
    mrhiCommandTextureSide from = {
        .object = side.object,
        .mip = source->mip,
        .x = source->x,
        .y = source->y,
        .z = source->z,
        .aspect = source->aspect,
    };
    mrhiCommandBufferSide to = {
        .bytesPerRow = (uint32_t)pitch,
        .offset = RingOffset(device, readback),
        .rowsPerImage = (uint32_t)rows,
    };
    memcpy(&records[1], &from, sizeof(from));
    memcpy(&records[2], &to, sizeof(to));
    *requestOut = (mrhiRequestId){readback->request, 1};
    return mrhi_success;
}

// The readback a request names that is waiting to be taken: or NULL.
static mrhiReadback* Find(mrhiDevice* device, mrhiRequestId request)
{
    for (uint32_t i = device->readbackTail; i != device->readbackHead; ++i)
    {
        mrhiReadback* readback = RecordAt(device, i);
        if (readback->request == request.index1 && request.generation == 1 &&
            (readback->state == mrhiReadbackRecorded || readback->state == mrhiReadbackAnswered))
        {
            return readback;
        }
    }
    return nullptr;
}

// Frees a taken readback, and the ring's room from the oldest readback to
// the first not yet taken.
static void Free(mrhiDevice* device, mrhiReadback* readback)
{
    readback->state = mrhiReadbackTaken;
    while (device->readbackTail != device->readbackHead &&
           RecordAt(device, device->readbackTail)->state == mrhiReadbackTaken)
    {
        const mrhiReadback* oldest = RecordAt(device, device->readbackTail++);
        device->ringTail = oldest->position + oldest->bytes;
    }
}

mrhiResult mrhiTakeReadback(mrhiDevice* device, mrhiRequestId request, void* bytes, size_t capacity,
                            size_t* sizeOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (sizeOut == nullptr || (bytes == nullptr && capacity > 0))
    {
        return mrhiDeviceMisuse(device);
    }
    mrhiReadback* readback = Find(device, request);
    if (readback == nullptr)
    {
        return mrhi_errorStale;
    }
    if (readback->state != mrhiReadbackAnswered)
    {
        return mrhi_errorState;
    }
    if (readback->outcome != mrhi_success)
    {
        mrhiResult outcome = readback->outcome;
        Free(device, readback);
        return outcome;
    }
    *sizeOut = readback->size;
    if (bytes == nullptr)
    {
        return mrhi_success;
    }
    if (capacity < readback->size)
    {
        return mrhi_errorCapacity;
    }
    const uint8_t* ring = device->readbackRing + RingOffset(device, readback);
    // A buffer's bytes, or rows of none, which have no pitch either.
    if (readback->pitch == 0)
    {
        memcpy(bytes, ring, readback->size);
    }
    else
    {
        uint64_t lines = readback->size / readback->rowBytes;
        for (uint64_t line = 0; line < lines; ++line)
        {
            memcpy((uint8_t*)bytes + line * readback->rowBytes, ring + line * readback->pitch,
                   readback->rowBytes);
        }
    }
    Free(device, readback);
    return mrhi_success;
}
