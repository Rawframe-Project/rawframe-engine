// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The desktop's look and motion on Linux, from the Settings portal
// (mwin-0018): asked for at the start without waiting, taken
// at a later pump, and followed through SettingChanged.
//
// - The theme is org.freedesktop.appearance's color-scheme, unknown
//   for no preference.
// - The accent is its accent-color, none when out of range.
// - Reduced motion is its reduced-motion, else GNOME's
//   enable-animations off or KDE's AnimationDurationFactor of 0.
// - The text scale is GNOME's text-scaling-factor.

#ifndef MAUL_WINDOW_SRC_LINUX_SETTINGS_H
#define MAUL_WINDOW_SRC_LINUX_SETTINGS_H

#include "core.h"
#include "linux_bus.h"

typedef struct mwinLinuxSettings
{
    mwinContext* context;
    mwinLinuxBus* bus;
    mwinBusCall call;
    bool listening;
    // What the desktop said, -1 or less where it said nothing.
    int8_t colorScheme;
    int8_t reducedMotion;
    int8_t animations;
    int8_t kdeAnimations;
    double textScale;
    bool hasAccent;
    uint32_t accent;
} mwinLinuxSettings;

// Connects to the bus, listens for changes and asks for the settings.
void mwinSettingsStart(mwinLinuxSettings* settings, mwinContext* context, mwinLinuxBus* bus,
                       uint64_t nowNs);

// Takes the answer once it came; after the bus's pump.
void mwinSettingsPump(mwinLinuxSettings* settings, uint64_t nowNs);

// Stops listening, before the bus closes.
void mwinSettingsStop(mwinLinuxSettings* settings);

#endif // MAUL_WINDOW_SRC_LINUX_SETTINGS_H
