// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// ORCA's linear programs (mnav-0006): RVO2's linearProgram1 to 3 (RVO2
// src/Agent.cc, lines 63 to 268) in binary64. The only function taken from
// libm is sqrt, which is exactly rounded.

#include "orca.h"

#include "maul-nav/avoidance.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

// Narrows [left, right] along line n to where the lines before it allow;
// false when nothing is left.
static bool Bound(const mnavLine* lines, int32_t n, double* left, double* right)
{
    const mnavLine* line = &lines[n];
    for (int32_t i = 0; i < n; ++i)
    {
        double denominator = mnavDet2(line->direction, lines[i].direction);
        double numerator = mnavDet2(lines[i].direction, mnavSub2(line->point, lines[i].point));
        if (fabs(denominator) <= MNAV_ORCA_PARALLEL)
        {
            if (numerator < 0.0)
            {
                return false;
            }
            continue;
        }
        double t = numerator / denominator;
        if (denominator >= 0.0)
        {
            *right = t < *right ? t : *right;
        }
        else
        {
            *left = t > *left ? t : *left;
        }
        if (*left > *right)
        {
            return false;
        }
    }
    return true;
}

// The 1D program on line n, bounded by the lines before it and the
// circle: false when they leave nothing of it.
static bool Program1(const mnavLine* lines, int32_t n, double radius, mnavPos2 wanted,
                     bool directionOpt, mnavPos2* result)
{
    const mnavLine* line = &lines[n];
    double dot = mnavDot2(line->point, line->direction);
    double discriminant = dot * dot + radius * radius - mnavDot2(line->point, line->point);
    if (discriminant < 0.0)
    {
        return false;
    }
    double root = sqrt(discriminant);
    double left = -dot - root;
    double right = -dot + root;
    if (!Bound(lines, n, &left, &right))
    {
        return false;
    }
    double t = 0.0;
    if (directionOpt)
    {
        t = mnavDot2(wanted, line->direction) > 0.0 ? right : left;
    }
    else
    {
        t = mnavDot2(line->direction, mnavSub2(wanted, line->point));
        t = t < left ? left : (t > right ? right : t);
    }
    *result = mnavAdd2(line->point, mnavScale2(line->direction, t));
    return true;
}

int32_t mnavLinearProgram2(const mnavLine* lines, int32_t count, double radius, mnavPos2 wanted,
                           bool directionOpt, mnavPos2* result)
{
    if (directionOpt)
    {
        *result = mnavScale2(wanted, radius);
    }
    else if (mnavDot2(wanted, wanted) > radius * radius)
    {
        *result = mnavScale2(mnavNormalize2(wanted), radius);
    }
    else
    {
        *result = wanted;
    }
    for (int32_t i = 0; i < count; ++i)
    {
        if (mnavDet2(lines[i].direction, mnavSub2(lines[i].point, *result)) > 0.0)
        {
            mnavPos2 before = *result;
            if (!Program1(lines, i, radius, wanted, directionOpt, result))
            {
                *result = before;
                return i;
            }
        }
    }
    return count;
}

// The line where lines i and j are equally broken, along which the 3D
// program's projection runs; false when they point the same way.
static bool Bisector(const mnavLine* i, const mnavLine* j, mnavLine* out)
{
    double determinant = mnavDet2(i->direction, j->direction);
    if (fabs(determinant) <= MNAV_ORCA_PARALLEL)
    {
        if (mnavDot2(i->direction, j->direction) > 0.0)
        {
            return false;
        }
        out->point = mnavScale2(mnavAdd2(i->point, j->point), 0.5);
    }
    else
    {
        double t = mnavDet2(j->direction, mnavSub2(i->point, j->point)) / determinant;
        out->point = mnavAdd2(i->point, mnavScale2(i->direction, t));
    }
    out->direction = mnavNormalize2(mnavSub2(j->direction, i->direction));
    return true;
}

void mnavLinearProgram3(const mnavLine* lines, int32_t count, int32_t fixed, int32_t first,
                        double radius, mnavLine* scratch, mnavPos2* result)
{
    double distance = 0.0;
    for (int32_t i = first; i < count; ++i)
    {
        if (mnavDet2(lines[i].direction, mnavSub2(lines[i].point, *result)) <= distance)
        {
            continue;
        }
        int32_t projected = 0;
        for (int32_t k = 0; k < fixed; ++k)
        {
            scratch[projected++] = lines[k];
        }
        for (int32_t j = fixed; j < i; ++j)
        {
            projected += Bisector(&lines[i], &lines[j], &scratch[projected]) ? 1 : 0;
        }
        mnavPos2 before = *result;
        mnavPos2 across = {-lines[i].direction.y, lines[i].direction.x};
        if (mnavLinearProgram2(scratch, projected, radius, across, true, result) < projected)
        {
            // In principle impossible: the result already lies in this
            // program's region. Rounding can say otherwise; keep it then.
            *result = before;
        }
        distance = mnavDet2(lines[i].direction, mnavSub2(lines[i].point, *result));
    }
}

mnavLine mnavPairLine(mnavPos2 selfPosition, mnavPos2 selfVelocity, mnavPos2 otherPosition,
                      mnavPos2 otherVelocity, double combined, double share, double horizon,
                      double step, bool lowerId)
{
    mnavPos2 position = mnavSub2(otherPosition, selfPosition);
    mnavPos2 velocity = mnavSub2(selfVelocity, otherVelocity);
    double distance = mnavDot2(position, position);
    double combinedSq = combined * combined;
    mnavLine line;
    mnavPos2 u;
    if (distance > combinedSq)
    {
        double inverse = 1.0 / horizon;
        mnavPos2 w = mnavSub2(velocity, mnavScale2(position, inverse));
        double wSq = mnavDot2(w, w);
        double dot = mnavDot2(w, position);
        if (dot < 0.0 && dot * dot > combinedSq * wSq)
        {
            // Onto the cut-off circle.
            double length = sqrt(wSq);
            mnavPos2 unit = {w.x / length, w.y / length};
            line.direction = (mnavPos2){unit.y, -unit.x};
            u = mnavScale2(unit, combined * inverse - length);
        }
        else
        {
            // Onto a leg.
            double leg = sqrt(distance - combinedSq);
            if (mnavDet2(position, w) > 0.0)
            {
                line.direction = (mnavPos2){(position.x * leg - position.y * combined) / distance,
                                            (position.x * combined + position.y * leg) / distance};
            }
            else
            {
                line.direction =
                    (mnavPos2){-(position.x * leg + position.y * combined) / distance,
                               -(-position.x * combined + position.y * leg) / distance};
            }
            u = mnavSub2(mnavScale2(line.direction, mnavDot2(velocity, line.direction)), velocity);
        }
    }
    else
    {
        // Overlapping: part within the step.
        double inverse = 1.0 / step;
        mnavPos2 w = mnavSub2(velocity, mnavScale2(position, inverse));
        double length = sqrt(mnavDot2(w, w));
        // Same place, same velocity: the lower id goes one way and the
        // other the opposite.
        mnavPos2 unit = length > 0.0 ? (mnavPos2){w.x / length, w.y / length}
                                     : (mnavPos2){lowerId ? -1.0 : 1.0, 0.0};
        line.direction = (mnavPos2){unit.y, -unit.x};
        u = mnavScale2(unit, combined * inverse - length);
    }
    line.point = mnavAdd2(selfVelocity, mnavScale2(u, share));
    return line;
}
