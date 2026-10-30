// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A path's direction, by SVG 2's path directionality:
// the curve's direction within a segment, its limit where the derivative
// vanishes (the first non-zero of the second and third derivatives, the
// outgoing direction at a cusp), the later segment's at a boundary; a
// segment of no length takes the direction of the nearest one before it
// with a length, else after it, and a path of no length points along +x.

#include "elementary.h"
#include "fpenv.h"
#include "path_internal.h"
#include "path_length.h"
#include "valid.h"

#include "maul-tween/path.h"

#include <math.h>
#include <string.h>

// Whether a segment's cubic has no length to speak of: its derivatives
// within its values' rounding (coincident keyframes; a Catmull-Rom's
// tangents between them are rounding, as the sample holds them still).
static bool Still(const mtwnPath* path, const mtwnCubic* cubic)
{
    double extent = 0.0;
    double scale = 0.0;
    for (uint8_t c = 0; c < path->channels; ++c)
    {
        double e = fabs(cubic->b[c]) + fabs(cubic->c[c]) + fabs(cubic->d[c]);
        extent = e > extent ? e : extent;
        scale = fabs(cubic->a[c]) > scale ? fabs(cubic->a[c]) : scale;
    }
    return extent <= scale * 0x1p-52;
}

// The first of a segment's derivatives at u that is not 0, as a unit
// vector: false for a step or a segment of no length.
static bool Heading(const mtwnPath* path, const mtwnCubic* cubic, double u, double out[4])
{
    if (cubic->step || Still(path, cubic))
    {
        return false;
    }
    // The three derivatives in one pass, and the largest of each.
    double v[3][4] = {{0.0}};
    double largest[3] = {0.0, 0.0, 0.0};
    for (uint8_t c = 0; c < path->channels; ++c)
    {
        v[0][c] = cubic->b[c] + u * (2.0 * cubic->c[c] + u * 3.0 * cubic->d[c]);
        v[1][c] = 2.0 * cubic->c[c] + 6.0 * cubic->d[c] * u;
        v[2][c] = 6.0 * cubic->d[c];
        for (int k = 0; k < 3; ++k)
        {
            largest[k] = fabs(v[k][c]) > largest[k] ? fabs(v[k][c]) : largest[k];
        }
    }
    int k = largest[0] > 0.0 ? 0 : (largest[1] > 0.0 ? 1 : 2);
    if (!(largest[k] > 0.0 && isfinite(largest[k])))
    {
        return false;
    }
    // Scaled by the largest first, so the sum of squares neither overflows
    // nor underflows.
    double sum = 0.0;
    for (int c = 0; c < 4; ++c)
    {
        out[c] = v[k][c] / largest[k];
        sum += out[c] * out[c];
    }
    double norm = mtwnRootOfSquares(sum);
    for (int c = 0; c < 4; ++c)
    {
        out[c] /= norm;
    }
    return true;
}

static MTWN_NOINLINE void mtwnComputePathDirectionWork(const mtwnPath* path, double progress,
                                                       double directionOut[4])
{
    uint32_t i = 0;
    double u = 0.0;
    mtwnCubic cubic;
    mtwnLocatePath(path, progress, &i, &u, &cubic);
    if (Heading(path, &cubic, u, directionOut))
    {
        return;
    }
    // The nearest segment before with a length, at its end; else after,
    // at its start.
    for (uint32_t j = i; j-- > 0;)
    {
        mtwnPathSegmentCubic(path, j, &cubic);
        if (Heading(path, &cubic, 1.0, directionOut))
        {
            return;
        }
    }
    for (uint32_t j = i + 1; j + 1 < path->count; ++j)
    {
        mtwnPathSegmentCubic(path, j, &cubic);
        if (Heading(path, &cubic, 0.0, directionOut))
        {
            return;
        }
    }
    const double along[4] = {1.0, 0.0, 0.0, 0.0};
    memcpy(directionOut, along, sizeof along);
}

// Whether a direction or an angle may be taken: the path and the segment
// it reads as a maker gives them, the progress finite.
static bool Takes(const mtwnPath* path, double progress)
{
    return mtwnValidPath(path) && mtwnPathReadsValid(path, progress);
}

static MTWN_NOINLINE mtwnResult mtwnComputePathDirectionChecked(const mtwnPath* path,
                                                                double progress,
                                                                double directionOut[4])
{
    if (directionOut == NULL || !Takes(path, progress))
    {
        return mtwn_errorInvalid;
    }
    mtwnComputePathDirectionWork(path, progress, directionOut);
    return mtwn_success;
}

mtwnResult mtwnComputePathDirection(const mtwnPath* path, double progress, double directionOut[4])
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnComputePathDirectionChecked(path, progress, directionOut);
    mtwnFloatEnvLeave(env);
    return result;
}

static MTWN_NOINLINE mtwnResult mtwnComputePathAngleChecked(const mtwnPath* path, double progress,
                                                            double* angleOut)
{
    if (angleOut == NULL || !Takes(path, progress))
    {
        return mtwn_errorInvalid;
    }
    double direction[4];
    mtwnComputePathDirectionWork(path, progress, direction);
    *angleOut = mtwnAtan2(direction[1], direction[0]);
    return mtwn_success;
}

mtwnResult mtwnComputePathAngle(const mtwnPath* path, double progress, double* angleOut)
{
    uint64_t env = mtwnFloatEnvEnter();
    mtwnResult result = mtwnComputePathAngleChecked(path, progress, angleOut);
    mtwnFloatEnvLeave(env);
    return result;
}
