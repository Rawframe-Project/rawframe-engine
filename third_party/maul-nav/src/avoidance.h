// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An avoidance set's memory (mnav-0006), shared by the ground and the
// space calls, and the checks of their input's ranges.

#ifndef MAUL_NAV_SRC_AVOIDANCE_H
#define MAUL_NAV_SRC_AVOIDANCE_H

#include "allocator.h"
#include "crowd.h"
#include "obstacle.h"
#include "orca.h"
#include "orca3.h"

#include "maul-nav/avoidance.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

// How far right of its preferred velocity a held-back agent aims, as a
// fraction of its speed.
#define MNAV_KEEP_RIGHT 0.01

struct mnavAvoidance
{
    mnavMemory memory;
    mnavAvoidanceDef def;
    // The neighbour grid; its table has room for tableCapacity runs.
    mnavCrowd crowd;
    int32_t tableCapacity;
    mnavObstacleVertex* vertices;
    // The obstacles or spheres near an agent.
    mnavObstacleNear* near;
    mnavObstacleGrid grid;
    // Room for the obstacle lines or sphere planes, then the agents'.
    mnavLine* lines;
    mnavLine* projected;
    mnavPlane* planes;
    mnavPlane* projectedPlanes;
};

static inline bool mnavAvoidPositive(double v)
{
    return isfinite(v) && v > 0.0;
}

static inline bool mnavAvoidTime(double v)
{
    return isfinite(v) && v >= MNAV_MIN_AVOIDANCE_TIME;
}

static inline bool mnavAvoidSpeed(double v)
{
    return isfinite(v) && fabs(v) <= MNAV_MAX_AVOIDANCE_SPEED;
}

static inline bool mnavAvoidCoordinate(double v)
{
    return isfinite(v) && fabs(v) <= MNAV_MAX_AVOIDANCE_COORDINATE;
}

static inline bool mnavAvoidRadius(double v)
{
    return mnavAvoidPositive(v) && v <= MNAV_MAX_AVOIDANCE_RADIUS;
}

#endif // MAUL_NAV_SRC_AVOIDANCE_H
