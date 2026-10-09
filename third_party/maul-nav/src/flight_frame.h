// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// World points in voxels of a flight volume's frame (mnav-0015): x and z
// from the origin, y from the volume's floor voxel.

#ifndef MAUL_NAV_SRC_FLIGHT_FRAME_H
#define MAUL_NAV_SRC_FLIGHT_FRAME_H

#include "maul-nav/base.h"
#include "maul-nav/flight.h"

#include <stdint.h>

// The farthest a point may lie from the frame's origin, in voxels on any
// axis, so that a walk's voxel indices stay well within 32 bits.
#define MNAV_FLIGHT_MAX_VOXELS 268435456.0

typedef struct mnavFlightFrame
{
    mnavPos3 origin;
    double voxel;
    int32_t floorVoxel;
    int32_t layers;
} mnavFlightFrame;

mnavFlightFrame mnavMakeFlightFrame(const mnavFlightVolume* volume);

// A world point in voxels, and the voxel holding it; false for a point
// not finite or past MNAV_FLIGHT_MAX_VOXELS.
bool mnavFlightToVoxels(const mnavFlightFrame* frame, mnavPos3 point, double out[3],
                        int32_t voxel[3]);

mnavPos3 mnavFlightToWorld(const mnavFlightFrame* frame, const double v[3]);

#endif // MAUL_NAV_SRC_FLIGHT_FRAME_H
