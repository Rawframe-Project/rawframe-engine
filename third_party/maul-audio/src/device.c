// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The device table: what a backend reports, the defaults per direction
// and role, and the queries hosts make. Names and keys live in the
// context's text storage, two fixed-size places per slot.

#include "device.h"

#include "context.h"
#include "follow.h"
#include "notify.h"

#include <string.h>

static maudDeviceId IdOf(const maudContext* context, const maudDeviceSlot* slot)
{
    return (maudDeviceId){(uint32_t)(slot - context->devices.slots) + 1, slot->generation};
}

static bool SameDevice(maudDeviceId a, maudDeviceId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

static void SetDefault(maudContext* context, maudDirection direction, maudDeviceRole role,
                       maudDeviceId device)
{
    context->devices.defaults[direction][role] = device;
    maudPostNotification(context, &(maudNotification){
                                      .kind = maud_notifyDefaultChanged,
                                      .direction = direction,
                                      .role = role,
                                      .deviceId = device,
                                  });
    maudFollowDefault(context, direction, role);
}

maudResult maudAddDevice(maudContext* context, const maudDeviceSpec* spec,
                         maudDeviceId* deviceIdOut)
{
    uint32_t textBytes = context->def.limits.deviceTextBytes;
    maudDeviceSlot* slot = nullptr;
    for (uint32_t i = 0; i < context->devices.capacity && slot == nullptr; ++i)
    {
        slot = context->devices.slots[i].live ? nullptr : &context->devices.slots[i];
    }
    if (slot == nullptr || spec->nameLength > textBytes || spec->keyLength > textBytes)
    {
        return maud_errorCapacity;
    }
    slot->info = spec->info;
    slot->info.defaultGeneral = false;
    slot->info.defaultCommunications = false;
    if (spec->nameLength != 0)
    {
        memcpy(slot->name.bytes, spec->name, spec->nameLength);
    }
    slot->name.length = (uint32_t)spec->nameLength;
    if (spec->keyLength != 0)
    {
        memcpy(slot->key.bytes, spec->key, spec->keyLength);
    }
    slot->key.length = (uint32_t)spec->keyLength;
    slot->live = true;
    maudDeviceId id = IdOf(context, slot);
    maudDirection direction = spec->info.direction;
    maudPostNotification(context, &(maudNotification){
                                      .kind = maud_notifyDeviceAdded,
                                      .direction = direction,
                                      .deviceId = id,
                                  });
    for (uint32_t role = maud_roleGeneral; role <= maud_roleCommunications; ++role)
    {
        if (context->devices.defaults[direction][role].index1 == 0)
        {
            SetDefault(context, direction, (maudDeviceRole)role, id);
        }
    }
    *deviceIdOut = id;
    return maud_success;
}

static maudDeviceId FirstDevice(const maudContext* context, maudDirection direction)
{
    for (uint32_t i = 0; i < context->devices.capacity; ++i)
    {
        const maudDeviceSlot* slot = &context->devices.slots[i];
        if (slot->live && slot->info.direction == direction)
        {
            return IdOf(context, slot);
        }
    }
    return (maudDeviceId){0, 0};
}

void maudRemoveDevice(maudContext* context, maudDeviceSlot* slot)
{
    maudDeviceId id = IdOf(context, slot);
    maudDirection direction = slot->info.direction;
    slot->live = false;
    // A generation of 0 never names a device, so it is skipped on wrap.
    slot->generation = slot->generation == UINT32_MAX ? 1 : slot->generation + 1;
    maudPostNotification(context, &(maudNotification){
                                      .kind = maud_notifyDeviceRemoved,
                                      .direction = direction,
                                      .deviceId = id,
                                  });
    maudLoseDevice(context, id);
    for (uint32_t role = maud_roleGeneral; role <= maud_roleCommunications; ++role)
    {
        if (SameDevice(context->devices.defaults[direction][role], id))
        {
            SetDefault(context, direction, (maudDeviceRole)role, FirstDevice(context, direction));
        }
    }
}

void maudSetDefaultDevice(maudContext* context, maudDeviceRole role, maudDeviceId device)
{
    const maudDeviceSlot* slot = maudFindDevice(context, device);
    maudDirection direction = slot->info.direction;
    if (!SameDevice(context->devices.defaults[direction][role], device))
    {
        SetDefault(context, direction, role, device);
    }
}

maudResult maudGetDevices(const maudContext* context, maudDirection direction, maudDeviceId* idsOut,
                          uint32_t capacity, uint32_t* countOut)
{
    if (context == nullptr || countOut == nullptr || (idsOut == nullptr && capacity != 0) ||
        direction > maud_directionInput)
    {
        return maud_errorInvalid;
    }
    uint32_t count = 0;
    for (uint32_t i = 0; i < context->devices.capacity; ++i)
    {
        const maudDeviceSlot* slot = &context->devices.slots[i];
        if (slot->live && slot->info.direction == direction)
        {
            if (count < capacity)
            {
                idsOut[count] = IdOf(context, slot);
            }
            count++;
        }
    }
    *countOut = count;
    return maud_success;
}

maudResult maudGetDeviceInfo(const maudContext* context, maudDeviceId device,
                             maudDeviceInfo* infoOut)
{
    if (context == nullptr || infoOut == nullptr)
    {
        return maud_errorInvalid;
    }
    const maudDeviceSlot* slot = maudFindDevice(context, device);
    if (slot == nullptr)
    {
        return maud_errorStale;
    }
    const maudDeviceId* defaults = context->devices.defaults[slot->info.direction];
    *infoOut = slot->info;
    infoOut->defaultGeneral = SameDevice(defaults[maud_roleGeneral], device);
    infoOut->defaultCommunications = SameDevice(defaults[maud_roleCommunications], device);
    return maud_success;
}

static maudResult CopyText(const maudContext* context, maudDeviceId device, bool key,
                           char* bytesOut, size_t capacity, size_t* lengthOut)
{
    if (context == nullptr || lengthOut == nullptr || (bytesOut == nullptr && capacity != 0))
    {
        return maud_errorInvalid;
    }
    const maudDeviceSlot* slot = maudFindDevice(context, device);
    if (slot == nullptr)
    {
        return maud_errorStale;
    }
    const maudDeviceText* text = key ? &slot->key : &slot->name;
    *lengthOut = text->length;
    if (text->length > capacity)
    {
        return maud_errorCapacity;
    }
    if (text->length != 0)
    {
        memcpy(bytesOut, text->bytes, text->length);
    }
    return maud_success;
}

maudResult maudGetDeviceName(const maudContext* context, maudDeviceId device, char* bytesOut,
                             size_t capacity, size_t* lengthOut)
{
    return CopyText(context, device, false, bytesOut, capacity, lengthOut);
}

maudResult maudGetDeviceKey(const maudContext* context, maudDeviceId device, char* bytesOut,
                            size_t capacity, size_t* lengthOut)
{
    return CopyText(context, device, true, bytesOut, capacity, lengthOut);
}

maudResult maudGetDefaultDevice(const maudContext* context, maudDirection direction,
                                maudDeviceRole role, maudDeviceId* deviceIdOut)
{
    if (context == nullptr || deviceIdOut == nullptr || direction > maud_directionInput ||
        role > maud_roleCommunications)
    {
        return maud_errorInvalid;
    }
    *deviceIdOut = context->devices.defaults[direction][role];
    return deviceIdOut->index1 != 0 ? maud_success : maud_empty;
}

static bool KeyIs(const maudDeviceSlot* slot, const char* key, size_t length)
{
    return slot->key.length == length && memcmp(slot->key.bytes, key, length) == 0;
}

// The spec a live device is, or NULL.
static const maudDeviceSpec* SpecOf(const maudDeviceSlot* slot, const maudDeviceSpec* specs,
                                    uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        if (specs[i].info.direction == slot->info.direction &&
            KeyIs(slot, specs[i].key, specs[i].keyLength))
        {
            return &specs[i];
        }
    }
    return nullptr;
}

