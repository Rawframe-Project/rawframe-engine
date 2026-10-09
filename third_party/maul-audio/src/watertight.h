// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Watertight ray-triangle intersection (Woop, Benthin and Wald, JCGT
// 2013): the ray is sheared so that it runs along +z from the origin;
// a triangle's edge functions in that frame decide the hit, with a
// double-precision recheck when one is exactly zero, so that a ray
// through a shared edge or vertex hits one of the triangles there.

#ifndef MAUL_AUDIO_SRC_WATERTIGHT_H
#define MAUL_AUDIO_SRC_WATERTIGHT_H

#include "maul-audio/base.h"

#include <stdint.h>

// A ray prepared for the test.
typedef struct maudShearedRay
{
    float origin[3];
    // The axes: kz the direction's largest, kx and ky the others.
    int kx;
    int ky;
    int kz;
    float sx;
    float sy;
    float sz;
} maudShearedRay;

void maudShearRay(const float* origin, const float* direction, maudShearedRay* ray);

// Whether the ray meets the triangle a, b, c at a distance within [tMin,
// tMax]; if so, *t receives it.
bool maudHitTriangle(const maudShearedRay* ray, const float* a, const float* b, const float* c,
                     float tMin, float tMax, float* t);

#endif // MAUL_AUDIO_SRC_WATERTIGHT_H
