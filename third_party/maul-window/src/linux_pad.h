// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Linux gamepads, for the Wayland and X11 backends alike: evdev devices
// in /dev/input, found when a context starts and as they come and go
// (inotify), read without blocking at each pump. A device is numbered as
// SDL numbers it, so SDL_GameControllerDB's mappings apply; one the
// database lacks is mapped by the kernel's gamepad layout when it has
// one (BTN_SOUTH and the rest), and raw otherwise. Rumble is force
// feedback, where the device takes it and the node opens for writing;
// motion comes from the pad's motion device (linux_motion.h), found with
// the pad or when it comes after it; the battery from the power supply
// beside it in sysfs (linux_battery.h), read again every few seconds.

#ifndef MAUL_WINDOW_SRC_LINUX_PAD_H
#define MAUL_WINDOW_SRC_LINUX_PAD_H

#include "core.h"
#include "linux_battery.h"
#include "linux_motion.h"
#include "pad_db.h"
#include "pad_map.h"

// The buttons, axes and hats of a device as SDL numbers them.
#define MWIN_LINUX_PAD_BUTTONS MWIN_PAD_NUMBERED_BUTTONS
#define MWIN_LINUX_PAD_AXES    MWIN_PAD_NUMBERED_AXES
#define MWIN_LINUX_PAD_HATS    MWIN_PAD_NUMBERED_HATS

typedef struct mwinLinuxPad
{
    // The device, or -1 for a free entry; its node's number (eventN).
    int fd;
    int node;
    // The core's gamepad slot.
    uint32_t slot;
    // Its controls; the mapping from the database or made from the
    // kernel's layout (own), or none for a raw gamepad.
    mwinPadControls controls;
    mwinPadMapping own;
    // Key codes of the buttons in SDL's order; for each absolute axis
    // code, its axis number plus 1, or 0.
    uint16_t buttonCodes[MWIN_LINUX_PAD_BUTTONS];
    uint8_t axisOf[64];
    // The hats present (bit per hat) and SDL's number of each.
    uint8_t hatNumbers[MWIN_LINUX_PAD_HATS];
    // The ranges of the axes.
    int32_t minimum[MWIN_LINUX_PAD_AXES];
    int32_t maximum[MWIN_LINUX_PAD_AXES];
    // The rumble effect uploaded, or -1.
    int16_t effect;
    // Who it is, its motion device (fd -1 for none) and its battery.
    mwinLinuxPadIdentity identity;
    mwinLinuxMotion motion;
    mwinLinuxBattery battery;
} mwinLinuxPad;

typedef struct mwinLinuxPads
{
    mwinContext* context;
    // The inotify watch of /dev/input, or -1.
    int watch;
    // When the batteries were last read.
    uint64_t batteryNs;
    // One per gamepad slot of the context.
    mwinLinuxPad* pads;
} mwinLinuxPads;

// Finds the gamepads connected and starts watching for more; false only
// when the memory for them could not be had.
bool mwinLinuxPadsStart(mwinLinuxPads* pads, mwinContext* context);
void mwinLinuxPadsStop(mwinLinuxPads* pads);

// Reads what the devices and /dev/input say, without waiting.
void mwinLinuxPadsPump(mwinLinuxPads* pads);

// The backends' rumble, and their motion sensors turned on or off: the
// motion device streams whenever it is open, so turning them is the
// core's alone.
mwinResult mwinLinuxPadsRumble(mwinLinuxPads* pads, uint32_t slot, float low, float high,
                               uint32_t durationMs);
mwinResult mwinLinuxPadsSetMotion(mwinContext* context, uint32_t slot, bool enabled);

#endif // MAUL_WINDOW_SRC_LINUX_PAD_H
