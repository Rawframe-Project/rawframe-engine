// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Real spherical harmonics in AmbiX's form (spherical_harmonics.h), in
// closed form up to order 3.

#include "spherical_harmonics.h"

#include <math.h>

// The direction in the field's axes, as a unit vector; ahead for zero.
void maudFieldAxes(maudVector3 v, float* x, float* y, float* z)
{
    float length = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (length == 0.0f)
    {
        *x = 1.0f;
        *y = 0.0f;
        *z = 0.0f;
        return;
    }
    *x = -v.z / length;
    *y = -v.x / length;
    *z = v.y / length;
}

// The gains of a unit direction in the field's axes, up to order.
void maudSphericalHarmonics(uint32_t order, float x, float y, float z, float* g)
{
    const float r3 = 1.7320508f;  // sqrt(3)
    const float r15 = 3.8729833f; // sqrt(15)
    const float r58 = 0.7905694f; // sqrt(5 / 8)
    const float r38 = 0.6123724f; // sqrt(3 / 8)
    g[0] = 1.0f;
    g[1] = y;
    g[2] = z;
    g[3] = x;
    if (order < 2)
    {
        return;
    }
    g[4] = r3 * x * y;
    g[5] = r3 * y * z;
    g[6] = 0.5f * (3.0f * z * z - 1.0f);
    g[7] = r3 * x * z;
    g[8] = 0.5f * r3 * (x * x - y * y);
    if (order < 3)
    {
        return;
    }
    g[9] = r58 * y * (3.0f * x * x - y * y);
    g[10] = r15 * x * y * z;
    g[11] = r38 * y * (5.0f * z * z - 1.0f);
    g[12] = 0.5f * z * (5.0f * z * z - 3.0f);
    g[13] = r38 * x * (5.0f * z * z - 1.0f);
    g[14] = 0.5f * r15 * z * (x * x - y * y);
    g[15] = r58 * x * (x * x - 3.0f * y * y);
}
