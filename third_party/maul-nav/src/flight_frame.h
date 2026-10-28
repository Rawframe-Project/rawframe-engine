// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// World points in voxels of a flight volume's frame: x and z
// from the origin, y from the volume's floor voxel.

#ifndef MAUL_NAV_SRC_FLIGHT_FRAME_H
#define MAUL_NAV_SRC_FLIGHT_FRAME_H

#include "maul-nav/base.h"
#include "maul-nav/flight.h"

#include <stdint.h>

typedef struct mnavFlightFrame
{
    mnavPos3 origin;
    double voxel;
    int32_t floorVoxel;
    int32_t layers;
} mnavFlightFrame;

mnavFlightFrame mnavMakeFlightFrame(const mnavFlightVolume* volume);

// A world point in voxels, and the voxel holding it: mnav_errorInvalid for
// a NaN coordinate, mnav_errorLimit for one past MNAV_MAX_FLIGHT_VOXELS,
// an infinity included.
mnavResult mnavFlightToVoxels(const mnavFlightFrame* frame, mnavPos3 point, double out[3],
                              int32_t voxel[3]);

mnavPos3 mnavFlightToWorld(const mnavFlightFrame* frame, const double v[3]);

#endif // MAUL_NAV_SRC_FLIGHT_FRAME_H
