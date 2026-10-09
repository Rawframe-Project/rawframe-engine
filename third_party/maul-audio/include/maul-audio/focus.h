// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Audio focus: asking the platform for the application's turn to play,
// and the state of that turn as the platform reports it. The library
// acts on none of it; the host pauses, ducks or stops its streams.

#ifndef MAUL_AUDIO_FOCUS_H
#define MAUL_AUDIO_FOCUS_H

#include "maul-audio/device.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // Where the context stands with the platform's audio focus.
    typedef uint8_t maudFocus;

    enum
    {
        // It has not asked, released it, or was refused.
        maud_focusNone = 0,
        // It holds focus: play as asked.
        maud_focusHeld = 1,
        // Another application took focus for good; it comes back only with
        // a new request. Stop, or pause until the user acts.
        maud_focusLost = 2,
        // Another application took focus for a while, such as a call:
        // pause until it is held again.
        maud_focusPaused = 3,
        // Another application plays for a while over this one, such as
        // navigation: play quieter, or pause, until it is held again.
        maud_focusDucked = 4,
    };

    // What a focus request asks for.
    typedef uint8_t maudFocusRequest;

    enum
    {
        // Gives focus back.
        maud_focusRelease = 0,
        // Focus for as long as the application plays: others stop.
        maud_focusLasting = 1,
        // Focus for a moment, such as a notification sound: others pause.
        maud_focusBrief = 2,
        // Focus for a moment over others, which play quieter meanwhile.
        maud_focusBriefMixed = 3,
    };

    /// Asks the platform for audio focus, or gives it back. Ask right
    /// before playing; on Android an application targeting Android 15
    /// may ask only while it is the top application or runs a foreground
    /// service. The state that follows comes as maud_notifyFocusChanged
    /// records and from maudGetContextFocus; a request the platform
    /// delays, as during a call, is held once the platform grants it.
    /// Releasing reports maud_focusNone. The library never pauses or ducks
    /// streams for focus: the host does.
    ///
    /// @param context  The context.
    /// @param request  What to ask for, or maud_focusRelease.
    /// @param role     What the audio is for: maud_roleGeneral for media
    ///                 and games, maud_roleCommunications for a call.
    /// @return `maud_success` when granted, delayed or released;
    ///         `maud_errorPlatform` when the platform refuses;
    ///         `maud_errorUnsupported` where the backend has no audio focus
    ///         (the desktop platforms, the web, the offline backend, and
    ///         Android without the Java half); `maud_errorInvalid` for a
    ///         NULL context, an unknown request or role; `maud_errorState`
    ///         when called on a thread that is rendering one of the
    ///         context's streams, which counts as misuse.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudRequestFocus(maudContext* context,
                                                        maudFocusRequest request,
                                                        maudDeviceRole role);

    /// Reads where the context stands with audio focus, as of the last
    /// drain of its notifications.
    ///
    /// @param context   The context.
    /// @param focusOut  Receives the state.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudGetContextFocus(const maudContext* context,
                                                           maudFocus* focusOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_FOCUS_H
