// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The power facts on Linux: low power from the
// desktop portal's PowerProfileMonitor (power-saver-enabled) on the
// session bus, on battery from UPower's OnBattery on the system bus.
// Each is asked for at the start without waiting, taken at a later
// pump, and followed through PropertiesChanged; unknown where nothing
// answers. The services own both buses (linux_services.h).

#ifndef MAUL_WINDOW_SRC_LINUX_POWER_H
#define MAUL_WINDOW_SRC_LINUX_POWER_H

#include "core.h"
#include "linux_bus.h"

typedef struct mwinLinuxPower
{
    mwinContext* context;
    mwinLinuxBus* session;
    mwinLinuxBus* system;
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
                    mwinLinuxBus* system, uint64_t nowNs);

// Takes the answers that came; after both buses' pumps.
void mwinPowerPump(mwinLinuxPower* power, uint64_t nowNs);

// Stops listening, before the buses close.
void mwinPowerStop(mwinLinuxPower* power);

#endif // MAUL_WINDOW_SRC_LINUX_POWER_H
