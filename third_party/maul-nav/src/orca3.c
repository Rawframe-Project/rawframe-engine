// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// ORCA's linear programs in space (mnav-0006): RVO2-3D's linearProgram1
// to 4 and its planes (RVO2-3D src/Agent.cc) in binary64. The only
// function taken from libm is sqrt, which is exactly rounded.

#include "orca3.h"

#include "maul-nav/base.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

// Planes whose normals' cross product, or a line and a plane whose
// direction and normal's dot product, are within this count as parallel,
// as lines do in 2D (MNAV_ORCA_PARALLEL).
#define PARALLEL 1e-9

// A line in space: the points point + t * direction, direction a unit
// vector.
typedef struct Line3
{
    mnavPos3 point;
    mnavPos3 direction;
} Line3;

// Narrows [left, right] along the line to where planes [0, n) allow;
// false when nothing is left.
static bool Bound(const mnavPlane* planes, int32_t n, const Line3* line, double* left,
                  double* right)
{
    for (int32_t i = 0; i < n; ++i)
    {
        double numerator = mnavDot3(mnavSub3(planes[i].point, line->point), planes[i].normal);
        double denominator = mnavDot3(line->direction, planes[i].normal);
        if (fabs(denominator) <= PARALLEL)
        {
            if (numerator > 0.0)
            {
                return false;
            }
            continue;
        }
        double t = numerator / denominator;
        if (denominator >= 0.0)
        {
            *left = t > *left ? t : *left;
        }
        else
        {
            *right = t < *right ? t : *right;
        }
        if (*left > *right)
        {
            return false;
        }
    }
    return true;
}

// The 1D program on a line, bounded by planes [0, n) and the sphere:
// false when they leave nothing of it.
static bool Program1(const mnavPlane* planes, int32_t n, const Line3* line, double radius,
                     mnavPos3 wanted, bool directionOpt, mnavPos3* result)
{
    double dot = mnavDot3(line->point, line->direction);
    double discriminant = dot * dot + radius * radius - mnavDot3(line->point, line->point);
    if (discriminant < 0.0)
    {
        return false;
    }
    double root = sqrt(discriminant);
    double left = -dot - root;
    double right = -dot + root;
    if (!Bound(planes, n, line, &left, &right))
    {
        return false;
    }
    double t = 0.0;
    if (directionOpt)
    {
        t = mnavDot3(wanted, line->direction) > 0.0 ? right : left;
    }
    else
    {
        t = mnavDot3(line->direction, mnavSub3(wanted, line->point));
        t = t < left ? left : (t > right ? right : t);
    }
    *result = mnavAdd3(line->point, mnavScale3(line->direction, t));
    return true;
}

// The best point of plane n's disc within the sphere, before the planes
// before it are kept: false when the plane misses the sphere.
static bool OnPlane(const mnavPlane* plane, double radius, mnavPos3 wanted, bool directionOpt,
                    mnavPos3* result)
{
    double distance = mnavDot3(plane->point, plane->normal);
    double distanceSq = distance * distance;
    double radiusSq = radius * radius;
    if (distanceSq > radiusSq)
    {
        return false;
    }
    double discSq = radiusSq - distanceSq;
    mnavPos3 center = mnavScale3(plane->normal, distance);
    if (directionOpt)
    {
        mnavPos3 along =
            mnavSub3(wanted, mnavScale3(plane->normal, mnavDot3(wanted, plane->normal)));
        double alongSq = mnavDot3(along, along);
        *result = alongSq <= PARALLEL * PARALLEL
                      ? center
                      : mnavAdd3(center, mnavScale3(along, sqrt(discSq / alongSq)));
        return true;
    }
    mnavPos3 projected = mnavAdd3(
        wanted, mnavScale3(plane->normal, mnavDot3(mnavSub3(plane->point, wanted), plane->normal)));
    if (mnavDot3(projected, projected) > radiusSq)
    {
        // Onto the disc's rim. A plane touching the sphere leaves a disc
        // of no size, where rounding can put the projection a hair
        // outside it and on its center: the center is then the rim.
        mnavPos3 out = mnavNormalize3(mnavSub3(projected, center));
        *result = mnavAdd3(center, mnavScale3(out, sqrt(discSq)));
    }
    else
    {
        *result = projected;
    }
    return true;
}

