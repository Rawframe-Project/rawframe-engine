// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The library's own scalar functions.

#include "scalar.h"

#include <math.h>

// Degrees to radians, rounded once to binary32.
static const float s_radiansPerDegree = 0.017453292519943295f;

float mnavCosDegrees(float degrees)
{
    // Below 45 degrees the cosine's Taylor series converges within binary32
    // precision by its x^10 term. Above, cos(d) = sin(90 - d), where the
    // subtraction is exact (both values lie within a factor of two), so the
    // small result keeps its relative precision near 90 degrees. Every
    // operation is a single rounding with no contraction.
    if (degrees <= 45.0f)
    {
        float x = degrees * s_radiansPerDegree;
        float x2 = x * x;
        float p = -1.0f / 3628800.0f;
        p = p * x2 + 1.0f / 40320.0f;
        p = p * x2 - 1.0f / 720.0f;
        p = p * x2 + 1.0f / 24.0f;
        p = p * x2 - 0.5f;
        return p * x2 + 1.0f;
    }
    float x = (90.0f - degrees) * s_radiansPerDegree;
    float x2 = x * x;
    float p = 1.0f / 362880.0f;
    p = p * x2 - 1.0f / 5040.0f;
    p = p * x2 + 1.0f / 120.0f;
    p = p * x2 - 1.0f / 6.0f;
    return (p * x2) * x + x;
}

bool mnavToCells(float meters, float cell, bool roundUp, int32_t max, int32_t* cellsOut)
{
    float quotient = meters / cell;
    if (quotient > (float)max + 0.5f)
    {
        return false;
    }
    // Within max + 0.5 the nearest integer is exact in binary32 (max is
    // far below 2^23), and so is its distance from the quotient.
    float nearest = floorf(quotient + 0.5f);
    float cells;
    if (fabsf(quotient - nearest) <= 0x1p-10f)
    {
        cells = nearest;
    }
    else
    {
        cells = roundUp ? ceilf(quotient) : floorf(quotient);
    }
    if (cells > (float)max)
    {
        return false;
    }
    *cellsOut = (int32_t)cells;
    return true;
}
