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

// The corners of a 16-gon on the unit circle, written out so that every
// platform has the same: the outline debug drawing gives an agent.
extern const double mnavOutline[16][2];

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

// Fills the set's obstacle grid with spheres, by their bounds on the
// ground (X and Z), its fastest speed their fastest in space; count is
// within the set's obstacle points.
void mnavBuildSphereGrid(mnavAvoidance* avoidance, const mnavSphere* spheres, int32_t count);

// The spheres an agent may meet within the obstacle horizon, the nearest
// first (ties by id, then index), at most the set's obstacle neighbours,
// into list; the grid holds those spheres.
int32_t mnavNearSpheres(mnavAvoidance* avoidance, const mnavAgent3D* agent,
                        const mnavSphere* spheres, int32_t count, mnavObstacleNear* list);

#endif // MAUL_NAV_SRC_AVOIDANCE_H
