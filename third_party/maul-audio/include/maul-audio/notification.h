// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context's notification queue: typed records of what changed in
// its devices and streams, drained by the host off the audio thread.

#ifndef MAUL_AUDIO_NOTIFICATION_H
#define MAUL_AUDIO_NOTIFICATION_H

#include "maul-audio/focus.h"
#include "maul-audio/stream.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // What a notification reports.
    typedef uint8_t maudNotificationKind;

    enum
    {
        // A device appeared: device and direction.
        maud_notifyDeviceAdded = 1,
        // A device disappeared: device and direction. The id is stale.
        maud_notifyDeviceRemoved = 2,
        // The default device of direction for role changed to device,
        // the null id when the direction has no device left.
        maud_notifyDefaultChanged = 3,
        // stream now runs on device.
        maud_notifyStreamMoved = 4,
        // stream cannot run, for reason.
        maud_notifyStreamSuspended = 5,
        // stream can run again.
        maud_notifyStreamResumed = 6,
        // stream now runs at sampleRate.
        maud_notifyStreamFormatChanged = 7,
        // droppedCount records did not fit in the queue and were lost.
        // Query the devices and stream statuses again.
        maud_notifyOverflow = 8,
        // device, of direction, now leads to form: its route changed,
        // as when headphones are plugged into the jack its port serves.
        maud_notifyRouteChanged = 9,
        // The context's audio focus is now focus (maudRequestFocus).
        maud_notifyFocusChanged = 10,
        // What the platform's spatializer does on device, an output,
        // changed (maudDeviceInfo's spatializer, headTracking and
        // spatialObjects).
        maud_notifySpatializerChanged = 11,
    };

    // One change. Fields a kind does not name are zero.
    typedef struct maudNotification
    {
        maudNotificationKind kind;
        maudDirection direction;
        maudDeviceRole role;
        maudSuspendReason reason;
        maudDeviceId device;
        maudStreamId stream;
        uint32_t sampleRate;
        uint32_t droppedCount;
        maudDeviceForm form;
        maudFocus focus;
    } maudNotification;

    /// Takes the oldest notification from the context's queue. On native
    /// backends whose platform reports changes through a loop the library
    /// owns, such as PipeWire, it first takes in what the platform reported
    /// since the last call, without blocking, and reconnects to a platform
    /// service that went away; a host that drains the queue regularly
    /// keeps the device table current.
    ///
    /// @param context          The context.
    /// @param notificationOut  Receives the record.
    /// @return `maud_success`; `maud_empty` when the queue is drained;
    ///         `maud_errorInvalid` for a NULL pointer; `maud_errorState` on a
    ///         thread rendering one of the context's streams.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudNextNotification(maudContext* context,
                                                            maudNotification* notificationOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_NOTIFICATION_H
