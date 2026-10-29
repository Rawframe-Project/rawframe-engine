// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What an evdev reader does with each event once the kernel has dropped
// some (SYN_DROPPED, when a reader's queue overflows). The queue then
// holds what is left of the report the drop cut, so, as the kernel's
// evdev documentation says, the reader ignores every event up to and
// including the next SYN_REPORT, and then reads the device's state
// again. It takes plain values; linux_pad.c and linux_motion_events.c
// use it.
//
// A state that cannot be read, as when the device has gone, gives
// nothing: the reader posts nothing for the report, and the read error
// that follows closes the device.

#ifndef MAUL_WINDOW_SRC_LINUX_EVDEV_DROP_H
#define MAUL_WINDOW_SRC_LINUX_EVDEV_DROP_H

#include <limits.h>
#include <linux/input.h>
#include <stdint.h>

// The longs of a key state (EVIOCGKEY), a bit per key code.
#define MWIN_EVDEV_KEY_LONGS                                                                       \
    ((KEY_CNT + sizeof(unsigned long) * CHAR_BIT - 1) / (sizeof(unsigned long) * CHAR_BIT))

typedef enum mwinEvdevStep
{
    // Read the event as usual.
    mwin_evdevTake,
    // Ignore it: it belongs to a report a drop cut.
    mwin_evdevSkip,
    // The report a drop cut has ended: read the device's state again.
    mwin_evdevResync,
} mwinEvdevStep;

// The step for an event, with whether the reader is skipping since a
// drop, which it updates.
mwinEvdevStep mwinEvdevDropStep(bool* dropping, uint16_t type, uint16_t code);

// Sets buttons from a resync's key state, each from the bit of its key
// code: false, setting nothing, when the state could not be read.
bool mwinEvdevResyncButtons(const unsigned long keys[MWIN_EVDEV_KEY_LONGS], bool read,
                            const uint16_t* codes, uint8_t count, bool* buttons);

#endif // MAUL_WINDOW_SRC_LINUX_EVDEV_DROP_H
