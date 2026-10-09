// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Transmission walks (transmission.h). The next query starts a
// millimetre past the surface just crossed, enough to clear it at any
// distance a float position holds to that precision (a kilometre).

#include "transmission.h"

#include <math.h>

#define PAST 1e-3f

maudRay maudPathRay(const maudPose* listener, const maudPose* source, float minDistance)
{
    double d[3] = {(double)source->position.x - (double)listener->position.x,
                   (double)source->position.y - (double)listener->position.y,
                   (double)source->position.z - (double)listener->position.z};
    double length = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    maudRay ray = {listener->position, {0.0f, 0.0f, -1.0f}, minDistance, (float)length};
    if (length > 0.0)
    {
        ray.direction =
            (maudVector3){(float)(d[0] / length), (float)(d[1] / length), (float)(d[2] / length)};
    }
    return ray;
}

bool maudCrossSurface(const maudRayHit* hit, const maudRay* ray,
                      const maudAcousticMaterial* materials, uint32_t materialCount,
                      float* transmission, float* nextMin)
{
    if (!(hit->distance >= ray->minDistance) || !(hit->distance <= ray->maxDistance))
    {
        return false;
    }
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        transmission[b] *=
            hit->material < materialCount ? materials[hit->material].transmission[b] : 0.0f;
    }
    *nextMin = hit->distance + PAST;
    return true;
}
