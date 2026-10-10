// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Steering along a corridor's corners (mnav-0005): a desired velocity on
// the ground from the corners alone, after Detour's crowd.

#include "maul-nav/base.h"
#include "maul-nav/query.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

static bool InRange(float v, float most)
{
    return v >= 0.0f && v <= most;
}

static bool Finite(mnavPos3 p)
{
    return isfinite(p.x) && isfinite(p.y) && isfinite(p.z);
}

// The length of (x, z), by sqrt, which is exactly rounded everywhere, not
// hypot, which is not and differs between C libraries (mnav-0001).
static double Length(double x, double z)
{
    return sqrt(x * x + z * z);
}

// The ground distance from a to b.
static double Ground(mnavPos3 a, mnavPos3 b)
{
    return Length(b.x - a.x, b.z - a.z);
}

// The direction to steer in, of length 1, or 0 when there is none:
// straight at corner n or, anticipating, toward it less the direction to
// the corner after it scaled by half the distance to it.
static mnavPos3 Direction(const mnavCorners* corners, int32_t n, bool anticipate)
{
    mnavPos3 p = corners->points[0];
    mnavPos3 next = corners->points[n];
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

mnavResult mnavSteer(const mnavCorners* corners, const mnavSteerDef* def, mnavSteering* steeringOut)
{
    if (corners == nullptr || def == nullptr || steeringOut == nullptr || corners->pointCount < 1 ||
        corners->points == nullptr || (corners->linkCount > 0 && corners->links == nullptr) ||
        def->cookie != MNAV_STEER_DEF_COOKIE)
    {
        return mnav_errorInvalid;
    }
    if (def->version != MNAV_ABI_VERSION)
    {
        return mnav_errorVersion;
    }
    if (!InRange(def->maxSpeed, MNAV_MAX_STEER_SPEED) ||
        !InRange(def->slowDistance, MNAV_MAX_STEER_DISTANCE) ||
        !InRange(def->arriveDistance, MNAV_MAX_STEER_DISTANCE))
    {
        return mnav_errorRange;
    }
    for (int32_t i = 0; i < corners->pointCount; ++i)
    {
        if (!Finite(corners->points[i]))
        {
            return mnav_errorRange;
        }
    }
    *steeringOut = (mnavSteering){{0.0, 0.0, 0.0}, mnav_steerArrived, -1};
    double left = Left(corners);
    if (left <= (double)def->arriveDistance)
    {
        return mnav_success;
    }
    // At a link: its takeoff the next corner, and near.
    bool takeoff = corners->linkCount > 0 && corners->links[0].point == 1;
    if (takeoff && Ground(corners->points[0], corners->points[1]) <= (double)def->arriveDistance)
    {
        *steeringOut = (mnavSteering){{0.0, 0.0, 0.0}, mnav_steerAtLink, 0};
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
    mnavPos3 direction = Direction(corners, n, def->anticipateTurns && !takeoff);
    double speed = (double)def->maxSpeed;
    if (left < (double)def->slowDistance)
    {
        speed *= left / (double)def->slowDistance;
    }
    *steeringOut =
        (mnavSteering){{direction.x * speed, 0.0, direction.z * speed}, mnav_steerMoving, -1};
    return mnav_success;
}
