// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Linux gamepad's motion sensors (mwin-0028): the second evdev device
// its driver makes (hid-playstation's "Motion Sensors", hid-nintendo's
// "IMU"), marked INPUT_PROP_ACCELEROMETER, with acceleration on ABS_X,
// ABS_Y and ABS_Z in units per g and rotation on ABS_RX, ABS_RY and
// ABS_RZ in units per degree per second, as their resolutions say. It
// belongs to the pad with its unique id (the controller's address)
// where both have one, else with its physical path. Samples are timed
// by MSC_TIMESTAMP where the device sends it, and turned into the
// contract's frame as SDL turns them: Nintendo's axes reordered.

#ifndef MAUL_WINDOW_SRC_LINUX_MOTION_H
#define MAUL_WINDOW_SRC_LINUX_MOTION_H

#include "core.h"

// A pad's identity, as the motion device must share it.
typedef struct mwinLinuxPadIdentity
{
    char uniq[64];
    char phys[64];
    uint16_t vendor;
} mwinLinuxPadIdentity;

typedef struct mwinLinuxMotion
{
    // The device, or -1; its node's number.
    int fd;
    int node;
    // Each axis's raw value and the factor to the contract's units, in
    // the order X, Y, Z, RX, RY, RZ.
    int32_t raw[6];
    float scale[6];
    bool nintendo;
    // The device's microseconds, counted from its first stamp (stamped
    // once it sent one), as the samples' time.
    bool stamped;
    uint32_t stamp;
    uint64_t timeNs;
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
