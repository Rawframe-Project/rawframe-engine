// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Devices: the endpoints a context's backend offers, the default for
// each role, and what each device runs at natively.

#ifndef MAUL_AUDIO_DEVICE_H
#define MAUL_AUDIO_DEVICE_H

#include "maul-audio/context.h"
#include "maul-audio/layout.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // Names a device of a context: a 1-based slot, 0 for the null id, and a
    // generation. A device that disappears and comes back is a new id; its
    // key stays the same.
    typedef struct maudDeviceId
    {
        uint32_t index1;
        uint32_t generation;
    } maudDeviceId;

    // Which way samples move.
    typedef uint8_t maudDirection;

    enum
    {
        // From the host to the device: playback.
        maud_directionOutput = 0,
        // From the device to the host: capture.
        maud_directionInput = 1,
        // Streams only: capture and playback in one callback, on a device
        // of each direction.
        maud_directionDuplex = 2,
    };

    // What a default device is the default for.
    typedef uint8_t maudDeviceRole;

    enum
    {
        // Games, media and everything else.
        maud_roleGeneral = 0,
        // Voice calls and chat.
        maud_roleCommunications = 1,
    };

    // What a device's active port leads to, as the platform reports it.
    // Connections that say nothing of the far end, such as USB and
    // Bluetooth, are unknown.
    typedef uint8_t maudDeviceForm;

    enum
    {
        maud_formUnknown = 0,
        // Loudspeakers: built-in, external, a car's or a television's.
        maud_formSpeakers = 1,
        maud_formHeadphones = 2,
        // Headphones with a microphone, or that microphone.
        maud_formHeadset = 3,
        // A telephone's earpiece, or its microphone.
        maud_formHandset = 4,
        maud_formMicrophone = 5,
        // An analog line connector.
        maud_formLine = 6,
        // A digital connector: HDMI, DisplayPort, S/PDIF.
        maud_formDigital = 7,
    };

    // What a device is and runs at.
    typedef struct maudDeviceInfo
    {
        maudDirection direction;
        // Whether the device is the default for each role.
        bool defaultGeneral;
        bool defaultCommunications;
        // The layout and rates are maud_layoutNone and 0 where the platform
        // cannot tell them without opening the device, as on ALSA, whose
        // devices are not opened to be listed; a stream that opens one
        // learns its rate.
        maudChannelLayout nativeLayout;
        uint32_t nativeSampleRate;
        // The rates the device can run at without conversion.
        uint32_t minSampleRate;
        uint32_t maxSampleRate;
        // What its active port leads to; maud_notifyRouteChanged reports a
        // change.
        maudDeviceForm form;
    } maudDeviceInfo;

    /// Lists the devices of one direction, in a stable order.
    ///
    /// @param context     The context.
    /// @param direction   Output or input devices.
    /// @param idsOut      Receives up to capacity ids. May be NULL when
    ///                    capacity is 0.
    /// @param capacity    The room in idsOut.
    /// @param countOut    Receives the number of devices, which may exceed
    ///                    capacity.
    /// @return `maud_success`, or `maud_errorInvalid` for a NULL pointer
    ///         where one is required or an unknown direction.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudGetDevices(const maudContext* context,
                                                      maudDirection direction, maudDeviceId* idsOut,
                                                      uint32_t capacity, uint32_t* countOut);

    /// Reports what a device is and runs at.
    ///
    /// @param context  The context.
    /// @param device   The device.
    /// @param infoOut  Receives the information.
    /// @return `maud_success`; `maud_errorStale` for an id that names no
    ///         device; `maud_errorInvalid` for a NULL pointer.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudGetDeviceInfo(const maudContext* context,
                                                         maudDeviceId device,
                                                         maudDeviceInfo* infoOut);

    /// Copies a device's display name, UTF-8, without a terminating NUL.
    ///
    /// @param context    The context.
    /// @param device     The device.
    /// @param bytesOut   Receives the name. May be NULL when capacity is 0.
    /// @param capacity   The room in bytesOut.
    /// @param lengthOut  Receives the name's length in bytes.
    /// @return `maud_success`; `maud_errorCapacity` when the name does not
    ///         fit, with nothing copied and lengthOut set; `maud_errorStale`;
    ///         `maud_errorInvalid` for a NULL pointer where one is required.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudGetDeviceName(const maudContext* context,
                                                         maudDeviceId device, char* bytesOut,
                                                         size_t capacity, size_t* lengthOut);

    /// Copies a device's persistent key, UTF-8, without a terminating NUL:
    /// the same for the same device after it is unplugged and plugged
    /// again, and across restarts where the platform allows, so a host may
    /// store it.
    ///
    /// @param context    The context.
    /// @param device     The device.
    /// @param bytesOut   Receives the key. May be NULL when capacity is 0.
    /// @param capacity   The room in bytesOut.
    /// @param lengthOut  Receives the key's length in bytes.
    /// @return `maud_success`; `maud_errorCapacity` when the key does not
    ///         fit, with nothing copied and lengthOut set; `maud_errorStale`;
    ///         `maud_errorInvalid` for a NULL pointer where one is required.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudGetDeviceKey(const maudContext* context,
                                                        maudDeviceId device, char* bytesOut,
                                                        size_t capacity, size_t* lengthOut);

    /// Reports the default device of a direction for a role.
    ///
    /// @param context      The context.
    /// @param direction    Output or input.
    /// @param role         The role.
    /// @param deviceIdOut  Receives the device; the null id when there is
    ///                     none.
    /// @return `maud_success`; `maud_empty` when the direction has no
    ///         device; `maud_errorInvalid` for a NULL pointer or an unknown
    ///         direction or role.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudGetDefaultDevice(const maudContext* context,
                                                            maudDirection direction,
                                                            maudDeviceRole role,
                                                            maudDeviceId* deviceIdOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_DEVICE_H
