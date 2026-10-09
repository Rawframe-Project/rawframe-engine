// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Changes to the device table, as a backend reports them: each posts
// its notifications and keeps the streams on their devices.

#ifndef MAUL_AUDIO_SRC_DEVICE_H
#define MAUL_AUDIO_SRC_DEVICE_H

#include "context_core.h"

// A device as a backend describes it. The default flags of info are
// ignored.
typedef struct maudDeviceSpec
{
    maudDeviceInfo info;
    const char* name;
    size_t nameLength;
    const char* key;
    size_t keyLength;
} maudDeviceSpec;

// Adds a device. If its direction had none, it becomes the default for
// every role. maud_errorCapacity past the device or text limit.
maudResult maudAddDevice(maudContext* context, const maudDeviceSpec* spec,
                         maudDeviceId* deviceIdOut);

// Removes a live device. Every default it was passes to the first
// remaining device of its direction, or to the null id.
void maudRemoveDevice(maudContext* context, maudDeviceSlot* slot);

// Brings the table in line with a backend's scan of count specs: every
// live device not among them is removed, except one whose key is kept
// (NULL for none); every spec not yet a device is added; a device whose
// layout or rates changed takes the new ones, and native streams their
// rate; one whose form changed reports its new route. Devices match by
// direction and key. maud_errorCapacity when the table is full.
maudResult maudSyncDevices(maudContext* context, const maudDeviceSpec* specs, uint32_t count,
                           const char* kept);

// The live device of direction whose key is key, length bytes, or the
// null id.
maudDeviceId maudFindDeviceByKey(const maudContext* context, maudDirection direction,
                                 const char* key, size_t length);

// The length of text cut to at most limit bytes without splitting a
// UTF-8 sequence: a device name as a backend reports it, cut to the
// context's text limit.
size_t maudCutUtf8(const char* text, size_t limit);

// Sets a live device's form; a change posts maud_notifyRouteChanged.
void maudSetDeviceForm(maudContext* context, maudDeviceSlot* slot, maudDeviceForm form);

// Sets a live device's spatializer state from info's spatializer,
// headTracking and spatialObjects; a change posts
// maud_notifySpatializerChanged.
void maudSetDeviceSpatializer(maudContext* context, maudDeviceSlot* slot,
                              const maudDeviceInfo* info);

// Makes a live device the default of its direction for role.
void maudSetDefaultDevice(maudContext* context, maudDeviceRole role, maudDeviceId device);

#endif // MAUL_AUDIO_SRC_DEVICE_H
