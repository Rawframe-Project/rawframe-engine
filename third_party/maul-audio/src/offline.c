// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The offline backend's scripted devices. Each call checks what the
// host passes and then changes the device table exactly as a platform
// report would.

#include "maul-audio/offline.h"

#include "context.h"
#include "device.h"

#define OFFLINE_DEVICE_DEF_COOKIE 0x6D616F64u
#define OFFLINE_MIN_RATE          8000u
#define OFFLINE_MAX_RATE          384000u

maudOfflineDeviceDef maudDefaultOfflineDeviceDef(void)
{
    return (maudOfflineDeviceDef){
        .cookie = OFFLINE_DEVICE_DEF_COOKIE,
        .direction = maud_directionOutput,
        .layout = maud_layoutStereo,
        .sampleRate = 48000,
        .form = maud_formUnknown,
        .name = nullptr,
        .nameLength = 0,
        .key = nullptr,
        .keyLength = 0,
    };
}

// Checks that a scripted change may be made on context now.
static maudResult CheckOffline(maudContext* context)
{
    if (context == nullptr)
    {
        return maud_errorInvalid;
    }
    if (context->def.backend != maud_backendOffline)
    {
        return maud_errorUnsupported;
    }
    if (maudIsRenderingThread(context))
    {
        maudCountMisuse(context);
        return maud_errorState;
    }
    return maud_success;
}

static bool DefValid(const maudOfflineDeviceDef* def)
{
    return def->cookie == OFFLINE_DEVICE_DEF_COOKIE && def->direction <= maud_directionInput &&
           def->form <= maud_formDigital && maudGetLayoutChannelCount(def->layout) != 0 &&
           def->sampleRate >= OFFLINE_MIN_RATE && def->sampleRate <= OFFLINE_MAX_RATE &&
           (def->name != nullptr || def->nameLength == 0) &&
           (def->key != nullptr || def->keyLength == 0);
}

maudResult maudAddOfflineDevice(maudContext* context, const maudOfflineDeviceDef* def,
                                maudDeviceId* deviceIdOut)
{
    if (deviceIdOut != nullptr)
    {
        *deviceIdOut = (maudDeviceId){0, 0};
    }
    maudResult result = CheckOffline(context);
    if (result != maud_success)
    {
        return result;
    }
    if (def == nullptr || deviceIdOut == nullptr || !DefValid(def))
    {
        maudCountMisuse(context);
        return maud_errorInvalid;
    }
    maudDeviceSpec spec = {
        .info = {.direction = def->direction,
                 .nativeLayout = def->layout,
                 .nativeSampleRate = def->sampleRate,
                 .minSampleRate = OFFLINE_MIN_RATE,
                 .maxSampleRate = OFFLINE_MAX_RATE,
                 .form = def->form},
        .name = def->name,
        .nameLength = def->nameLength,
        .key = def->key,
        .keyLength = def->keyLength,
    };
    return maudAddDevice(context, &spec, deviceIdOut);
}

maudResult maudRemoveOfflineDevice(maudContext* context, maudDeviceId device)
{
    maudResult result = CheckOffline(context);
    if (result != maud_success)
    {
        return result;
    }
    maudDeviceSlot* slot = maudFindDevice(context, device);
    if (slot == nullptr)
    {
        return maud_errorStale;
    }
    maudRemoveDevice(context, slot);
    return maud_success;
}

maudResult maudSetOfflineDefaultDevice(maudContext* context, maudDeviceRole role,
                                       maudDeviceId device)
{
    maudResult result = CheckOffline(context);
    if (result != maud_success)
    {
        return result;
    }
    if (role > maud_roleCommunications)
    {
        maudCountMisuse(context);
        return maud_errorInvalid;
    }
    if (maudFindDevice(context, device) == nullptr)
    {
        return maud_errorStale;
    }
    maudSetDefaultDevice(context, role, device);
    return maud_success;
}

maudResult maudSetOfflineDeviceForm(maudContext* context, maudDeviceId device, maudDeviceForm form)
{
    maudResult result = CheckOffline(context);
    if (result != maud_success)
    {
        return result;
    }
    if (form > maud_formDigital)
    {
        maudCountMisuse(context);
        return maud_errorInvalid;
    }
    maudDeviceSlot* slot = maudFindDevice(context, device);
    if (slot == nullptr)
    {
        return maud_errorStale;
    }
    maudSetDeviceForm(context, slot, form);
    return maud_success;
}
