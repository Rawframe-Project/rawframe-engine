// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Flattening by Wang's formula: a curve of degree d whose control
// points' second differences are at most m long is within tolerance of n
// segments when n is at least the square root of d (d - 1) m / (8
// tolerance).

#include "flatten.h"

#include FT_OUTLINE_H

#include <math.h>

enum
{
    // The most segments a curve becomes.
    MAX_PIECES = 256
};

typedef struct Walk
{
    muiSegment* segments;
    uint32_t capacity;
    uint32_t count;
    float x;
    float y;
} Walk;

static float Pixels(FT_Pos value)
{
    return (float)value / 64.0f;
}

static void Add(Walk* walk, float x, float y)
{
    if (walk->count < walk->capacity)
    {
        walk->segments[walk->count] = (muiSegment){walk->x, walk->y, x, y};
    }
    walk->count++;
    walk->x = x;
    walk->y = y;
}

static uint32_t Pieces(float factor, float dx, float dy)
{
    float n = ceilf(sqrtf(factor * sqrtf(dx * dx + dy * dy) / MUI_FLATTEN_TOLERANCE));
    return n<1.0f ? 1u : n>(float) MAX_PIECES ? MAX_PIECES : (uint32_t)n;
}

static int MoveTo(const FT_Vector* to, void* user)
{
    Walk* walk = user;
    walk->x = Pixels(to->x);
    walk->y = Pixels(to->y);
    return 0;
}

static int LineTo(const FT_Vector* to, void* user)
{
    Add(user, Pixels(to->x), Pixels(to->y));
    return 0;
}

static int ConicTo(const FT_Vector* control, const FT_Vector* to, void* user)
{
    Walk* walk = user;
    float x0 = walk->x;
    float y0 = walk->y;
    float x1 = Pixels(control->x);
    float y1 = Pixels(control->y);
    float x2 = Pixels(to->x);
    float y2 = Pixels(to->y);
    // d (d - 1) / 8 is 1/4 for a quadratic.
    uint32_t n = Pieces(0.25f, x0 - 2.0f * x1 + x2, y0 - 2.0f * y1 + y2);
    for (uint32_t i = 1; i <= n; i++)
    {
        float t = (float)i / (float)n;
        float u = 1.0f - t;
        Add(walk, u * u * x0 + 2.0f * u * t * x1 + t * t * x2,
            u * u * y0 + 2.0f * u * t * y1 + t * t * y2);
    }
    return 0;
}

static int CubicTo(const FT_Vector* control1, const FT_Vector* control2, const FT_Vector* to,
                   void* user)
{
    Walk* walk = user;
    float x0 = walk->x;
    float y0 = walk->y;
    float x1 = Pixels(control1->x);
    float y1 = Pixels(control1->y);
    float x2 = Pixels(control2->x);
    float y2 = Pixels(control2->y);
    float x3 = Pixels(to->x);
    float y3 = Pixels(to->y);
    float ax = x0 - 2.0f * x1 + x2;
    float ay = y0 - 2.0f * y1 + y2;
    float bx = x1 - 2.0f * x2 + x3;
    float by = y1 - 2.0f * y2 + y3;
    bool first = ax * ax + ay * ay >= bx * bx + by * by;
    // d (d - 1) / 8 is 3/4 for a cubic.
    uint32_t n = Pieces(0.75f, first ? ax : bx, first ? ay : by);
    for (uint32_t i = 1; i <= n; i++)
    {
        float t = (float)i / (float)n;
        float u = 1.0f - t;
        float a = u * u * u;
        float b = 3.0f * u * u * t;
        float c = 3.0f * u * t * t;
        float d = t * t * t;
        Add(walk, a * x0 + b * x1 + c * x2 + d * x3, a * y0 + b * y1 + c * y2 + d * y3);
    }
    return 0;
}

bool muiFlattenOutline(const FT_Outline* outline, muiSegment* segments, uint32_t capacity,
                       uint32_t* countOut)
{
    static const FT_Outline_Funcs funcs = {MoveTo, LineTo, ConicTo, CubicTo, 0, 0};
    Walk walk = {segments, capacity, 0, 0.0f, 0.0f};
    FT_Error error = FT_Outline_Decompose((FT_Outline*)outline, &funcs, &walk);
    *countOut = walk.count;
    return error == 0;
}
