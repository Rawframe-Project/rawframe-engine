// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Adapter requests (mrhi-0003): the adapter table keeps the ids of
// adapters found again and retires the others, and the listing orders
// them by the request's preference.

#include "capabilities_core.h"
#include "chain.h"
#include "instance_core.h"

#define ADAPTER_REQUEST_DEF_COOKIE 0x6D726172u

mrhiAdapterRequestDef mrhiDefaultAdapterRequestDef(void)
{
    mrhiAdapterRequestDef def = {0};
    def.cookie = ADAPTER_REQUEST_DEF_COOKIE;
    def.preference = mrhi_powerDefault;
    def.allowSoftware = true;
    return def;
}

// Where an adapter kind ranks under a preference; lower is better.
static int Rank(mrhiAdapterKind kind, mrhiPowerPreference preference)
{
    static const int high[] = {3, 0, 1, 2, 4};
    static const int low[] = {3, 1, 0, 2, 4};
    if (kind > mrhi_adapterSoftware || preference == mrhi_powerDefault)
    {
        return 0;
    }
    return preference == mrhi_powerHigh ? high[kind] : low[kind];
}

static void SortListing(mrhiInstance* instance, mrhiPowerPreference preference)
{
    uint32_t* listing = instance->listing;
    for (uint32_t i = 1; i < instance->listed; ++i)
    {
        uint32_t slot = listing[i];
        int rank = Rank(instance->slots[slot].adapter.info.kind, preference);
        uint32_t j = i;
        while (j > 0 && Rank(instance->slots[listing[j - 1]].adapter.info.kind, preference) > rank)
        {
            listing[j] = listing[j - 1];
            --j;
        }
        listing[j] = slot;
    }
}

static uint32_t SlotFor(mrhiInstance* instance, uint64_t handle)
{
    uint32_t free = instance->limits.adapters;
    for (uint32_t i = 0; i < instance->limits.adapters; ++i)
    {
        if (instance->slots[i].inUse && instance->slots[i].adapter.handle == handle)
        {
            return i;
        }
        if (!instance->slots[i].inUse && free == instance->limits.adapters)
        {
            free = i;
        }
    }
    return free;
}

// Rebuilds the table and the listing from what the driver found. An
// adapter below the floor limits is left out, and its features are
// masked to what its API can grant. The driver's order is kept for
// equal ranks; more adapters than the limit is a capacity outcome, with
// the first ones kept.
mrhiFormatCaps mrhiAdapterFormatCaps(const mrhiInstance* instance, const mrhiDriverAdapter* adapter,
                                     mrhiFormat format)
{
    mrhiFormatCaps caps = {0};
    if (instance->driver.vtable != nullptr && mrhiFormatFamilyGranted(format, &adapter->features))
    {
        instance->driver.vtable->getFormatCaps(instance->driver.self, adapter->handle, format,
                                               &caps);
    }
    return caps;
}

// Whether an adapter meets the floor: the floor limits, and the floor
// capabilities of every format.
static bool MeetsFloor(const mrhiInstance* instance, const mrhiDriverAdapter* adapter)
{
    mrhiLimits floor = mrhiDefaultLimits();
    if (!mrhiLimitsWithin(&floor, &adapter->limits))
    {
        return false;
    }
    for (size_t i = 0; i < MRHI_KNOWN_FORMATS; ++i)
    {
        mrhiFormatCaps least = mrhiFloorFormatCaps(mrhiKnownFormats[i]);
        mrhiFormatCaps caps = mrhiAdapterFormatCaps(instance, adapter, mrhiKnownFormats[i]);
        if (!mrhiFormatCapsWithin(&least, &caps))
        {
            return false;
        }
    }
    return true;
}

// Whether an adapter presents to the search's surface, if it names one.
static bool Presents(const mrhiInstance* instance, const mrhiPending* search,
                     const mrhiDriverAdapter* adapter)
{
    if (search->compatibleSurface.index1 == 0)
    {
        return true;
    }
    mrhiSurfaceCaps caps = {0};
    instance->driver.vtable->getSurfaceCaps(instance->driver.self,
                                            mrhiFindSurface(instance, search->compatibleSurface),
                                            adapter->handle, &caps);
    return caps.presentable;
}

mrhiResult mrhiRefreshAdapters(mrhiInstance* instance, const mrhiPending* search)
{
    // A surface that ended before the answer leaves no adapter to list.
    bool ended = search->compatibleSurface.index1 != 0 &&
                 mrhiFindSurface(instance, search->compatibleSurface) == 0;
    size_t total = 0;
    if (instance->driver.vtable != nullptr)
    {
        total = instance->driver.vtable->getAdapters(instance->driver.self, instance->found,
                                                     instance->limits.adapters);
    }
    size_t count = total < instance->limits.adapters ? total : instance->limits.adapters;
    count = ended ? 0 : count;
    for (uint32_t i = 0; i < instance->limits.adapters; ++i)
    {
        instance->slots[i].seen = false;
    }
    instance->listed = 0;
    for (size_t i = 0; i < count; ++i)
    {
        mrhiDriverAdapter* adapter = &instance->found[i];
        mrhiMaskFeatures(&adapter->features, adapter->info.driver);
        if ((!search->allowSoftware && adapter->info.kind == mrhi_adapterSoftware) ||
            !MeetsFloor(instance, adapter) || !Presents(instance, search, adapter))
        {
            continue;
        }
        uint32_t slot = SlotFor(instance, adapter->handle);
        instance->slots[slot].inUse = true;
        instance->slots[slot].seen = true;
        instance->slots[slot].adapter = *adapter;
        instance->listing[instance->listed++] = slot;
    }
    for (uint32_t i = 0; i < instance->limits.adapters; ++i)
    {
        if (instance->slots[i].inUse && !instance->slots[i].seen)
        {
            instance->slots[i].inUse = false;
            ++instance->slots[i].generation;
        }
    }
    SortListing(instance, search->preference);
    if (ended)
    {
        return mrhi_errorStale;
    }
    return total > count ? mrhi_errorCapacity : mrhi_success;
}

