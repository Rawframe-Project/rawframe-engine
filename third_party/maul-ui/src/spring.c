// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// x'' + 2 z w0 x' + w0^2 x = 0 with w0 = 2 pi f. The three cases of the
// damping ratio z have the solutions below; their constants come from
// x(0) = offset and x'(0) = velocity.

#include "spring.h"

#include "motion_math.h"

#include <math.h>

#define TWO_PI 6.28318530717958647692

enum
{
    kindOverdamped = 0,
    kindCritical = 1,
    kindUnderdamped = 2,
};

muiSpringShape muiMakeSpringShape(double frequency, double dampingRatio)
{
    double w0 = TWO_PI * frequency;
    muiSpringShape shape = {0};
    if (dampingRatio < 1.0)
    {
        shape.kind = kindUnderdamped;
        shape.r1 = -dampingRatio * w0;
        shape.w = w0 * sqrt(1.0 - dampingRatio * dampingRatio);
    }
    else if (dampingRatio == 1.0)
    {
        shape.kind = kindCritical;
        shape.r1 = -w0;
    }
    else
    {
        double root = sqrt(dampingRatio * dampingRatio - 1.0);
        shape.kind = kindOverdamped;
        shape.r1 = -w0 * (dampingRatio - root);
        shape.r2 = -w0 * (dampingRatio + root);
    }
    return shape;
}

muiSpringStart muiStartSpring(const muiSpringShape* shape, double offset, double velocity)
{
    switch (shape->kind)
    {
    case kindUnderdamped:
        return (muiSpringStart){offset, (velocity - shape->r1 * offset) / shape->w};
    case kindCritical:
        return (muiSpringStart){offset, velocity - shape->r1 * offset};
    default:
    {
        double c2 = (velocity - shape->r1 * offset) / (shape->r2 - shape->r1);
        return (muiSpringStart){offset - c2, c2};
    }
    }
}

muiSpringTime muiSpringTimeAt(const muiSpringShape* shape, double seconds)
{
    muiSpringTime time = {.seconds = seconds, .decay = muiExp(shape->r1 * seconds)};
    if (shape->kind == kindUnderdamped)
    {
        muiSinCos(shape->w * seconds, &time.sine, &time.cosine);
    }
    else if (shape->kind == kindOverdamped)
    {
        time.second = muiExp(shape->r2 * seconds);
    }
    return time;
}

void muiSpringAt(const muiSpringShape* shape, const muiSpringTime* time, muiSpringStart start,
                 double* offsetOut, double* velocityOut)
{
    double decay = time->decay;
    switch (shape->kind)
    {
    case kindUnderdamped:
    {
        double wave = start.c1 * time->cosine + start.c2 * time->sine;
        *offsetOut = decay * wave;
        *velocityOut = decay * (shape->r1 * wave +
                                shape->w * (start.c2 * time->cosine - start.c1 * time->sine));
        break;
    }
    case kindCritical:
    {
        double linear = start.c1 + start.c2 * time->seconds;
        *offsetOut = decay * linear;
        *velocityOut = decay * (shape->r1 * linear + start.c2);
        break;
    }
    default:
        *offsetOut = start.c1 * decay + start.c2 * time->second;
        *velocityOut = start.c1 * shape->r1 * decay + start.c2 * shape->r2 * time->second;
        break;
    }
}
