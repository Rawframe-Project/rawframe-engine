// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An avoidance set's memory, shared by the ground and the
// space calls, and the checks of their input's ranges.

#ifndef MAUL_NAV_SRC_AVOIDANCE_H
#define MAUL_NAV_SRC_AVOIDANCE_H

#include "allocator.h"
#include "crowd.h"
#include "host_code.h"
#include "invariant.h"
#include "obstacle.h"
#include "orca.h"
#include "orca3.h"
#include "outputs.h"

#include "maul-nav/avoidance.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// How far right of its preferred velocity a held-back agent aims, as a
// fraction of its speed.
#define MNAV_KEEP_RIGHT 0.01

struct mnavAvoidance
{
    mnavMemory memory;
    mnavHostCode host;
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

// A value one unit nearer zero; a zero stays.
static inline double mnavTowardZero(double v)
{
    uint64_t bits;
    memcpy(&bits, &v, sizeof bits);
    bits -= (bits << 1) != 0;
    memcpy(&v, &bits, sizeof v);
    return v;
}

// The power of two a velocity and its maximum speed are scaled by before
// their squares are taken: 2^600 when the largest of them is below
// 2^-500, so that the squares stay far from the subnormals, which round
// absolutely and would blur both the scaling back and a step toward zero.
// The scaling is then exact and cannot overflow.
static inline double mnavSpeedScale(double largest)
{
    return largest < 0x1p-500 ? 0x1p600 : 1.0;
}

static inline double mnavLarger(double a, double b)
{
    return a > b ? a : b;
}

// Whether a velocity no faster than about 2^-500 m/s, or one of any speed
// at a maximum of 2^-500 or more, is within the speed circle: its squared
// speed, scaled by mnavSpeedScale, is no more than the squared maximum.
MNAV_NODISCARD static inline bool mnavWithinSpeed2(mnavPos2 v, double maxSpeed)
{
    double k = mnavSpeedScale(maxSpeed);
    mnavPos2 s = mnavScale2(v, k);
    return mnavDot2(s, s) <= (maxSpeed * k) * (maxSpeed * k);
}

// The same for the speed sphere.
MNAV_NODISCARD static inline bool mnavWithinSpeed3(mnavPos3 v, double maxSpeed)
{
    double k = mnavSpeedScale(maxSpeed);
    mnavPos3 s = mnavScale3(v, k);
    return mnavDot3(s, s) <= (maxSpeed * k) * (maxSpeed * k);
}

// Whether a velocity just scaled back onto the speed circle is within a
// few units of it, which a few steps close: 2^-40 of the squared maximum,
// and two units of the least subnormal on the speed, scaled, where the
// components round absolutely. A velocity further out would take up to
// 2^52 steps.
MNAV_NODISCARD static inline bool mnavNearSpeed2(mnavPos2 v, double maxSpeed)
{
    double k = mnavSpeedScale(maxSpeed);
    mnavPos2 s = mnavScale2(v, k);
    double bound = (maxSpeed + 0x1p-1073) * k;
    return mnavDot2(s, s) <= bound * bound * (1.0 + 0x1p-40);
}

// The same for the speed sphere.
MNAV_NODISCARD static inline bool mnavNearSpeed3(mnavPos3 v, double maxSpeed)
{
    double k = mnavSpeedScale(maxSpeed);
    mnavPos3 s = mnavScale3(v, k);
    double bound = (maxSpeed + 0x1p-1073) * k;
    return mnavDot3(s, s) <= bound * bound * (1.0 + 0x1p-40);
}

// A velocity kept within the speed circle. One past it is scaled back
// onto it, and since that rounds, each component then steps one unit
// toward zero until it is within. A step takes at least a unit of the
// scaling's rounding off a nonzero component, and zero fits, so a few
// steps end it: three at most over 2.2e7 random velocities and maxima.
static inline mnavPos2 mnavKeepSpeed2(mnavPos2 v, double maxSpeed)
{
    double k = mnavSpeedScale(mnavLarger(maxSpeed, mnavLarger(fabs(v.x), fabs(v.y))));
    mnavPos2 s = mnavScale2(v, k);
    double speedSq = mnavDot2(s, s);
    if (speedSq <= (maxSpeed * k) * (maxSpeed * k))
    {
        return v;
    }
    v = mnavScale2(v, maxSpeed * k / sqrt(speedSq));
    MNAV_ASSERT(mnavNearSpeed2(v, maxSpeed));
    while (!mnavWithinSpeed2(v, maxSpeed))
    {
        v = (mnavPos2){mnavTowardZero(v.x), mnavTowardZero(v.y)};
    }
    return v;
}

// The same within the speed sphere.
static inline mnavPos3 mnavKeepSpeed3(mnavPos3 v, double maxSpeed)
{
    double k = mnavSpeedScale(
        mnavLarger(maxSpeed, mnavLarger(fabs(v.x), mnavLarger(fabs(v.y), fabs(v.z)))));
    mnavPos3 s = mnavScale3(v, k);
    double speedSq = mnavDot3(s, s);
    if (speedSq <= (maxSpeed * k) * (maxSpeed * k))
    {
        return v;
    }
    v = mnavScale3(v, maxSpeed * k / sqrt(speedSq));
    MNAV_ASSERT(mnavNearSpeed3(v, maxSpeed));
    while (!mnavWithinSpeed3(v, maxSpeed))
    {
        v = (mnavPos3){mnavTowardZero(v.x), mnavTowardZero(v.y), mnavTowardZero(v.z)};
    }
    return v;
}

MNAV_NODISCARD static inline bool mnavAvoidPositive(double v)
{
    return isfinite(v) && v > 0.0;
}

// Of two checks' statuses, the one the call returns: mnav_errorInvalid
// when either is, else mnav_errorLimit when either is, else success.
static inline mnavResult mnavAvoidJoin(mnavResult a, mnavResult b)
{
    return a == mnav_errorInvalid || b == mnav_success ? a : b;
}

// mnav_errorInvalid for a NaN, mnav_errorLimit for a value whose
// magnitude passes bound, an infinity included.
static inline mnavResult mnavAvoidBound(double v, double bound)
{
    if (isnan(v))
    {
        return mnav_errorInvalid;
    }
    return fabs(v) <= bound ? mnav_success : mnav_errorLimit;
}

// A time: mnav_errorLimit below MNAV_MIN_AVOIDANCE_TIME, minus infinity
// included; mnav_errorInvalid for a NaN or plus infinity, which no bound
// names.
static inline mnavResult mnavAvoidTime(double v)
{
    if (isnan(v))
    {
        return mnav_errorInvalid;
    }
    if (v < MNAV_MIN_AVOIDANCE_TIME)
    {
        return mnav_errorLimit;
    }
    return isfinite(v) ? mnav_success : mnav_errorInvalid;
}

static inline mnavResult mnavAvoidSpeed(double v)
{
    return mnavAvoidBound(v, MNAV_MAX_AVOIDANCE_SPEED);
}

static inline mnavResult mnavAvoidCoordinate(double v)
{
    return mnavAvoidBound(v, MNAV_MAX_AVOIDANCE_COORDINATE);
}

static inline mnavResult mnavAvoidRadius(double v)
{
    return v > 0.0 ? mnavAvoidBound(v, MNAV_MAX_AVOIDANCE_RADIUS) : mnav_errorInvalid;
}

// The binary32 next to f toward zero: one less in the magnitude's bits,
// the sign kept. f is finite and not zero.
static inline float mnavStepTowardZero(float f)
{
    uint32_t bits = 0;
    memcpy(&bits, &f, sizeof(bits));
    bits -= 1u;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

// A velocity the set gives back, rounded once to binary32, to nearest. v
// is within the maximum speed; when that rounding would put it past, each
// component it grew steps back toward zero, to at most its own magnitude in
// v, so the speed check that held for v holds for the result too.
static inline mnavVec2 mnavNarrow2(mnavPos2 v, double maxSpeed)
{
    mnavVec2 f = {(float)v.x, (float)v.y};
    mnavPos2 wide = mnavWide2(f);
    if (!mnavWithinSpeed2(wide, maxSpeed))
    {
        f.x = fabs(wide.x) > fabs(v.x) ? mnavStepTowardZero(f.x) : f.x;
        f.y = fabs(wide.y) > fabs(v.y) ? mnavStepTowardZero(f.y) : f.y;
    }
    MNAV_ASSERT(mnavWithinSpeed2(mnavWide2(f), maxSpeed));
    return f;
}

static inline mnavVec3 mnavNarrow3(mnavPos3 v, double maxSpeed)
{
    mnavVec3 f = {(float)v.x, (float)v.y, (float)v.z};
    mnavPos3 wide = mnavWide3(f);
    if (!mnavWithinSpeed3(wide, maxSpeed))
    {
        f.x = fabs(wide.x) > fabs(v.x) ? mnavStepTowardZero(f.x) : f.x;
        f.y = fabs(wide.y) > fabs(v.y) ? mnavStepTowardZero(f.y) : f.y;
        f.z = fabs(wide.z) > fabs(v.z) ? mnavStepTowardZero(f.z) : f.z;
    }
    MNAV_ASSERT(mnavWithinSpeed3(mnavWide3(f), maxSpeed));
    return f;
}

// Clears the velocities of a call that failed, when the set's agent limit
// holds them; a count past it, or no set, writes nothing, since the count
// is all that bounds the caller's array. A call refused as busy, which only
// the set's own allocator can make, leaves them: they may be the ones the
// call in progress fills.
static inline mnavResult mnavAvoidClear(mnavResult result, const mnavAvoidance* avoidance,
                                        int32_t agentCount, void* velocities, size_t size)
{
    if (result != mnav_success && result != mnav_errorBusy && avoidance != nullptr &&
        agentCount > 0 && agentCount <= avoidance->def.limits.agents)
    {
        mnavClearOutput(velocities, (size_t)agentCount * size);
    }
    return result;
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