// The 2D program on plane n, bounded by planes [0, n) and the sphere:
// false when they leave nothing of it.
static bool Program2(const mnavPlane* planes, int32_t n, double radius, mnavPos3 wanted,
                     bool directionOpt, mnavPos3* result)
{
    const mnavPlane* plane = &planes[n];
    if (!OnPlane(plane, radius, wanted, directionOpt, result))
    {
        return false;
    }
    for (int32_t i = 0; i < n; ++i)
    {
        if (mnavDot3(planes[i].normal, mnavSub3(planes[i].point, *result)) <= 0.0)
        {
            continue;
        }
        mnavPos3 cross = mnavCross3(planes[i].normal, plane->normal);
        if (mnavDot3(cross, cross) <= PARALLEL * PARALLEL)
        {
            // Parallel, and plane i leaves nothing of plane n.
            return false;
        }
        Line3 line;
        line.direction = mnavNormalize3(cross);
        mnavPos3 across = mnavCross3(line.direction, plane->normal);
        double t = mnavDot3(mnavSub3(planes[i].point, plane->point), planes[i].normal) /
                   mnavDot3(across, planes[i].normal);
        line.point = mnavAdd3(plane->point, mnavScale3(across, t));
        if (!Program1(planes, i, &line, radius, wanted, directionOpt, result))
        {
            return false;
        }
    }
    return true;
}

int32_t mnavPlaneProgram3(const mnavPlane* planes, int32_t count, double radius, mnavPos3 wanted,
                          bool directionOpt, mnavPos3* result)
{
    if (directionOpt)
    {
        *result = mnavScale3(wanted, radius);
    }
    else if (mnavDot3(wanted, wanted) > radius * radius)
    {
        *result = mnavScale3(mnavNormalize3(wanted), radius);
    }
    else
    {
        *result = wanted;
    }
    for (int32_t i = 0; i < count; ++i)
    {
        if (mnavDot3(planes[i].normal, mnavSub3(planes[i].point, *result)) > 0.0)
        {
            mnavPos3 before = *result;
            if (!Program2(planes, i, radius, wanted, directionOpt, result))
            {
                *result = before;
                return i;
            }
        }
    }
    return count;
}

// The plane where planes i and j are equally broken, through which the
// 4D program's projection runs; false when they face the same way.
static bool Bisector(const mnavPlane* i, const mnavPlane* j, mnavPlane* out)
{
    mnavPos3 cross = mnavCross3(j->normal, i->normal);
    if (mnavDot3(cross, cross) <= PARALLEL * PARALLEL)
    {
        if (mnavDot3(i->normal, j->normal) > 0.0)
        {
            return false;
        }
        out->point = mnavScale3(mnavAdd3(i->point, j->point), 0.5);
    }
    else
    {
        mnavPos3 across = mnavCross3(cross, i->normal);
        double t = mnavDot3(mnavSub3(j->point, i->point), j->normal) / mnavDot3(across, j->normal);
        out->point = mnavAdd3(i->point, mnavScale3(across, t));
    }
    out->normal = mnavNormalize3(mnavSub3(j->normal, i->normal));
    return true;
}

