// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The instance as the core sees it: its limits, its driver, the
// notification queue, the work its driver has not answered yet, and the
// adapter table. Everything is laid out in one block when the instance
// is made, so nothing allocates later.

#ifndef MAUL_RHI_SRC_INSTANCE_CORE_H
#define MAUL_RHI_SRC_INSTANCE_CORE_H

#include "diagnostics.h"
#include "driver.h"
#include "pool.h"

#include "maul-rhi/device.h"

// An adapter id's slot: its generation, and, in use, what the driver
// reported. seen marks the slots a refresh found again.
typedef struct mrhiAdapterSlot
{
    uint32_t generation;
    bool inUse;
    bool seen;
    mrhiDriverAdapter adapter;
} mrhiAdapterSlot;

// A surface as its instance keeps it: its driver handle, and the device
// that configured it with that device's swapchain slot, NULL and 0 when
// it is not configured.
typedef struct mrhiSurfaceSlot
{
    uint64_t handle;
    mrhiDevice* device;
    uint32_t swapchain;
} mrhiSurfaceSlot;

// What a pending request waits for.
typedef enum mrhiPendingKind
{
    mrhiPendingAdapters,
    mrhiPendingDevice,
} mrhiPendingKind;

// A request its driver has not answered. Its request id's index is also
// the tag the driver answers with.
typedef struct mrhiPending
{
    uint32_t request;
    mrhiPendingKind kind;
    mrhiPowerPreference preference;
    bool allowSoftware;
    mrhiSurfaceId compatibleSurface;
    mrhiDevice* device;
} mrhiPending;

struct mrhiInstance
{
    mrhiAllocator allocator;
    mrhiInstanceLimits limits;
    size_t bytes;
    // Calls refused as invalid input, and the records of those refusals.
    uint64_t misuse;
    mrhiDiagnosticQueue diagnostics;
    // No driver when its vtable is NULL; external for one the program
    // made (mrhi-0024), whose adapters the core reports as such.
    mrhiInstanceDriver driver;
    bool external;
    // Breaches of the SPI the validation layer found (mrhi-0025), its
    // devices' included, which may count on their threads.
    _Atomic uint64_t driverFaults;
    uint32_t nextRequest;
    uint32_t deviceCount;
    // A ring of limits.notifications records.
    mrhiInstanceNotification* queue;
    uint32_t queueHead;
    uint32_t queueCount;
    // With the queue, at most limits.notifications, so every answer has
    // room.
    mrhiPending* pending;
    uint32_t pendingCount;
    // limits.adapters slots, the listing of their indices in the last
    // answer's order, and room for what the driver reports.
    mrhiAdapterSlot* slots;
    uint32_t* listing;
    uint32_t listed;
    mrhiDriverAdapter* found;
    // Surfaces: ids, and each slot's driver handle and configuration.
    mrhiPool surfaces;
    mrhiSurfaceSlot* surfaceSlots;
};

// Counts one misuse refused by a check, records it, and returns
// mrhi_errorInvalid, for a refusal of invalid input on a live instance.
mrhiResult mrhiMisuse(mrhiInstance* instance, mrhiDiagnosticCode code);

// Appends a record; the caller has made sure there is room.
void mrhiPushInstanceNotification(mrhiInstance* instance, mrhiInstanceNotification notification);

// Whether the queue has room for one more answer.
bool mrhiHasRoomForAnswer(const mrhiInstance* instance);

// A new request id's index, never zero.
uint32_t mrhiNextRequest(mrhiInstance* instance);

// Records work a driver will answer; the caller has checked the room.
void mrhiAddPending(mrhiInstance* instance, mrhiPending pending);

// Answers a pending request at once with an outcome, without its
// driver: a device destroyed while opening, or work without a driver.
void mrhiAnswerNow(mrhiInstance* instance, uint32_t request, mrhiResult outcome);

// The adapter an id names, or NULL for a stale or null id.
const mrhiDriverAdapter* mrhiFindAdapter(const mrhiInstance* instance, mrhiAdapterId adapter);

// What a format can do on an adapter: the driver's answer, nothing for
// a compressed family whose feature the adapter lacks.
mrhiFormatCaps mrhiAdapterFormatCaps(const mrhiInstance* instance, const mrhiDriverAdapter* adapter,
                                     mrhiFormat format);

// Rebuilds the adapter table for an answered search and returns the
// search's outcome.
mrhiResult mrhiRefreshAdapters(mrhiInstance* instance, const mrhiPending* search);

// The driver handle of a live surface, or 0 for a stale or null id.
uint64_t mrhiFindSurface(const mrhiInstance* instance, mrhiSurfaceId surface);

// Destroys every surface left, before the driver goes.
void mrhiDestroySurfaces(mrhiInstance* instance);

// Moves an opening device to ready, or to failed, and returns the
// outcome.
mrhiResult mrhiFinishOpening(mrhiDevice* device, mrhiResult outcome);

#endif // MAUL_RHI_SRC_INSTANCE_CORE_H
