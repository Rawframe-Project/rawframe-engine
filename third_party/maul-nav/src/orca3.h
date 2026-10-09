// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// ORCA's linear programs in space (mnav-0006), after RVO2-3D's
// linearProgram1 to 4 and in binary64: the velocity nearest a wanted one
// within a speed that keeps to the front of every plane, or that breaks
// the planes least.

#ifndef MAUL_NAV_SRC_ORCA3_H
#define MAUL_NAV_SRC_ORCA3_H

#include "maul-nav/base.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

// Vector helpers in space.
static inline mnavPos3 mnavAdd3(mnavPos3 a, mnavPos3 b)
{
    return (mnavPos3){a.x + b.x, a.y + b.y, a.z + b.z};
}

static inline mnavPos3 mnavSub3(mnavPos3 a, mnavPos3 b)
{
    return (mnavPos3){a.x - b.x, a.y - b.y, a.z - b.z};
}

static inline mnavPos3 mnavScale3(mnavPos3 a, double s)
{
    return (mnavPos3){a.x * s, a.y * s, a.z * s};
}

static inline double mnavDot3(mnavPos3 a, mnavPos3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline mnavPos3 mnavCross3(mnavPos3 a, mnavPos3 b)
{
    return (mnavPos3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// The unit vector along a, or {0, 0, 0} when a has no length; a vector
// too short to square accurately is first scaled up by 2^600, as in 2D.
static inline mnavPos3 mnavNormalize3(mnavPos3 a)
{
    double d = mnavDot3(a, a);
    if (d < 0x1p-1000)
    {
        a = mnavScale3(a, 0x1p600);
        d = mnavDot3(a, a);
        if (d == 0.0)
        {
            return (mnavPos3){0.0, 0.0, 0.0};
        }
    }
    return mnavScale3(a, 1.0 / sqrt(d));
}

// A half-space of allowed velocities: those on the side of the plane
// through point that normal, a unit vector, points to.
typedef struct mnavPlane
{
    mnavPos3 point;
    mnavPos3 normal;
} mnavPlane;

// The 3D program: the velocity within radius in front of every plane
// nearest wanted, or with directionOpt the farthest along wanted, a unit
// vector. Returns the count when it holds, else the first plane it could
// not keep, result then being the best before it.
int32_t mnavPlaneProgram3(const mnavPlane* planes, int32_t count, double radius, mnavPos3 wanted,
                          bool directionOpt, mnavPos3* result);

// The 4D program, after the 3D one failed at plane first: keeps the
// first fixed planes and finds the velocity that breaks the others
// least. Needs scratch room for count planes.
void mnavPlaneProgram4(const mnavPlane* planes, int32_t count, int32_t fixed, int32_t first,
                       double radius, mnavPlane* scratch, mnavPos3* result);

// The ORCA plane of a sphere against another, as mnavPairLine builds a
// line: combined is their radii's sum, share the first's part of the
// avoidance, 1 against an obstacle.
mnavPlane mnavPairPlane(mnavPos3 selfPosition, mnavPos3 selfVelocity, mnavPos3 otherPosition,
                        mnavPos3 otherVelocity, double combined, double share, double horizon,
                        double step, bool lowerId);

#endif // MAUL_NAV_SRC_ORCA3_H
