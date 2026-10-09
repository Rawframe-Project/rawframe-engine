// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Occlusion rays (occlusion.h). Rays are built in double from world
// positions and handed over as unit directions with their lengths; a ray
// of no length (a listener at the source) points ahead and has length 0,
// which no hit can fall within.

#include "occlusion.h"

#include <math.h>

#define PI_D 3.14159265358979323846

void maudBallPoints(uint32_t count, maudVector3* points)
{
    // Roberts' R3: phi is the real root of x^4 = x + 1.
    const double phi = 1.2207440846057596;
    const double alpha[3] = {1.0 / phi, 1.0 / (phi * phi), 1.0 / (phi * phi * phi)};
    for (uint32_t i = 0; i < count; ++i)
    {
        double u[3];
        for (int d = 0; d < 3; ++d)
        {
            double v = 0.5 + alpha[d] * (double)(i + 1);
            u[d] = v - floor(v);
        }
        double r = cbrt(u[0]);
        double z = 1.0 - 2.0 * u[1];
        double a = 2.0 * PI_D * u[2];
        double s = sqrt(1.0 - z * z);
        points[i] = (maudVector3){(float)(r * s * cos(a)), (float)(r * s * sin(a)), (float)(r * z)};
    }
}

uint32_t maudOcclusionRayCount(maudOcclusionMethod method, uint32_t samples)
{
    if (method == maud_occlusionRay)
    {
        return 1;
    }
    return method == maud_occlusionVolumetric ? 2 * samples : 0;
}

static maudRay Between(const double* from, const double* to)
{
    double d[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
    double length = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    maudRay ray = {
        {(float)from[0], (float)from[1], (float)from[2]}, {0.0f, 0.0f, -1.0f}, 0.0f, (float)length};
    if (length > 0.0)
    {
        ray.direction =
            (maudVector3){(float)(d[0] / length), (float)(d[1] / length), (float)(d[2] / length)};
    }
    return ray;
}

void maudOcclusionRays(const maudPose* listener, const maudPose* source, maudOcclusionMethod method,
                       float radius, uint32_t samples, const maudVector3* points, maudRay* rays)
{
    double l[3] = {(double)listener->position.x, (double)listener->position.y,
                   (double)listener->position.z};
    double s[3] = {(double)source->position.x, (double)source->position.y,
                   (double)source->position.z};
    if (method == maud_occlusionRay)
    {
        rays[0] = Between(l, s);
        return;
    }
    for (uint32_t i = 0; i < samples; ++i)
    {
        double p[3] = {s[0] + (double)radius * (double)points[i].x,
                       s[1] + (double)radius * (double)points[i].y,
                       s[2] + (double)radius * (double)points[i].z};
        rays[2 * i] = Between(s, p);
        rays[2 * i + 1] = Between(l, p);
    }
}

float maudOcclusionOf(maudOcclusionMethod method, uint32_t samples, const uint8_t* occluded)
{
    if (method == maud_occlusionRay)
    {
        return occluded[0] != 0 ? 1.0f : 0.0f;
    }
    uint32_t valid = 0;
    uint32_t hidden = 0;
    for (uint32_t i = 0; i < samples; ++i)
    {
        if (occluded[2 * i] != 0)
        {
            continue;
        }
        valid += 1;
        hidden += occluded[2 * i + 1] != 0 ? 1u : 0u;
    }
    // A source that sees none of its sphere is buried: fully occluded.
    return valid > 0 ? (float)hidden / (float)valid : 1.0f;
}
