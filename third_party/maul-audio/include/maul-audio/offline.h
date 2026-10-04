// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The offline backend's scripted devices: a host adds and removes
// devices and sets defaults, and the context reacts as it would to the
// platform, so device changes can be tested without hardware.

#ifndef MAUL_AUDIO_OFFLINE_H
#define MAUL_AUDIO_OFFLINE_H

#include "maul-audio/device.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // An offline device. Build it with maudDefaultOfflineDeviceDef. It runs
    // at any rate from 8,000 to 384,000 without conversion.
    typedef struct maudOfflineDeviceDef
    {
        uint32_t cookie;
        maudDirection direction;
        maudChannelLayout layout;
        // Its native rate.
        uint32_t sampleRate;
        // What it leads to.
        maudDeviceForm form;
        // Its display name and key, UTF-8, with their lengths in bytes.
        const char* name;
        size_t nameLength;
        const char* key;
        size_t keyLength;
    } maudOfflineDeviceDef;

    /// Returns the default offline device def: an output, stereo, at
    /// 48,000, of unknown form, with an empty name and key.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudOfflineDeviceDef maudDefaultOfflineDeviceDef(void);

    /// Adds a device to an offline context. If its direction had no device
    /// it becomes the default for both roles, and streams waiting for one
    /// move to it.
    ///
    /// @param context      An offline context.
    /// @param def          The def, from maudDefaultOfflineDeviceDef.
    /// @param deviceIdOut  Receives the device's id; the null id on failure.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer where
    ///         one is required, a def without its cookie, or a value out of
    ///         range; `maud_errorCapacity` past the device limit or the
    ///         name and key limit; `maud_errorUnsupported` on a context that
    ///         is not offline; `maud_errorState` on a thread rendering one of
    ///         the context's streams.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudAddOfflineDevice(maudContext* context,
                                                            const maudOfflineDeviceDef* def,
                                                            maudDeviceId* deviceIdOut);

    /// Removes a device from an offline context, as an unplug would. A
    /// default it was passes to the first remaining device of its direction.
    /// Streams on it move or are suspended.
    ///
    /// @param context  An offline context.
    /// @param device   The device.
    /// @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a
    ///         NULL context; `maud_errorUnsupported` on a context that is not
    ///         offline; `maud_errorState` on a thread rendering one of the
    ///         context's streams.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudRemoveOfflineDevice(maudContext* context,
                                                               maudDeviceId device);

    /// Makes a device the default of its direction for a role. Streams that
    /// follow that default move to it.
    ///
    /// @param context  An offline context.
    /// @param role     The role.
    /// @param device   The device.
    /// @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a
    ///         NULL context or an unknown role; `maud_errorUnsupported` on a
    ///         context that is not offline; `maud_errorState` on a thread
    ///         rendering one of the context's streams.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudSetOfflineDefaultDevice(maudContext* context,
                                                                   maudDeviceRole role,
                                                                   maudDeviceId device);

    /// Changes what a device leads to, as plugging headphones into its jack
    /// does: a change posts maud_notifyRouteChanged.
    ///
    /// @param context  An offline context.
    /// @param device   The device.
    /// @param form     Its new form.
    /// @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a
    ///         NULL context or an unknown form; `maud_errorUnsupported` on a
    ///         context that is not offline; `maud_errorState` on a thread
    ///         rendering one of the context's streams.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudSetOfflineDeviceForm(maudContext* context,
                                                                maudDeviceId device,
                                                                maudDeviceForm form);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_OFFLINE_H
