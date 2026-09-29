// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the system says about the user's preferences and the machine:
// the theme and accent color, reduced motion and text scale, the power
// source, and the preferred locales. Each has a notification when it
// changes; the values are read at any time.

#ifndef MAUL_WINDOW_SYSTEM_H
#define MAUL_WINDOW_SYSTEM_H

#include "maul-window/context.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // The system's light or dark look.
    typedef uint8_t mwinTheme;

    enum
    {
        mwin_themeUnknown = 0,
        mwin_themeLight = 1,
        mwin_themeDark = 2,
    };

    // A yes or no the platform may not know.
    typedef uint8_t mwinTristate;

    enum
    {
        mwin_unknown = 0,
        mwin_no = 1,
        mwin_yes = 2,
    };

    // The system's preferences and facts.
    typedef struct mwinSystemFacts
    {
        mwinTheme theme;
        // The accent color as 0xRRGGBBAA, valid when hasAccent is set.
        bool hasAccent;
        uint32_t accent;
        // The user asked for less motion.
        bool reducedMotion;
        // The user's text scale, 1 for the platform's default size.
        float textScale;
        mwinTristate onBattery;
        mwinTristate lowPower;
        // Windows shows its snap layouts over a window's maximize hit
        // region (Windows 11); a named optional capability, false
        // elsewhere.
        bool snapLayouts;
    } mwinSystemFacts;

    /// Reads the system's preferences and facts.
    ///
    /// @param context   The context.
    /// @param factsOut  Receives them.
    /// @return `mwin_success`; `mwin_errorInvalid` for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinGetSystemFacts(const mwinContext* context,
                                                          mwinSystemFacts* factsOut);

    /// Reads the user's preferred locales, most preferred first, as BCP 47
    /// tags separated by commas ("de-DE,en-GB"); empty where the platform
    /// does not say.
    ///
    /// @param context    The context.
    /// @param buffer     Receives the list, not NUL-terminated. May be NULL
    ///                   when capacity is 0.
    /// @param capacity   The bytes buffer holds.
    /// @param lengthOut  Receives the list's length in bytes.
    /// @return `mwin_success`; `mwin_errorCapacity` when the list does not
    ///         fit (the bytes that fit are written); `mwin_errorInvalid`
    ///         for a NULL argument.
    /// @par Thread safety
    /// Main thread only.
    MWIN_NODISCARD MWIN_API mwinResult mwinGetPreferredLocales(const mwinContext* context,
                                                               char* buffer, size_t capacity,
                                                               size_t* lengthOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_WINDOW_SYSTEM_H
