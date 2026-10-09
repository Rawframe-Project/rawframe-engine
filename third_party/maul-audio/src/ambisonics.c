// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Encoding into and rotating an ambisonic bed. Gains are the closed-form
// SN3D real spherical harmonics in ACN order, in the field's axes (x
// ahead, y left, z up); the listener's frame (+x right, +y up, -z ahead)
// is converted at the edge. Changes ramp linearly per frame across a
// call.

#include "maul-audio/ambisonics.h"

#include "ambisonic_rotation.h"
#include "spherical_harmonics.h"

#include <math.h>

uint32_t maudGetAmbisonicChannelCount(uint32_t order)
{
    return order >= 1 && order <= MAUD_MAX_AMBISONIC_ORDER ? (order + 1) * (order + 1) : 0;
}

static bool Finite(maudVector3 v)
{
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}

maudResult maudGetAmbisonicGains(uint32_t order, maudVector3 direction, float* gainsOut)
{
    if (maudGetAmbisonicChannelCount(order) == 0 || gainsOut == nullptr || !Finite(direction))
    {
        return maud_errorInvalid;
    }
    float x;
    float y;
    float z;
    maudFieldAxes(direction, &x, &y, &z);
    maudSphericalHarmonics(order, x, y, z, gainsOut);
    return maud_success;
}

static bool BedValid(float* const* bed, uint32_t channels)
{
    if (bed == nullptr)
    {
        return false;
    }
    for (uint32_t c = 0; c < channels; ++c)
    {
        if (bed[c] == nullptr)
        {
            return false;
        }
    }
    return true;
}

static bool SourceValid(const maudPanSource* source)
{
    return source != nullptr && Finite(source->direction) && isfinite(source->gain);
}

maudResult maudEncodeAmbisonic(uint32_t order, const maudPanSource* from, const maudPanSource* to,
                               const float* in, float* const* bed, uint32_t frames)
{
    uint32_t channels = maudGetAmbisonicChannelCount(order);
    if (channels == 0 || !SourceValid(from) || !SourceValid(to) || in == nullptr ||
        !BedValid(bed, channels))
    {
        return maud_errorInvalid;
    }
    float start[MAUD_MAX_AMBISONIC_CHANNELS];
    float end[MAUD_MAX_AMBISONIC_CHANNELS];
    float x;
    float y;
    float z;
    maudFieldAxes(from->direction, &x, &y, &z);
    maudSphericalHarmonics(order, x, y, z, start);
    maudFieldAxes(to->direction, &x, &y, &z);
    maudSphericalHarmonics(order, x, y, z, end);
    for (uint32_t c = 0; c < channels; ++c)
    {
        float a = start[c] * from->gain;
        float step = frames > 0 ? (end[c] * to->gain - a) / (float)frames : 0.0f;
        float* out = bed[c];
        for (uint32_t n = 0; n < frames; ++n)
        {
            out[n] += (a + step * (float)(n + 1)) * in[n];
        }
    }
    return maud_success;
}

// A unit quaternion in the listener's frame as a rotation matrix in the
// field's axes, P R P^T with field = P listener.
static bool FieldRotation(const maudQuaternion* q, double rotation[3][3])
{
    if (q == nullptr || !isfinite(q->x) || !isfinite(q->y) || !isfinite(q->z) || !isfinite(q->w))
    {
        return false;
    }
    double qx = (double)q->x;
    double qy = (double)q->y;
    double qz = (double)q->z;
    double qw = (double)q->w;
    double length = sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
    if (length == 0.0)
    {
        return false;
    }
    double x = qx / length;
    double y = qy / length;
    double z = qz / length;
    double w = qw / length;
    double r[3][3] = {
        {1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - z * w), 2.0 * (x * z + y * w)},
        {2.0 * (x * y + z * w), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - x * w)},
        {2.0 * (x * z - y * w), 2.0 * (y * z + x * w), 1.0 - 2.0 * (x * x + y * y)},
    };
    // field x = -listener z, field y = -listener x, field z = listener y.
    static const int source[3] = {2, 0, 1};
    static const double sign[3] = {-1.0, -1.0, 1.0};
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            rotation[i][j] = sign[i] * sign[j] * r[source[i]][source[j]];
        }
    }
    return true;
}

// Rotates one frame: y = A x, or A x + t (B x - A x) while moving.
static void RotateFrame(uint32_t order, const float* a, const float* b, float t, float* frame)
{
    float in[MAUD_MAX_AMBISONIC_CHANNELS] = {0};
    for (uint32_t c = 0; c < (order + 1) * (order + 1); ++c)
    {
        in[c] = frame[c];
    }
    uint32_t first = 1;
    size_t at = 0;
    for (uint32_t l = 1; l <= order; ++l)
    {
        uint32_t size = 2 * l + 1;
        for (uint32_t i = 0; i < size; ++i)
        {
            float sa = 0.0f;
            float sb = 0.0f;
            for (uint32_t j = 0; j < size; ++j)
            {
                sa += a[at + i * size + j] * in[first + j];
                sb += b != nullptr ? b[at + i * size + j] * in[first + j] : 0.0f;
            }
            frame[first + i] = b != nullptr ? sa + t * (sb - sa) : sa;
        }
        at += (size_t)size * size;
        first += size;
    }
}

maudResult maudRotateAmbisonic(uint32_t order, const maudQuaternion* from, const maudQuaternion* to,
                               float* const* bed, uint32_t frames)
{
    uint32_t channels = maudGetAmbisonicChannelCount(order);
    double start[3][3];
    double end[3][3];
    if (channels == 0 || !FieldRotation(from, start) || !FieldRotation(to, end) ||
        !BedValid(bed, channels))
    {
        return maud_errorInvalid;
    }
    float a[MAUD_ROTATION_FLOATS] = {0};
    float b[MAUD_ROTATION_FLOATS] = {0};
    maudAmbisonicRotation(order, start, a);
    maudAmbisonicRotation(order, end, b);
    bool still = from->x == to->x && from->y == to->y && from->z == to->z && from->w == to->w;
    float frame[MAUD_MAX_AMBISONIC_CHANNELS];
    for (uint32_t n = 0; n < frames; ++n)
    {
        for (uint32_t c = 0; c < channels; ++c)
        {
            frame[c] = bed[c][n];
        }
        RotateFrame(order, still ? b : a, still ? nullptr : b, (float)(n + 1) / (float)frames,
                    frame);
        for (uint32_t c = 1; c < channels; ++c)
        {
            bed[c][n] = frame[c];
        }
    }
    return maud_success;
}
