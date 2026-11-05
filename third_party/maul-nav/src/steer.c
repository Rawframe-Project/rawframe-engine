// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Steering along a corridor's corners: a desired velocity on
// the ground from the corners alone, after Detour's crowd.

#include "fpenv.h"

#include "maul-nav/base.h"
#include "maul-nav/query.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

MNAV_NODISCARD static bool InRange(float v, float most)
{
    return v >= 0.0f && v <= most;
}

MNAV_NODISCARD static bool Finite(mnavPos3 p)
{
    return isfinite(p.x) && isfinite(p.y) && isfinite(p.z);
}

// The length of (x, z), by sqrt, which is exactly rounded everywhere, not
// hypot, which is not and differs between C libraries.
static double Length(double x, double z)
{
    return sqrt(x * x + z * z);
}

// Whether a lies within 2^500 of the origin on the ground, where the
// difference from another such point and its square overflow no double.
MNAV_NODISCARD static bool Within(mnavPos3 a)
{
    return fabs(a.x) <= 0x1p500 && fabs(a.z) <= 0x1p500;
}

// For a pair of corners not both within 2^500: half of b - a, which no two
// finite corners overflow, scaled by a power of its own size so that its
// squares are neither past nor below a double's range; *scale receives
// how many times that is half the true difference, a power of two.
static void Far(mnavPos3 a, mnavPos3 b, double* x, double* z, double* scale)
{
    *x = b.x * 0.5 - a.x * 0.5;
    *z = b.z * 0.5 - a.z * 0.5;
    double m = fabs(*x) > fabs(*z) ? fabs(*x) : fabs(*z);
    *scale = 2.0;
    if (m > 0x1p500)
    {
        *x *= 0x1p-600;
        *z *= 0x1p-600;
        *scale = 0x1p601;
    }
}

// The ground distance from a to b: as it is between corners within 2^500,
// and otherwise from the scaled half difference, so that no square
// overflows or vanishes; inf when the distance is past a double's range.
static double Ground(mnavPos3 a, mnavPos3 b)
{
    if (Within(a) && Within(b))
    {
        return Length(b.x - a.x, b.z - a.z);
    }
    double x;
    double z;
    double scale;
    Far(a, b, &x, &z, &scale);
    return Length(x, z) * scale;
}

// The unit direction on the ground from a to b, or 0 for none.
static mnavPos3 Toward(mnavPos3 a, mnavPos3 b)
{
    double x = b.x - a.x;
    double z = b.z - a.z;
    double scale = 1.0;
    if (!Within(a) || !Within(b))
    {
        Far(a, b, &x, &z, &scale);
    }
    double length = Length(x, z);
    return length > 0.0 ? (mnavPos3){x / length, 0.0, z / length} : (mnavPos3){0.0, 0.0, 0.0};
}

// The direction to steer in, of length 1, or 0 when there is none:
// straight at corner n or, anticipating, toward it less the direction to
// the corner after it scaled by half the distance to it.
static mnavPos3 Direction(const mnavCorners* corners, int32_t n, bool anticipate)
{
    mnavPos3 p = corners->points[0];
    mnavPos3 next = corners->points[n];
    bool far = !Within(p) || !Within(next) ||
               (anticipate && n + 1 < corners->pointCount && !Within(corners->points[n + 1]));
    if (far)
    {
        // The same direction with the length to the next corner taken out:
        // toward it less half the direction to the corner after it.
        mnavPos3 u = Toward(p, next);
        bool none = u.x == 0.0 && u.z == 0.0;
        if (!none && anticipate && n + 1 < corners->pointCount &&
            Ground(p, corners->points[n + 1]) > 0.001)
        {
            mnavPos3 v = Toward(p, corners->points[n + 1]);
            u = Toward((mnavPos3){0.0, 0.0, 0.0},
                       (mnavPos3){u.x - v.x * 0.5, 0.0, u.z - v.z * 0.5});
        }
        return u;
    }
    double x = next.x - p.x;
    double z = next.z - p.z;
    if (anticipate && n + 1 < corners->pointCount)
    {
        mnavPos3 after = corners->points[n + 1];
        double length = Length(x, z);
        double ax = after.x - p.x;
        double az = after.z - p.z;
        double away = Length(ax, az);
        if (away > 0.001)
        {
            x -= ax / away * length * 0.5;
            z -= az / away * length * 0.5;
        }
    }
    double length = Length(x, z);
    return length > 0.0 ? (mnavPos3){x / length, 0.0, z / length} : (mnavPos3){0.0, 0.0, 0.0};
}

