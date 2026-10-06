// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Bindless heaps (mrhi-0015): device objects of resource and sampler
// entries at indices the program chooses. An entry is written only while
// empty; one emptied is written again only once the frames submitted
// before it was emptied finish, so no running frame ever reads an entry
// that changes under it. Destroying a view, buffer or sampler empties
// the entries naming it.

#include "allocator.h"
#include "heap_core.h"

#include "maul-rhi/encoder.h"

#define HEAP_DEF_COOKIE 0x6D726870u

mrhiHeapDef mrhiDefaultHeapDef(void)
{
    mrhiHeapDef def = {0};
    def.cookie = HEAP_DEF_COOKIE;
    def.entries = 1024;
    def.samplers = 16;
    return def;
}

// A heap's entries: its resource entries, then its sampler entries.
static size_t TableBytes(uint32_t entries, uint32_t samplers)
{
    return ((size_t)entries + samplers) * sizeof(mrhiHeapEntrySlot);
}

mrhiResult mrhiCreateHeap(mrhiDevice* device, const mrhiHeapDef* def, mrhiHeapId* heapOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (def == nullptr || heapOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    mrhiResult status = mrhiCheckObjectDef(device, MRHI_DEF_HEAD(def), HEAP_DEF_COOKIE);
    if (status != mrhi_success)
    {
        return status;
    }
    if (def->entries == 0)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticHeapDef);
    }
    if (!device->features.bindlessSampling || def->entries > device->limits.heapSize ||
        def->samplers > device->limits.samplerHeapSize)
    {
        return mrhi_errorUnsupported;
    }
    status = mrhiDeviceUsable(device);
    if (status != mrhi_success)
    {
        return status;
    }
    size_t bytes = TableBytes(def->entries, def->samplers);
    mrhiHeapEntrySlot* table = mrhiAllocate(&device->allocator, bytes, alignof(mrhiHeapEntrySlot));
    uint32_t index1 = 0;
    uint32_t generation = 0;
    if (table == nullptr || !mrhiPoolAcquire(&device->heaps, &index1, &generation))
    {
        if (table != nullptr)
        {
            mrhiRelease(&device->allocator, table, bytes, alignof(mrhiHeapEntrySlot));
        }
        return mrhi_errorCapacity;
    }
    uint64_t handle = 0;
    status = mrhiDriverStatus(device,
                              device->driver.vtable->createHeap(device->driver.self, def, &handle));
    if (status != mrhi_success)
    {
        mrhiRelease(&device->allocator, table, bytes, alignof(mrhiHeapEntrySlot));
        mrhiPoolRelease(&device->heaps, index1);
        return status;
    }
    for (size_t i = 0; i < (size_t)def->entries + def->samplers; ++i)
    {
        table[i] = (mrhiHeapEntrySlot){0};
    }
    device->heapSlots[index1 - 1] = (mrhiHeapSlot){
        .handle = handle,
        .entries = def->entries,
        .samplers = def->samplers,
        .table = table,
    };
    *heapOut = (mrhiHeapId){index1, generation};
    return mrhi_success;
}

// The heap refs of the object an entry names: a view's, a buffer's or
// (in the sampler part) a sampler's.
static uint32_t* RefsOf(mrhiDevice* device, const mrhiHeapEntrySlot* entry, bool sampler)
{
    if (sampler)
    {
        return &device->samplerSlots[entry->object - 1].heapRefs;
    }
    return entry->kind == mrhi_heapStorageBuffer ? &device->bufferSlots[entry->object - 1].heapRefs
                                                 : &device->viewSlots[entry->object - 1].heapRefs;
}

// Empties an entry, which frames submitted until now may still read.
static void Empty(mrhiDevice* device, mrhiHeapEntrySlot* entry, bool sampler)
{
    if (entry->object != 0)
    {
        --*RefsOf(device, entry, sampler);
    }
    *entry = (mrhiHeapEntrySlot){.freeAfter = device->lastSubmitted};
}

// Frees a heap's entries and its slot.
static void EndHeap(mrhiDevice* device, uint32_t index1)
{
    mrhiHeapSlot* slot = &device->heapSlots[index1 - 1];
    for (uint32_t i = 0; i < slot->entries + slot->samplers; ++i)
    {
        Empty(device, &slot->table[i], i >= slot->entries);
    }
    device->driver.vtable->destroyHeap(device->driver.self, slot->handle);
    mrhiRelease(&device->allocator, slot->table, TableBytes(slot->entries, slot->samplers),
                alignof(mrhiHeapEntrySlot));
    *slot = (mrhiHeapSlot){0};
    mrhiPoolRelease(&device->heaps, index1);
}