void mnavPlaneProgram4(const mnavPlane* planes, int32_t count, int32_t fixed, int32_t first,
                       double radius, mnavPlane* scratch, mnavPos3* result)
{
    double distance = 0.0;
    for (int32_t i = first; i < count; ++i)
    {
        if (mnavDot3(planes[i].normal, mnavSub3(planes[i].point, *result)) <= distance)
        {
            continue;
        }
        int32_t projected = 0;
        for (int32_t k = 0; k < fixed; ++k)
        {
            scratch[projected++] = planes[k];
        }
        for (int32_t j = fixed; j < i; ++j)
        {
            projected += Bisector(&planes[i], &planes[j], &scratch[projected]) ? 1 : 0;
        }
        mnavPos3 before = *result;
        if (mnavPlaneProgram3(scratch, projected, radius, planes[i].normal, true, result) <
            projected)
        {
            // In principle impossible: the result already lies in this
            // program's region. Rounding can say otherwise; keep it then.
            *result = before;
        }
        distance = mnavDot3(planes[i].normal, mnavSub3(planes[i].point, *result));
    }
}

// A unit vector square to a, which is not zero: across a and world up,
// or a and world +X when a is vertical. Turned with a, so that the other
// of a pair, seeing -a, gets the opposite.
static mnavPos3 Square(mnavPos3 a)
{
    mnavPos3 side = mnavCross3(a, (mnavPos3){0.0, 1.0, 0.0});
    if (mnavDot3(side, side) == 0.0)
    {
        side = mnavCross3(a, (mnavPos3){1.0, 0.0, 0.0});
    }
    return mnavNormalize3(side);
}

mnavPlane mnavPairPlane(mnavPos3 selfPosition, mnavPos3 selfVelocity, mnavPos3 otherPosition,
                        mnavPos3 otherVelocity, double combined, double share, double horizon,
                        double step, bool lowerId)
{
    mnavPos3 position = mnavSub3(otherPosition, selfPosition);
    mnavPos3 velocity = mnavSub3(selfVelocity, otherVelocity);
    double distance = mnavDot3(position, position);
    double combinedSq = combined * combined;
    mnavPlane plane;
    mnavPos3 u;
    if (distance > combinedSq)
    {
        double inverse = 1.0 / horizon;
        mnavPos3 w = mnavSub3(velocity, mnavScale3(position, inverse));
        double wSq = mnavDot3(w, w);
        double dot = mnavDot3(w, position);
        if (dot < 0.0 && dot * dot > combinedSq * wSq)
        {
            // Onto the cut-off sphere.
            double length = sqrt(wSq);
            plane.normal = mnavScale3(w, 1.0 / length);
            u = mnavScale3(plane.normal, combined * inverse - length);
        }
        else
        {
            // Onto the cone, along the sphere of the cone's that the
            // relative velocity is nearest the side of.
            mnavPos3 cross = mnavCross3(position, velocity);
            double b = mnavDot3(position, velocity);
            double c =
                mnavDot3(velocity, velocity) - mnavDot3(cross, cross) / (distance - combinedSq);
            double discriminant = b * b - distance * c;
            double t = (b + sqrt(discriminant > 0.0 ? discriminant : 0.0)) / distance;
            mnavPos3 ww = mnavSub3(velocity, mnavScale3(position, t));
            double length = sqrt(mnavDot3(ww, ww));
            // Heading straight at the other: every side is as near; one
            // square to the line between them, the other's the opposite.
            plane.normal = length > 0.0 ? mnavScale3(ww, 1.0 / length) : Square(position);
            u = mnavScale3(plane.normal, combined * t - length);
        }
    }
    else
    {
        // Overlapping: part within the step.
        double inverse = 1.0 / step;
        mnavPos3 w = mnavSub3(velocity, mnavScale3(position, inverse));
        double length = sqrt(mnavDot3(w, w));
        // Bound for the other's center within the step: away from it, or
        // on the same spot with the same velocity, the lower id one way
        // along X and the other the opposite.
        if (length > 0.0)
        {
            plane.normal = mnavScale3(w, 1.0 / length);
        }
        else if (distance > 0.0)
        {
            plane.normal = mnavNormalize3(mnavScale3(position, -1.0));
        }
        else
        {
            plane.normal = (mnavPos3){lowerId ? -1.0 : 1.0, 0.0, 0.0};
        }
        u = mnavScale3(plane.normal, combined * inverse - length);
    }
    plane.point = mnavAdd3(selfVelocity, mnavScale3(u, share));
    return plane;
}