// The ground distance along the corners from the first to the last.
static double Left(const mnavCorners* corners)
{
    double left = 0.0;
    for (int32_t i = 1; i < corners->pointCount; ++i)
    {
        left += Ground(corners->points[i - 1], corners->points[i]);
    }
    return left;
}

static MNAV_NOINLINE mnavResult SteerWork(const mnavCorners* corners, const mnavSteerDef* def,
                                          mnavSteering* steeringOut)
{
    if (corners == nullptr || def == nullptr || steeringOut == nullptr || corners->pointCount < 1 ||
        corners->points == nullptr || corners->linkCount < 0 ||
        (corners->linkCount > 0 && corners->links == nullptr) ||
        def->cookie != MNAV_STEER_DEF_COOKIE)
    {
        return mnav_errorInvalid;
    }
    if (def->version != MNAV_ABI_VERSION)
    {
        return mnav_errorVersion;
    }
    if (!(def->maxSpeed >= 0.0f) || !(def->slowDistance >= 0.0f) || !(def->arriveDistance >= 0.0f))
    {
        return mnav_errorInvalid;
    }
    if (!InRange(def->maxSpeed, MNAV_MAX_STEER_SPEED) ||
        !InRange(def->slowDistance, MNAV_MAX_STEER_DISTANCE) ||
        !InRange(def->arriveDistance, MNAV_MAX_STEER_DISTANCE))
    {
        return mnav_errorLimit;
    }
    for (int32_t i = 0; i < corners->pointCount; ++i)
    {
        if (!Finite(corners->points[i]))
        {
            return mnav_errorInvalid;
        }
    }
    *steeringOut = (mnavSteering){{0.0f, 0.0f, 0.0f}, mnav_steerArrived, -1};
    double left = Left(corners);
    if (left <= (double)def->arriveDistance)
    {
        return mnav_success;
    }
    // At a link: its takeoff the next corner, and near.
    bool takeoff = corners->linkCount > 0 && corners->links[0].point == 1;
    if (takeoff && Ground(corners->points[0], corners->points[1]) <= (double)def->arriveDistance)
    {
        *steeringOut = (mnavSteering){{0.0f, 0.0f, 0.0f}, mnav_steerAtLink, 0};
        return mnav_success;
    }
    // The first corner away from the agent on the ground: one straight
    // above or below it, at a step, is passed.
    int32_t n = 1;
    while (n + 1 < corners->pointCount && Ground(corners->points[0], corners->points[n]) == 0.0)
    {
        n += 1;
    }
    // Toward a takeoff, straight: the link starts there.
    mnavPos3 direction = Direction(corners, n, def->enableTurnAnticipation && !takeoff);
    double speed = (double)def->maxSpeed;
    if (left < (double)def->slowDistance)
    {
        speed *= left / (double)def->slowDistance;
    }
    *steeringOut = (mnavSteering){
        {(float)(direction.x * speed), 0.0f, (float)(direction.z * speed)}, mnav_steerMoving, -1};
    return mnav_success;
}

mnavResult mnavSteer(const mnavCorners* corners, const mnavSteerDef* def, mnavSteering* steeringOut)
{
    uint64_t env = mnavFloatEnvEnter();
    mnavResult result = SteerWork(corners, def, steeringOut);
    mnavFloatEnvLeave(env);
    return result;
}
