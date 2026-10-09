// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// ORCA's linear programs (mnav-0006), after RVO2's linearProgram1 to 3 and
// in binary64: the velocity nearest a wanted one within a speed that
// keeps to the left of every line, or that breaks the lines least.

#ifndef MAUL_NAV_SRC_ORCA_H
#define MAUL_NAV_SRC_ORCA_H

#include "maul-nav/avoidance.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

// Vector helpers on the ground plane.
static inline mnavPos2 mnavAdd2(mnavPos2 a, mnavPos2 b)
{
    return (mnavPos2){a.x + b.x, a.y + b.y};
}

static inline mnavPos2 mnavSub2(mnavPos2 a, mnavPos2 b)
{
    return (mnavPos2){a.x - b.x, a.y - b.y};
}

static inline mnavPos2 mnavScale2(mnavPos2 a, double s)
{
    return (mnavPos2){a.x * s, a.y * s};
}

static inline double mnavDot2(mnavPos2 a, mnavPos2 b)
{
    return a.x * b.x + a.y * b.y;
}

static inline double mnavDet2(mnavPos2 a, mnavPos2 b)
{
    return a.x * b.y - a.y * b.x;
}

// The unit vector along a, or {0, 0} when a has no length. A vector whose
// squared length lies near or below the smallest normal number, where it
// loses precision or rounds to 0, is first scaled up by 2^600, which is
// exact; longer ones are not touched.
static inline mnavPos2 mnavNormalize2(mnavPos2 a)
{
    double d = mnavDot2(a, a);
    if (d < 0x1p-1000)
    {
        a = mnavScale2(a, 0x1p600);
        d = mnavDot2(a, a);
        if (d == 0.0)
        {
            return (mnavPos2){0.0, 0.0};
        }
    }
    return mnavScale2(a, 1.0 / sqrt(d));
}

// Lines whose directions' cross product is within this count as parallel.
#define MNAV_ORCA_PARALLEL 1e-9

// A half-plane of allowed velocities: those on the left of the line
// through point along direction, a unit vector.
typedef struct mnavLine
{
    mnavPos2 point;
    mnavPos2 direction;
} mnavLine;

// The 2D program: the velocity within radius on the left of every line
// nearest wanted, or with directionOpt the farthest along wanted, a unit
// vector. Returns the count when it holds, else the first line it could
// not keep, result then being the best before it.
int32_t mnavLinearProgram2(const mnavLine* lines, int32_t count, double radius, mnavPos2 wanted,
                           bool directionOpt, mnavPos2* result);

// The 3D program, after the 2D one failed at line first: keeps the first
// fixed lines and finds the velocity that breaks the others least. Needs
// scratch room for count lines.
void mnavLinearProgram3(const mnavLine* lines, int32_t count, int32_t fixed, int32_t first,
                        double radius, mnavLine* scratch, mnavPos2* result);

// The ORCA line of a disc against another, combined being their radii's
// sum: the velocities of the first that, if the other takes the rest,
// avoid colliding within horizon, or for discs already overlapping, part
// within step. share is the first's part of the avoidance, 1 against an
// obstacle; lowerId says which way it goes from a disc on the same spot
// with the same velocity.
mnavLine mnavPairLine(mnavPos2 selfPosition, mnavPos2 selfVelocity, mnavPos2 otherPosition,
                      mnavPos2 otherVelocity, double combined, double share, double horizon,
                      double step, bool lowerId);

#endif // MAUL_NAV_SRC_ORCA_H
