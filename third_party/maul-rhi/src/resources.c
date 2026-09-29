// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The objects a device owns, as generation-checked ids over its tables:
// each def checked, the driver asked to make the object, and a
// destruction that ends the id at once.

#include "capabilities_core.h"
#include "device_core.h"
#include "heap_core.h"

#define SAMPLER_DEF_COOKIE 0x6D727361u
#define BUFFER_DEF_COOKIE  0x6D726275u

mrhiSamplerDef mrhiDefaultSamplerDef(void)
{
    mrhiSamplerDef def = {0};
    def.cookie = SAMPLER_DEF_COOKIE;
    def.magFilter = mrhi_filterNearest;
    def.minFilter = mrhi_filterNearest;
    def.mipFilter = mrhi_filterNearest;
    def.addressU = mrhi_addressClampToEdge;
    def.addressV = mrhi_addressClampToEdge;
    def.addressW = mrhi_addressClampToEdge;
    def.lodMin = 0.0f;
    def.lodMax = 32.0f;
    def.maxAnisotropy = 1;
    def.compare = mrhi_compareNone;
    return def;
}

// Whether a sampler def's values are in range: known enums, levels of
// detail in order (NaN refused), and anisotropy only with linear
// filtering.
static bool IsSamplerDefValid(const mrhiSamplerDef* def)
{
    bool filters = def->magFilter <= mrhi_filterLinear && def->minFilter <= mrhi_filterLinear &&
                   def->mipFilter <= mrhi_filterLinear;
    bool address = def->addressU <= mrhi_addressMirrorRepeat &&
                   def->addressV <= mrhi_addressMirrorRepeat &&
                   def->addressW <= mrhi_addressMirrorRepeat;
    bool lod = def->lodMin >= 0.0f && def->lodMax >= def->lodMin;
    bool linear = def->magFilter == mrhi_filterLinear && def->minFilter == mrhi_filterLinear &&
                  def->mipFilter == mrhi_filterLinear;
    bool anisotropy =
        def->maxAnisotropy >= 1 && def->maxAnisotropy <= 16 && (def->maxAnisotropy == 1 || linear);
    return filters && address && lod && anisotropy && def->compare <= mrhi_compareAlways;
}

mrhiResult mrhiCreateSampler(mrhiDevice* device, const mrhiSamplerDef* def,
                             mrhiSamplerId* samplerOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (def == nullptr || samplerOut == nullptr)
    {
        return mrhiDeviceMisuse(device);
    }
    mrhiResult status = mrhiCheckObjectDef(device, MRHI_DEF_HEAD(def), SAMPLER_DEF_COOKIE);
    if (status != mrhi_success)
    {
        return status;
    }
    if (!IsSamplerDefValid(def))
    {
        return mrhiDeviceMisuse(device);
    }
    status = mrhiDeviceUsable(device);
    if (status != mrhi_success)
    {
        return status;
    }
    uint32_t index1 = 0;
    uint32_t generation = 0;
    if (!mrhiPoolAcquire(&device->samplers, &index1, &generation))
    {
        return mrhi_errorCapacity;
    }
    uint64_t handle = 0;
    status = mrhiDriverStatus(
        device, device->driver.vtable->createSampler(device->driver.self, def, &handle));
    if (status != mrhi_success)
    {
        mrhiPoolRelease(&device->samplers, index1);
        return status;
    }
    device->samplerSlots[index1 - 1] = (mrhiSamplerSlot){
        .handle = handle,
        .comparison = def->compare != mrhi_compareNone,
        .filtering = def->magFilter == mrhi_filterLinear || def->minFilter == mrhi_filterLinear ||
                     def->mipFilter == mrhi_filterLinear,
    };
    *samplerOut = (mrhiSamplerId){index1, generation};
    return mrhi_success;
}

mrhiResult mrhiDestroySampler(mrhiDevice* device, mrhiSamplerId sampler)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (!mrhiPoolIsLive(&device->samplers, sampler.index1, sampler.generation))
    {
        return mrhi_errorStale;
    }
    if (device->samplerSlots[sampler.index1 - 1].heapRefs > 0)
    {
        mrhiForgetHeapObject(device, mrhiHeapObjectSampler, sampler.index1);
    }
    device->driver.vtable->destroySampler(device->driver.self,
                                          device->samplerSlots[sampler.index1 - 1].handle);
    mrhiPoolRelease(&device->samplers, sampler.index1);
    return mrhi_success;
}

mrhiBufferDef mrhiDefaultBufferDef(void)
{
    mrhiBufferDef def = {0};
    def.cookie = BUFFER_DEF_COOKIE;
    return def;
}

mrhiResult mrhiCheckBufferShape(mrhiDevice* device, const mrhiBufferDef* def)
{
    mrhiResult status = mrhiCheckObjectDef(device, MRHI_DEF_HEAD(def), BUFFER_DEF_COOKIE);
    if (status != mrhi_success)
    {
        return status;
    }
    if (def->size == 0 || def->size % 4 != 0)
    {
        return mrhiDeviceMisuse(device);
    }
    return def->size > device->limits.bufferBytes ? mrhi_errorUnsupported : mrhi_success;
}

mrhiResult mrhiCreateBuffer(mrhiDevice* device, const mrhiBufferDef* def, mrhiBufferId* bufferOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (def == nullptr || bufferOut == nullptr)
    {
        return mrhiDeviceMisuse(device);
    }
    mrhiResult status = mrhiCheckBufferShape(device, def);
    if (status != mrhi_success)
    {
        return status;
    }
    if (def->usage == 0 || (def->usage & ~mrhiBufferUsageKnown) != 0)
    {
        return mrhiDeviceMisuse(device);
    }
    status = mrhiDeviceUsable(device);
    if (status != mrhi_success)
    {
        return status;
    }
    uint32_t index1 = 0;
    uint32_t generation = 0;
    if (!mrhiPoolAcquire(&device->buffers, &index1, &generation))
    {
        return mrhi_errorCapacity;
    }
    uint64_t handle = 0;
    status = mrhiDriverStatus(
        device, device->driver.vtable->createBuffer(device->driver.self, def, &handle));
    if (status != mrhi_success)
    {
        mrhiPoolRelease(&device->buffers, index1);
        return status;
    }
    device->bufferSlots[index1 - 1] =
        (mrhiBufferSlot){.handle = handle, .size = def->size, .usage = def->usage};
    *bufferOut = (mrhiBufferId){index1, generation};
    return mrhi_success;
}

mrhiResult mrhiDestroyBuffer(mrhiDevice* device, mrhiBufferId buffer)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (!mrhiPoolIsLive(&device->buffers, buffer.index1, buffer.generation))
    {
        return mrhi_errorStale;
    }
    if (device->bufferSlots[buffer.index1 - 1].heapRefs > 0)
    {
        mrhiForgetHeapObject(device, mrhiHeapObjectBuffer, buffer.index1);
    }
    device->driver.vtable->destroyBuffer(device->driver.self,
                                         device->bufferSlots[buffer.index1 - 1].handle);
    mrhiPoolRelease(&device->buffers, buffer.index1);
    return mrhi_success;
}