mrhiResult mrhiDestroyHeap(mrhiDevice* device, mrhiHeapId heap)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (!mrhiPoolIsLive(&device->heaps, heap.index1, heap.generation))
    {
        return mrhi_errorStale;
    }
    EndHeap(device, heap.index1);
    return mrhi_success;
}

void mrhiDestroyHeaps(mrhiDevice* device)
{
    for (uint32_t i = 0; i < device->deviceLimits.heaps; ++i)
    {
        if (device->heapSlots[i].table != nullptr)
        {
            EndHeap(device, i + 1);
        }
    }
}

// Whether no running frame was submitted before an emptied entry's
// emptying, so that none can read it.
static bool IsFree(const mrhiDevice* device, const mrhiHeapEntrySlot* entry)
{
    if (entry->object != 0)
    {
        return false;
    }
    for (uint32_t i = 0; i < device->runningCount; ++i)
    {
        if (device->running[i] <= entry->freeAfter)
        {
            return false;
        }
    }
    return true;
}

// The heap an id names and the entry at an index of its resource part
// (or its sampler part): NULL with the refusal.
static mrhiHeapEntrySlot* FindEntry(mrhiDevice* device, mrhiHeapId heap, uint32_t index,
                                    bool sampler, mrhiResult* statusOut)
{
    if (!mrhiPoolIsLive(&device->heaps, heap.index1, heap.generation))
    {
        *statusOut = mrhi_errorStale;
        return nullptr;
    }
    mrhiHeapSlot* slot = &device->heapSlots[heap.index1 - 1];
    if (index >= (sampler ? slot->samplers : slot->entries))
    {
        *statusOut = mrhiDeviceMisuse(device, mrhi_diagnosticHeapIndex);
        return nullptr;
    }
    return &slot->table[(sampler ? slot->entries : 0) + index];
}

// Checks a view entry: success with the view's driver handle, or the
// refusal.
static mrhiResult CheckView(mrhiDevice* device, const mrhiHeapEntry* entry, uint64_t* handleOut)
{
    if (!mrhiPoolIsLive(&device->views, entry->view.index1, entry->view.generation))
    {
        return mrhi_errorStale;
    }
    const mrhiViewSlot* view = &device->viewSlots[entry->view.index1 - 1];
    bool storage = entry->kind == mrhi_heapStorageTexture;
    mrhiTextureUsage needed = storage ? mrhi_textureStorage : mrhi_textureSampled;
    if ((view->def.usage & needed) == 0 || (storage && view->def.mipCount != 1) ||
        (!storage && entry->writable))
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticHeapView);
    }
    *handleOut = view->handle;
    return mrhi_success;
}

// Checks a buffer entry and resolves its size: success with the
// buffer's driver handle, or the refusal.
static mrhiResult CheckBuffer(mrhiDevice* device, const mrhiHeapEntry* entry, uint64_t* sizeOut,
                              uint64_t* handleOut)
{
    if (!mrhiPoolIsLive(&device->buffers, entry->buffer.index1, entry->buffer.generation))
    {
        return mrhi_errorStale;
    }
    const mrhiBufferSlot* buffer = &device->bufferSlots[entry->buffer.index1 - 1];
    uint64_t offset = entry->offset;
    uint64_t size = entry->size == MRHI_WHOLE_SIZE && offset <= buffer->size ? buffer->size - offset
                                                                             : entry->size;
    if ((buffer->usage & mrhi_bufferStorage) == 0 ||
        offset % device->limits.storageOffsetAlignment != 0 || size == 0 || size % 4 != 0 ||
        offset > buffer->size || size > buffer->size - offset)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticHeapBuffer);
    }
    *sizeOut = size;
    *handleOut = buffer->handle;
    return mrhi_success;
}

