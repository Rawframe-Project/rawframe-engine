// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Linux gamepad's battery: the power supply its HID device registers
// beside the input device in sysfs (hid-playstation, hid-nintendo,
// xpadneo and others), as eventN/device/device/power_supply/NAME/
// capacity under the class of input devices. A pad without one has no
// battery the kernel tells, and is wired or unknown alike.

#ifndef MAUL_WINDOW_SRC_LINUX_BATTERY_H
#define MAUL_WINDOW_SRC_LINUX_BATTERY_H

#include <stdbool.h>
#include <stdint.h>

// Room for the capacity file's path.
#define MWIN_LINUX_BATTERY_PATH 192

typedef struct mwinLinuxBattery
{
    // The capacity file, empty for a pad without a battery.
    char path[MWIN_LINUX_BATTERY_PATH];
} mwinLinuxBattery;

// Finds the battery of the input device eventN under a sysfs root
// ("/sys", or a test's own tree): false, with an empty path, when it
// has none.
bool mwinLinuxFindBattery(const char* root, int node, mwinLinuxBattery* batteryOut);

// The battery's charge in percent, or -1 when it has none or the file
// says no number from 0 to 100.
int8_t mwinLinuxReadBattery(const mwinLinuxBattery* battery);

#endif // MAUL_WINDOW_SRC_LINUX_BATTERY_H
