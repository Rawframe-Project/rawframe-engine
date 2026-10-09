// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Near-field filters from the generated table: its parameters
// interpolated bilinearly in angle and inverse distance (the generator
// checks that this adds at most 0.15 dB), the set's distance taken out,
// and the analog shelf made digital by the bilinear transform, warped to
// keep its corner.

#include "near_field.h"

#include "near_field_table.h"

#include <math.h>
#include <stdint.h>

#define ANGLE_STEP     5.0f
#define SPEED_OF_SOUND 343.0f
// The bilinear transform's corner stays below the Nyquist frequency.
#define MAX_HALF_ANGLE 1.55f

typedef struct Parameters
{
    float gainDb;
    float shelfDb;
    float logCorner;
} Parameters;

static float Clamp(float value, float low, float high)
{
    return value < low ? low : value > high ? high : value;
}

// The table row below a value in a rising axis of count entries, and how
// far the value lies towards the next one.
static uint32_t Below(const float* axis, uint32_t count, float value, float* fractionOut)
{
    uint32_t index = 0;
    while (index + 2 < count && axis[index + 1] <= value)
    {
        ++index;
    }
    *fractionOut = (value - axis[index]) / (axis[index + 1] - axis[index]);
    return index;
}

static Parameters Lookup(float angleDegrees, float inverseDistance)
{
    float angle = Clamp(angleDegrees, 0.0f, 180.0f) / ANGLE_STEP;
    uint32_t i = (uint32_t)angle;
    i = i > MAUD_NEAR_FIELD_ANGLES - 2 ? MAUD_NEAR_FIELD_ANGLES - 2 : i;
    float fa = angle - (float)i;
    const float* axis = maudNearFieldInverseDistances;
    float inverse = Clamp(inverseDistance, 0.0f, axis[MAUD_NEAR_FIELD_DISTANCES - 1]);
    float fd;
    uint32_t j = Below(axis, MAUD_NEAR_FIELD_DISTANCES, inverse, &fd);
    float weights[4] = {(1.0f - fa) * (1.0f - fd), (1.0f - fa) * fd, fa * (1.0f - fd), fa * fd};
    const float* corners[4] = {maudNearFieldTable[i][j], maudNearFieldTable[i][j + 1],
                               maudNearFieldTable[i + 1][j], maudNearFieldTable[i + 1][j + 1]};
    float sum[3] = {0.0f, 0.0f, 0.0f};
    for (int corner = 0; corner < 4; ++corner)
    {
        for (int k = 0; k < 3; ++k)
        {
            sum[k] += weights[corner] * corners[corner][k];
        }
    }
    return (Parameters){sum[0], sum[1], sum[2]};
}

maudNearFieldFilter maudNearField(float angleDegrees, float inverseDistance,
                                  float setInverseDistance, float headRadius, float sampleRate)
{
    Parameters source = Lookup(angleDegrees, inverseDistance);
    Parameters set = Lookup(angleDegrees, setInverseDistance);
    float gain = powf(10.0f, (source.gainDb - set.gainDb) / 20.0f);
    float high = powf(10.0f, (source.shelfDb - set.shelfDb) / 20.0f);
    // The corner in radians per second, then the bilinear transform's
    // prewarped constant.
    float corner = expf(source.logCorner) * SPEED_OF_SOUND / headRadius;
    float k = tanf(Clamp(corner / (2.0f * sampleRate), 0.0f, MAX_HALF_ANGLE));
    return (maudNearFieldFilter){
        .b0 = gain * (high + k) / (1.0f + k),
        .b1 = gain * (k - high) / (1.0f + k),
        .a1 = (k - 1.0f) / (1.0f + k),
    };
}

static maudVector3 Normalized(maudVector3 v)
{
    float length = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (length == 0.0f)
    {
        return (maudVector3){0.0f, 0.0f, -1.0f};
    }
    return (maudVector3){v.x / length, v.y / length, v.z / length};
}

maudVector3 maudEarDirection(maudVector3 position, float earX, float setDistance)
{
    maudVector3 ray = {position.x - earX, position.y, position.z};
    float length = sqrtf(ray.x * ray.x + ray.y * ray.y + ray.z * ray.z);
    if (length == 0.0f || fabsf(earX) >= setDistance)
    {
        return Normalized(position);
    }
    maudVector3 u = {ray.x / length, ray.y / length, ray.z / length};
    // |ear + t u| = setDistance: t^2 + 2 t (ear . u) + ear^2 - R^2 = 0,
    // the positive root (the ear is inside the sphere).
    float along = earX * u.x;
    float t = -along + sqrtf(along * along - earX * earX + setDistance * setDistance);
    return Normalized((maudVector3){earX + t * u.x, t * u.y, t * u.z});
}