mrhiResult mrhiSetHeapEntry(mrhiDevice* device, mrhiHeapId heap, uint32_t index,
                            const mrhiHeapEntry* entry)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (entry == nullptr || entry->kind > mrhi_heapStorageBuffer)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticHeapEntry);
    }
    mrhiResult status = mrhi_success;
    mrhiHeapEntrySlot* slot = FindEntry(device, heap, index, false, &status);
    if (slot == nullptr)
    {
        return status;
    }
    if (entry->kind != mrhi_heapSampledTexture && !device->features.bindlessHeterogeneous)
    {
        return mrhi_errorUnsupported;
    }
    mrhiDriverHeapEntry written = {.kind = entry->kind, .writable = entry->writable};
    bool buffer = entry->kind == mrhi_heapStorageBuffer;
    status = buffer ? CheckBuffer(device, entry, &written.size, &written.handle)
                    : CheckView(device, entry, &written.handle);
    if (status != mrhi_success)
    {
        return status;
    }
    if (!IsFree(device, slot))
    {
        return mrhi_errorState;
    }
    written.offset = buffer ? entry->offset : 0;
    *slot = (mrhiHeapEntrySlot){
        .object = buffer ? entry->buffer.index1 : entry->view.index1,
        .generation = buffer ? entry->buffer.generation : entry->view.generation,
        .offset = written.offset,
        .size = written.size,
        .kind = entry->kind,
        .writable = entry->writable,
    };
    ++*RefsOf(device, slot, false);
    device->driver.vtable->writeHeapEntry(
        device->driver.self, device->heapSlots[heap.index1 - 1].handle, index, &written);
    return mrhi_success;
}

mrhiResult mrhiSetHeapSampler(mrhiDevice* device, mrhiHeapId heap, uint32_t index,
                              mrhiSamplerId sampler)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = mrhi_success;
    mrhiHeapEntrySlot* slot = FindEntry(device, heap, index, true, &status);
    if (slot == nullptr)
    {
        return status;
    }
    if (!mrhiPoolIsLive(&device->samplers, sampler.index1, sampler.generation))
    {
        return mrhi_errorStale;
    }
    if (!IsFree(device, slot))
    {
        return mrhi_errorState;
    }
    *slot = (mrhiHeapEntrySlot){.object = sampler.index1, .generation = sampler.generation};
    ++*RefsOf(device, slot, true);
    device->driver.vtable->writeHeapSampler(device->driver.self,
                                            device->heapSlots[heap.index1 - 1].handle, index,
                                            device->samplerSlots[sampler.index1 - 1].handle);
    return mrhi_success;
}

static mrhiResult Clear(mrhiDevice* device, mrhiHeapId heap, uint32_t index, bool sampler)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = mrhi_success;
    mrhiHeapEntrySlot* slot = FindEntry(device, heap, index, sampler, &status);
    if (slot == nullptr)
    {
        return status;
    }
    // Clearing an empty entry leaves when it frees as it was.
    if (slot->object != 0)
    {
        Empty(device, slot, sampler);
    }
    return mrhi_success;
}

mrhiResult mrhiClearHeapEntry(mrhiDevice* device, mrhiHeapId heap, uint32_t index)
{
    return Clear(device, heap, index, false);
}

mrhiResult mrhiClearHeapSampler(mrhiDevice* device, mrhiHeapId heap, uint32_t index)
{
    return Clear(device, heap, index, true);
}

void mrhiForgetHeapObject(mrhiDevice* device, mrhiHeapObject kind, uint32_t index1)
{
    for (uint32_t h = 0; h < device->deviceLimits.heaps; ++h)
    {
        mrhiHeapSlot* slot = &device->heapSlots[h];
        bool sampler = kind == mrhiHeapObjectSampler;
        uint32_t first = sampler ? slot->entries : 0;
        uint32_t end = sampler ? slot->entries + slot->samplers : slot->entries;
        for (uint32_t i = first; i < end; ++i)
        {
            mrhiHeapEntrySlot* entry = &slot->table[i];
            bool buffer = entry->kind == mrhi_heapStorageBuffer;
            bool named =
                entry->object == index1 && (sampler || (kind == mrhiHeapObjectBuffer) == buffer);
            if (named)
            {
                Empty(device, entry, sampler);
            }
        }
    }
}

mrhiResult mrhiCheckPassHeap(mrhiDevice* device, mrhiHeapId heap, uint64_t* handleOut)
{
    *handleOut = 0;
    if (heap.index1 == 0 && heap.generation == 0)
    {
        return mrhi_success;
    }
    if (!mrhiPoolIsLive(&device->heaps, heap.index1, heap.generation))
    {
        return mrhi_errorStale;
    }
    *handleOut = device->heapSlots[heap.index1 - 1].handle;
    return mrhi_success;
}
