// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// World points in voxels of a flight volume's frame.

#include "flight_frame.h"

#include "flight_volume.h"

#include "maul-nav/base.h"
#include "maul-nav/flight.h"

#include <math.h>
#include <stdint.h>

mnavFlightFrame mnavMakeFlightFrame(const mnavFlightVolume* volume)
{
    return (mnavFlightFrame){volume->def.origin, (double)volume->def.voxelSize,
                             volume->shape.floorVoxel,
                             volume->shape.cubeCount * volume->def.tileVoxels};
}

bool mnavFlightToVoxels(const mnavFlightFrame* frame, mnavPos3 point, double out[3],
                        int32_t voxel[3])
{
    out[0] = (point.x - frame->origin.x) / frame->voxel;
    out[1] = (point.y - frame->origin.y) / frame->voxel - (double)frame->floorVoxel;
    out[2] = (point.z - frame->origin.z) / frame->voxel;
    for (int32_t k = 0; k < 3; ++k)
    {
        // A NaN fails the comparison.
        if (!(fabs(out[k]) <= MNAV_FLIGHT_MAX_VOXELS))
        {
            return false;
        }
        voxel[k] = (int32_t)floor(out[k]);
    }
    return true;
}

mnavPos3 mnavFlightToWorld(const mnavFlightFrame* frame, const double v[3])
{
    return (mnavPos3){frame->origin.x + v[0] * frame->voxel,
                      frame->origin.y + (v[1] + (double)frame->floorVoxel) * frame->voxel,
                      frame->origin.z + v[2] * frame->voxel};
}
