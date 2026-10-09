// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A source's direct geometry (direct_step.h), in double for the
// differences of world positions far from the origin.

#include "direct_step.h"

#include <math.h>

bool maudPoseValid(const maudPose* pose)
{
    const float values[7] = {pose->position.x,    pose->position.y,    pose->position.z,
                             pose->orientation.x, pose->orientation.y, pose->orientation.z,
                             pose->orientation.w};
    for (int i = 0; i < 7; ++i)
    {
        if (!isfinite(values[i]))
        {
            return false;
        }
    }
    maudQuaternion q = pose->orientation;
    return q.x != 0.0f || q.y != 0.0f || q.z != 0.0f || q.w != 0.0f;
}

maudVector3 maudUnrotate(maudQuaternion q, maudVector3 v)
{
    double qx = (double)q.x;
    double qy = (double)q.y;
    double qz = (double)q.z;
    double qw = (double)q.w;
    double n = sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
    // The conjugate turns back: u = -q's vector part.
    double ux = -qx / n;
    double uy = -qy / n;
    double uz = -qz / n;
    double w = qw / n;
    double vx = (double)v.x;
    double vy = (double)v.y;
    double vz = (double)v.z;
    // v + 2w (u x v) + 2 u x (u x v)
    double cx = uy * vz - uz * vy;
    double cy = uz * vx - ux * vz;
    double cz = ux * vy - uy * vx;
    double dx = uy * cz - uz * cy;
    double dy = uz * cx - ux * cz;
    double dz = ux * cy - uy * cx;
    return (maudVector3){(float)(vx + 2.0 * (w * cx + dx)), (float)(vy + 2.0 * (w * cy + dy)),
                         (float)(vz + 2.0 * (w * cz + dz))};
}

void maudDirectGeometry(const maudPose* listener, const maudPose* source,
                        const maudDirectivityPattern* pattern, maudDirectResult* result)
{
    double dx = (double)source->position.x - (double)listener->position.x;
    double dy = (double)source->position.y - (double)listener->position.y;
    double dz = (double)source->position.z - (double)listener->position.z;
    double distance = sqrt(dx * dx + dy * dy + dz * dz);
    maudVector3 toSource = {(float)dx, (float)dy, (float)dz};
    maudVector3 toListener = {(float)-dx, (float)-dy, (float)-dz};
    result->distance = (float)distance;
    if (distance > 0.0)
    {
        maudVector3 local = maudUnrotate(listener->orientation, toSource);
        float length = sqrtf(local.x * local.x + local.y * local.y + local.z * local.z);
        result->direction = (maudVector3){local.x / length, local.y / length, local.z / length};
    }
    else
    {
        result->direction = (maudVector3){0.0f, 0.0f, -1.0f};
    }
    // The pattern was checked when the source was made.
    if (maudGetDirectivity(pattern, maudUnrotate(source->orientation, toListener),
                           result->directivity) != maud_success)
    {
        for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
        {
            result->directivity[b] = 1.0f;
        }
    }
    result->occlusion = 0.0f;
    result->surfaces = 0;
    result->pathed = false;
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        result->transmission[b] = 1.0f;
    }
}