mrhiResult mrhiRequestAdapters(mrhiInstance* instance, const mrhiAdapterRequestDef* def,
                               mrhiRequestId* requestOut)
{
    if (instance == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (def == nullptr || requestOut == nullptr || def->cookie != ADAPTER_REQUEST_DEF_COOKIE ||
        def->preference > mrhi_powerHigh)
    {
        return mrhiMisuse(instance);
    }
    mrhiResult chain = mrhiCheckChain(def->next, nullptr, 0, instance->limits.chainDepth);
    if (chain != mrhi_success)
    {
        return chain == mrhi_errorInvalid ? mrhiMisuse(instance) : chain;
    }
    if (def->compatibleSurface.index1 != 0 &&
        mrhiFindSurface(instance, def->compatibleSurface) == 0)
    {
        return mrhi_errorStale;
    }
    if (!mrhiHasRoomForAnswer(instance))
    {
        return mrhi_errorCapacity;
    }
    uint32_t request = mrhiNextRequest(instance);
    if (instance->driver.vtable != nullptr)
    {
        mrhiResult status =
            instance->driver.vtable->requestAdapters(instance->driver.self, request);
        if (status != mrhi_success)
        {
            return status;
        }
    }
    mrhiAddPending(instance, (mrhiPending){
                                 .request = request,
                                 .kind = mrhiPendingAdapters,
                                 .preference = def->preference,
                                 .allowSoftware = def->allowSoftware,
                                 .compatibleSurface = def->compatibleSurface,
                             });
    if (instance->driver.vtable == nullptr)
    {
        mrhiAnswerNow(instance, request, mrhi_success);
    }
    *requestOut = (mrhiRequestId){request, 1};
    return mrhi_success;
}

mrhiResult mrhiGetAdapters(mrhiInstance* instance, mrhiAdapterId* adapters, size_t capacity,
                           size_t* countOut)
{
    if (instance == nullptr || countOut == nullptr || (adapters == nullptr && capacity > 0))
    {
        return instance == nullptr ? mrhi_errorInvalid : mrhiMisuse(instance);
    }
    for (uint32_t i = 0; i < instance->listed && i < capacity; ++i)
    {
        uint32_t slot = instance->listing[i];
        adapters[i] = (mrhiAdapterId){slot + 1, instance->slots[slot].generation};
    }
    *countOut = instance->listed;
    return mrhi_success;
}

const mrhiDriverAdapter* mrhiFindAdapter(const mrhiInstance* instance, mrhiAdapterId adapter)
{
    uint32_t slot = adapter.index1 - 1;
    if (adapter.index1 == 0 || slot >= instance->limits.adapters || !instance->slots[slot].inUse ||
        instance->slots[slot].generation != adapter.generation)
    {
        return nullptr;
    }
    return &instance->slots[slot].adapter;
}

mrhiResult mrhiGetAdapterInfo(mrhiInstance* instance, mrhiAdapterId adapter,
                              mrhiAdapterInfo* infoOut)
{
    if (instance == nullptr || infoOut == nullptr)
    {
        return instance == nullptr ? mrhi_errorInvalid : mrhiMisuse(instance);
    }
    const mrhiDriverAdapter* found = mrhiFindAdapter(instance, adapter);
    if (found == nullptr)
    {
        return mrhi_errorStale;
    }
    *infoOut = found->info;
    return mrhi_success;
}

mrhiResult mrhiGetAdapterFeatures(mrhiInstance* instance, mrhiAdapterId adapter,
                                  mrhiFeatures* featuresOut)
{
    if (instance == nullptr || featuresOut == nullptr)
    {
        return instance == nullptr ? mrhi_errorInvalid : mrhiMisuse(instance);
    }
    const mrhiDriverAdapter* found = mrhiFindAdapter(instance, adapter);
    if (found == nullptr)
    {
        return mrhi_errorStale;
    }
    *featuresOut = found->features;
    return mrhi_success;
}

mrhiResult mrhiGetAdapterLimits(mrhiInstance* instance, mrhiAdapterId adapter,
                                mrhiLimits* limitsOut)
{
    if (instance == nullptr || limitsOut == nullptr)
    {
        return instance == nullptr ? mrhi_errorInvalid : mrhiMisuse(instance);
    }
    const mrhiDriverAdapter* found = mrhiFindAdapter(instance, adapter);
    if (found == nullptr)
    {
        return mrhi_errorStale;
    }
    *limitsOut = found->limits;
    return mrhi_success;
}

mrhiResult mrhiGetFormatCaps(mrhiInstance* instance, mrhiAdapterId adapter, mrhiFormat format,
                             mrhiFormatCaps* capsOut)
{
    if (instance == nullptr || capsOut == nullptr || !mrhiIsFormatKnown(format))
    {
        return instance == nullptr ? mrhi_errorInvalid : mrhiMisuse(instance);
    }
    const mrhiDriverAdapter* found = mrhiFindAdapter(instance, adapter);
    if (found == nullptr)
    {
        return mrhi_errorStale;
    }
    *capsOut = mrhiAdapterFormatCaps(instance, found, format);
    return mrhi_success;
}