// Takes a scanned device's format; true when its native rate changed.
void maudSetDeviceForm(maudContext* context, maudDeviceSlot* slot, maudDeviceForm form)
{
    if (slot->info.form == form)
    {
        return;
    }
    slot->info.form = form;
    maudPostNotification(context, &(maudNotification){
                                      .kind = maud_notifyRouteChanged,
                                      .direction = slot->info.direction,
                                      .deviceId = IdOf(context, slot),
                                      .form = form,
                                  });
}

static bool Update(maudContext* context, maudDeviceSlot* slot, const maudDeviceInfo* info)
{
    maudSetDeviceForm(context, slot, info->form);
    bool rateChanged = slot->info.nativeSampleRate != info->nativeSampleRate;
    slot->info.nativeLayout = info->nativeLayout;
    slot->info.nativeSampleRate = info->nativeSampleRate;
    slot->info.minSampleRate = info->minSampleRate;
    slot->info.maxSampleRate = info->maxSampleRate;
    return rateChanged;
}

maudResult maudSyncDevices(maudContext* context, const maudDeviceSpec* specs, uint32_t count,
                           const char* kept)
{
    size_t keptLength = kept != nullptr ? strlen(kept) : 0;
    bool rateChanged = false;
    for (uint32_t i = 0; i < context->devices.capacity; ++i)
    {
        maudDeviceSlot* slot = &context->devices.slots[i];
        const maudDeviceSpec* spec = slot->live ? SpecOf(slot, specs, count) : nullptr;
        if (spec != nullptr)
        {
            rateChanged = Update(context, slot, &spec->info) || rateChanged;
        }
        else if (slot->live && (kept == nullptr || !KeyIs(slot, kept, keptLength)))
        {
            maudRemoveDevice(context, slot);
        }
    }
    if (rateChanged)
    {
        maudRefreshNativeRates(context);
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        bool present = false;
        for (uint32_t d = 0; d < context->devices.capacity && !present; ++d)
        {
            const maudDeviceSlot* slot = &context->devices.slots[d];
            present = slot->live && slot->info.direction == specs[i].info.direction &&
                      KeyIs(slot, specs[i].key, specs[i].keyLength);
        }
        maudDeviceId id;
        maudResult result = present ? maud_success : maudAddDevice(context, &specs[i], &id);
        if (result != maud_success)
        {
            return result;
        }
    }
    return maud_success;
}

maudDeviceId maudFindDeviceByKey(const maudContext* context, maudDirection direction,
                                 const char* key, size_t length)
{
    for (uint32_t i = 0; i < context->devices.capacity; ++i)
    {
        const maudDeviceSlot* slot = &context->devices.slots[i];
        if (slot->live && slot->info.direction == direction && KeyIs(slot, key, length))
        {
            return IdOf(context, slot);
        }
    }
    return (maudDeviceId){0, 0};
}

size_t maudCutUtf8(const char* text, size_t limit)
{
    size_t length = strlen(text);
    if (length <= limit)
    {
        return length;
    }
    while (limit > 0 && ((unsigned char)text[limit] & 0xC0u) == 0x80u)
    {
        limit--;
    }
    return limit;
}
