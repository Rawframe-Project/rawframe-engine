// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Linux gamepad's motion sensors: the second evdev device
// its driver makes (hid-playstation's "Motion Sensors", hid-nintendo's
// "IMU"), marked INPUT_PROP_ACCELEROMETER, with acceleration on ABS_X,
// ABS_Y and ABS_Z in units per g and rotation on ABS_RX, ABS_RY and
// ABS_RZ in units per degree per second, as their resolutions say. This
// opens and reads the device; linux_motion_events.h reads what it hands
// over.

#ifndef MAUL_WINDOW_SRC_LINUX_MOTION_H
#define MAUL_WINDOW_SRC_LINUX_MOTION_H

#include "core.h"
#include "linux_motion_events.h"

typedef struct mwinLinuxMotion
{
    // The device, or -1; its node's number.
    int fd;
    int node;
    mwinLinuxMotionEvents events;
} mwinLinuxMotion;

// Reads a device's identity (EVIOCGUNIQ, EVIOCGPHYS, EVIOCGID).
void mwinLinuxIdentityOf(int fd, mwinLinuxPadIdentity* identity);

// Opens /dev/input/eventN as a pad's motion device: false when it is
// not one, or not that pad's.
bool mwinLinuxOpenMotion(mwinLinuxMotion* motion, int node, const mwinLinuxPadIdentity* pad);

// Looks through /dev/input for a pad's motion device.
bool mwinLinuxFindMotion(mwinLinuxMotion* motion, const mwinLinuxPadIdentity* pad);

// Reads the device's events, posting a sample at each report to the
// gamepad in a slot: false when the device is gone.
bool mwinLinuxReadMotion(mwinLinuxMotion* motion, mwinContext* context, uint32_t slot);

void mwinLinuxCloseMotion(mwinLinuxMotion* motion);

#endif // MAUL_WINDOW_SRC_LINUX_MOTION_H
