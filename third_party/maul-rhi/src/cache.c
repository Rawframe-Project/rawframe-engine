// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Pipeline caches: the driver's blob in an envelope that names the ABI
// version, driver and adapter it came from and a SHA-256 digest of it
// all. A cache is state, read only under the ABI version that wrote it.
// It only saves time, so an import never fails the device; one that does
// not fit is ignored and reported.

#include "bytes.h"
#include "device_core.h"
#include "sha256.h"

#include <string.h>

// The envelope's header: the magic, its version, the whole size, the
// digest of what follows it, then the ABI version, the driver kind, and
// the adapter's vendor and device ids.
#define HEADER_BYTES  64
#define DIGESTED_FROM 48
#define CACHE_VERSION 2u

static const uint8_t MAGIC[4] = {'M', 'R', 'P', 'C'};

// Whether an envelope names this driver and adapter.
static bool IsOurs(const mrhiDevice* device, const uint8_t* bytes)
{
    return mrhiRead32(bytes + 52) == device->adapterInfo.driver &&
           mrhiRead32(bytes + 56) == device->adapterInfo.vendorId &&
           mrhiRead32(bytes + 60) == device->adapterInfo.deviceId;
}

mrhiResult mrhiImportPipelineCache(mrhiDevice* device, const void* bytes, size_t size)
{
    if (bytes == nullptr)
    {
        return mrhi_empty;
    }
    const uint8_t* data = bytes;
    if (size < HEADER_BYTES || memcmp(data, MAGIC, sizeof(MAGIC)) != 0)
    {
        return mrhi_errorInvalid;
    }
    // Every envelope version keeps the magic and its version here; the
    // rest of another version's header may lie elsewhere, so the version
    // is read before anything it places.
    if (mrhiRead32(data + 4) != CACHE_VERSION)
    {
        return mrhi_errorVersion;
    }
    if (mrhiRead64(data + 8) != size)
    {
        return mrhi_errorInvalid;
    }
    uint8_t digest[MRHI_DIGEST_BYTES];
    mrhiSha256(data + DIGESTED_FROM, size - DIGESTED_FROM, digest);
    if (memcmp(digest, data + 16, MRHI_DIGEST_BYTES) != 0)
    {
        return mrhi_errorInvalid;
    }
    if (mrhiRead32(data + 48) != MRHI_ABI_VERSION)
    {
        return mrhi_errorVersion;
    }
    bool taken =
        IsOurs(device, data) && device->driver.vtable->importPipelineCache(
                                    device->driver.self, data + HEADER_BYTES, size - HEADER_BYTES);
    return taken ? mrhi_success : mrhi_errorStale;
}

mrhiResult mrhiGetPipelineCache(mrhiDevice* device, void* bytes, size_t capacity, size_t* sizeOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (sizeOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    mrhiResult status = mrhiDeviceUsable(device);
    if (status != mrhi_success)
    {
        return status;
    }
    size_t payload = device->driver.vtable->exportPipelineCache(device->driver.self, nullptr, 0);
    *sizeOut = HEADER_BYTES + payload;
    if (bytes == nullptr)
    {
        return mrhi_success;
    }
    if (capacity < *sizeOut)
    {
        return mrhi_errorCapacity;
    }
    uint8_t* data = bytes;
    // The device is used by one thread at a time, so a driver whose cache
    // changed size between the two calls has failed.
    if (device->driver.vtable->exportPipelineCache(device->driver.self, data + HEADER_BYTES,
                                                   payload) != payload)
    {
        return mrhi_errorPlatform;
    }
    memcpy(data, MAGIC, sizeof(MAGIC));
    mrhiWrite32(data + 4, CACHE_VERSION);
    mrhiWrite64(data + 8, *sizeOut);
    mrhiWrite32(data + 48, MRHI_ABI_VERSION);
    mrhiWrite32(data + 52, device->adapterInfo.driver);
    mrhiWrite32(data + 56, device->adapterInfo.vendorId);
    mrhiWrite32(data + 60, device->adapterInfo.deviceId);
    mrhiSha256(data + DIGESTED_FROM, *sizeOut - DIGESTED_FROM, data + 16);
    return mrhi_success;
}

mrhiResult mrhiGetPipelineCacheOutcome(mrhiDevice* device)
{
    return device == nullptr ? mrhi_errorInvalid : device->cacheOutcome;
}
