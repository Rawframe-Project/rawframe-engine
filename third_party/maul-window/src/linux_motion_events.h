// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Linux gamepad's motion device read from plain values: which pad it
// belongs to, its axes' factors from their resolutions, and its evdev
// events turned into timed samples in the contract's frame. A device
// hands these over and the reading must hold up against them, so it
// takes no handle, and is tested on Linux without a device and fuzzed;
// linux_motion.c opens and reads the device and hands them on.
//
// The device belongs to the pad with its unique id (the controller's
// address) where both have one, else with its physical path. Samples are
// timed on the records' clock: by their report's event time, or, where
// the device sends MSC_TIMESTAMP, by its stamps counted from the event
// time of the first report that had one, which spaces them as the device
// took them. They are turned into the contract's frame as SDL turns
// them: Nintendo's axes reordered.

#ifndef MAUL_WINDOW_SRC_LINUX_MOTION_EVENTS_H
#define MAUL_WINDOW_SRC_LINUX_MOTION_EVENTS_H

#include "linux_evdev_drop.h"

#include <stdint.h>

// The motion device's axes: acceleration on ABS_X, ABS_Y and ABS_Z,
// rotation on ABS_RX, ABS_RY and ABS_RZ.
#define MWIN_LINUX_MOTION_AXES 6

// A pad's identity, as the motion device must share it.
typedef struct mwinLinuxPadIdentity
{
    char uniq[64];
    char phys[64];
    uint16_t vendor;
} mwinLinuxPadIdentity;

// A motion device's reading between reports.
typedef struct mwinLinuxMotionEvents
{
    // Each axis's raw value and the factor to the contract's units, in
    // the order X, Y, Z, RX, RY, RZ.
    int32_t raw[MWIN_LINUX_MOTION_AXES];
    float scale[MWIN_LINUX_MOTION_AXES];
    bool nintendo;
    // A stamp in the report under way, and the last report's stamp (once
    // a report had one) with the time it gave the report.
    bool stamping;
    uint32_t next;
    bool stamped;
    uint32_t stamp;
    uint64_t timeNs;
    // Skipping what is left of a report the kernel's drop cut.
    bool dropping;
} mwinLinuxMotionEvents;

// What an event leaves the reader to do.
typedef enum mwinLinuxMotionStep
{
    mwin_motionNothing,
    // A report ended: post its sample.
    mwin_motionSample,
    // A report the kernel's drop cut ended, and gave no sample: read the
    // axes' current values again, through mwinLinuxMotionAxis.
    mwin_motionResync,
} mwinLinuxMotionStep;

// A report's sample, in the contract's frame and units.
typedef struct mwinLinuxMotionSample
{
    float acceleration[3];
    float rate[3];
    uint64_t timeNs;
} mwinLinuxMotionSample;

// Whether two devices are parts of one controller: the same unique id
// where both have one (Bluetooth pads share the adapter's path), else
// the same path.
bool mwinLinuxSameController(const mwinLinuxPadIdentity* a, const mwinLinuxPadIdentity* b);

// Starts a reading for a pad's motion device, its axes turned as the
// pad's vendor needs.
void mwinLinuxMotionStart(mwinLinuxMotionEvents* events, const mwinLinuxPadIdentity* pad);

// Sets an axis (0 to 5, in the order of raw) from its current value and
// its resolution: false when the resolution says nothing.
bool mwinLinuxMotionAxis(mwinLinuxMotionEvents* events, unsigned axis, int32_t value,
                         int32_t resolution);

// Takes an event at its time, with the report's sample where it ends one.
mwinLinuxMotionStep mwinLinuxMotionEvent(mwinLinuxMotionEvents* events, uint16_t type,
                                         uint16_t code, int32_t value, uint64_t timeNs,
                                         mwinLinuxMotionSample* sample);

#endif // MAUL_WINDOW_SRC_LINUX_MOTION_EVENTS_H
