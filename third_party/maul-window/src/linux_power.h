// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The power facts on Linux (window decision W9): low power from the
// desktop portal's PowerProfileMonitor (power-saver-enabled) on the
// session bus, on battery from UPower's OnBattery on the system bus.
// Each is asked for at the start without waiting, taken at a later
// pump, and followed through PropertiesChanged; unknown where nothing
// answers.

#ifndef MAUL_WINDOW_SRC_LINUX_POWER_H
#define MAUL_WINDOW_SRC_LINUX_POWER_H

#include "core.h"
#include "linux_bus.h"

typedef struct mwinLinuxPower
{
    mwinContext* context;
    mwinLinuxBus* session;
    mwinLinuxBus system;
    mwinBusCall saverCall;
    mwinBusCall batteryCall;
    bool sessionListening;
    bool systemListening;
    // What was said: -1 for nothing, else 0 or 1.
    int8_t saver;
    int8_t battery;
} mwinLinuxPower;

// Asks for both facts and listens for their changes.
void mwinPowerStart(mwinLinuxPower* power, mwinContext* context, mwinLinuxBus* session,
                    uint64_t nowNs);

// Reads the system bus and takes the answers that came; after the
// session bus's pump.
void mwinPowerPump(mwinLinuxPower* power, uint64_t nowNs);

// Stops listening and closes the system bus, before the session bus
// closes.
void mwinPowerStop(mwinLinuxPower* power);

#endif // MAUL_WINDOW_SRC_LINUX_POWER_H
