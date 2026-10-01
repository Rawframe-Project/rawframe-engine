// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A damped spring pulling a value to its target, solved in closed form
// from a start offset and velocity, as Flutter's SpringSimulation, with
// the library's own exponential and trigonometry. One shape serves every
// channel of a value; each channel has its own start.

#ifndef MAUL_UI_SRC_SPRING_H
#define MAUL_UI_SRC_SPRING_H

// The spring's motion: its offset from the target at time t is
// c1 e^(r1 t) + c2 e^(r2 t) for an overdamped spring, e^(r1 t) (c1 + c2 t)
// for a critical one, and e^(r1 t) (c1 cos(w t) + c2 sin(w t)) for an
// underdamped one.
typedef struct muiSpringShape
{
    double r1;
    double r2;
    double w;
    // 0 overdamped, 1 critical, 2 underdamped.
    int kind;
} muiSpringShape;

// The constants c1 and c2 of one start.
typedef struct muiSpringStart
{
    double c1;
    double c2;
} muiSpringStart;

// The terms of every start's motion at one time.
typedef struct muiSpringTime
{
    double seconds;
    double decay;
    double second;
    double sine;
    double cosine;
} muiSpringTime;

// A spring of frequency hertz and damping ratio above 0.
muiSpringShape muiMakeSpringShape(double frequency, double dampingRatio);

// A start offset from the target with velocity, both in value units (per
// second).
muiSpringStart muiStartSpring(const muiSpringShape* shape, double offset, double velocity);

// The terms at seconds after the start.
muiSpringTime muiSpringTimeAt(const muiSpringShape* shape, double seconds);

// The offset from the target and the velocity of a start at a time.
void muiSpringAt(const muiSpringShape* shape, const muiSpringTime* time, muiSpringStart start,
                 double* offsetOut, double* velocityOut);

#endif // MAUL_UI_SRC_SPRING_H
