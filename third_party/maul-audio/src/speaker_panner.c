// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Speaker panners (VBAP, Pulkki 1997). Speakers and directions are unit
// vectors in the field's axes (x ahead, y left, z up). A layout's
// speakers, with imaginary ones at the zenith and the nadir, are
// triangulated once by brute force: every triplet whose plane has all
// the other points on one side is a face of their convex hull. A
// direction's gains come from the faces whose inverse matrices give no
// negative gain; an imaginary speaker's gain is shared equally
// by the real speakers it has faces with; then the gains are normalized
// for energy.

#include "allocator.h"
#include "spherical_harmonics.h"

#include "maul-audio/speakers.h"

#include <math.h>
#include <string.h>

#define PANNER_DEF_COOKIE 0x6D617370u
#define MAX_CHANNELS      12
#define MAX_POINTS        (MAX_CHANNELS + 2)
#define MAX_TRIANGLES     64
#define PI_F              3.14159265f
// How far below zero a face's least gain may round and the face still
// hold a direction.
#define HELD 1e-5f

typedef enum Kind
{
    kind_mono,
    kind_stereo,
    kind_hull,
} Kind;

struct maudSpeakerPanner
{
    maudAllocator allocator;
    maudChannelLayout layout;
    uint32_t channels;
    Kind kind;
    // Real speakers (the low-frequency channel left out), then the
    // imaginary ones above and below.
    uint32_t realCount;
    uint32_t pointCount;
    uint32_t channelOf[MAX_CHANNELS];
    float points[MAX_POINTS][3];
    uint32_t triangleCount;
    uint32_t triangles[MAX_TRIANGLES][3];
    float inverses[MAX_TRIANGLES][9];
    // For the imaginary speakers above and below: their real neighbours.
    uint32_t neighbourCount[2];
    uint32_t neighbours[2][MAX_CHANNELS];
};

maudSpeakerPannerDef maudDefaultSpeakerPannerDef(void)
{
    return (maudSpeakerPannerDef){
        .cookie = PANNER_DEF_COOKIE,
        .layout = maud_layoutStereo,
        .allocator = {nullptr, nullptr, nullptr},
    };
}

static void Point(float azimuthDegrees, float elevationDegrees, float* p)
{
    float a = azimuthDegrees * PI_F / 180.0f;
    float e = elevationDegrees * PI_F / 180.0f;
    p[0] = cosf(e) * cosf(a);
    p[1] = cosf(e) * sinf(a);
    p[2] = sinf(e);
}

static void Gather(maudSpeakerPanner* panner)
{
    for (uint32_t c = 0; c < panner->channels; ++c)
    {
        if (maudGetLayoutSpeaker(panner->layout, c) == maud_speakerLowFrequency)
        {
            continue;
        }
        maudSpeakerPosition at = maudGetLayoutSpeakerPosition(panner->layout, c);
        Point(at.azimuthDegrees, at.elevationDegrees, panner->points[panner->realCount]);
        panner->channelOf[panner->realCount++] = c;
    }
    Point(0.0f, 90.0f, panner->points[panner->realCount]);
    Point(0.0f, -90.0f, panner->points[panner->realCount + 1]);
    panner->pointCount = panner->realCount + 2;
}

// Whether the plane through points i, j and k has every other point on
// one side, and so is a face of the hull.
static bool Face(const maudSpeakerPanner* panner, uint32_t i, uint32_t j, uint32_t k)
{
    const float* a = panner->points[i];
    double u[3];
    double v[3];
    for (int axis = 0; axis < 3; ++axis)
    {
        u[axis] = (double)panner->points[j][axis] - (double)a[axis];
        v[axis] = (double)panner->points[k][axis] - (double)a[axis];
    }
    double n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
    if (n[0] * n[0] + n[1] * n[1] + n[2] * n[2] < 1e-12)
    {
        return false;
    }
    bool above = false;
    bool below = false;
    for (uint32_t m = 0; m < panner->pointCount; ++m)
    {
        double side = 0.0;
        for (int axis = 0; axis < 3; ++axis)
        {
            side += ((double)panner->points[m][axis] - (double)a[axis]) * n[axis];
        }
        above = above || side > 1e-7;
        below = below || side < -1e-7;
    }
    return !(above && below);
}

// The inverse of the matrix whose columns are a triangle's points; false
// when they are (nearly) dependent.
static bool Invert(const maudSpeakerPanner* panner, const uint32_t* t, float* inverse)
{
    double p[3][3];
    for (int v = 0; v < 3; ++v)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            p[v][axis] = (double)panner->points[t[v]][axis];
        }
    }
    const double* a = p[0];
    const double* b = p[1];
    const double* c = p[2];
    // Rows of the inverse: the cross products over the determinant.
    double rows[3][3] = {
        {b[1] * c[2] - b[2] * c[1], b[2] * c[0] - b[0] * c[2], b[0] * c[1] - b[1] * c[0]},
        {c[1] * a[2] - c[2] * a[1], c[2] * a[0] - c[0] * a[2], c[0] * a[1] - c[1] * a[0]},
        {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]},
    };
    double det = a[0] * rows[0][0] + a[1] * rows[0][1] + a[2] * rows[0][2];
    if (fabs(det) < 1e-9)
    {
        return false;
    }
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            inverse[3 * i + j] = (float)(rows[i][j] / det);
        }
    }
    return true;
}

static void Neighbour(maudSpeakerPanner* panner, uint32_t imaginary, uint32_t real)
{
    uint32_t* count = &panner->neighbourCount[imaginary];
    for (uint32_t i = 0; i < *count; ++i)
    {
        if (panner->neighbours[imaginary][i] == real)
        {
            return;
        }
    }
    panner->neighbours[imaginary][(*count)++] = real;
}

static void AddTriangle(maudSpeakerPanner* panner, uint32_t i, uint32_t j, uint32_t k)
{
    uint32_t t[3] = {i, j, k};
    if (panner->triangleCount == MAX_TRIANGLES ||
        !Invert(panner, t, panner->inverses[panner->triangleCount]))
    {
        return;
    }
    memcpy(panner->triangles[panner->triangleCount++], t, sizeof(t));
    for (int v = 0; v < 3; ++v)
    {
        if (t[v] < panner->realCount)
        {
            continue;
        }
        for (int w = 0; w < 3; ++w)
        {
            if (t[w] < panner->realCount)
            {
                Neighbour(panner, t[v] - panner->realCount, t[w]);
            }
        }
    }
}

static void Triangulate(maudSpeakerPanner* panner)
{
    uint32_t n = panner->pointCount;
    for (uint32_t i = 0; i < n; ++i)
    {
        for (uint32_t j = i + 1; j < n; ++j)
        {
            for (uint32_t k = j + 1; k < n; ++k)
            {
                if (Face(panner, i, j, k))
                {
                    AddTriangle(panner, i, j, k);
                }
            }
        }
    }
}

maudResult maudCreateSpeakerPanner(const maudSpeakerPannerDef* def, maudSpeakerPanner** pannerOut)
{
    if (pannerOut != nullptr)
    {
        *pannerOut = nullptr;
    }
    if (def == nullptr || pannerOut == nullptr || def->cookie != PANNER_DEF_COOKIE ||
        maudGetLayoutChannelCount(def->layout) == 0 || !maudIsAllocatorValid(&def->allocator))
    {
        return maud_errorInvalid;
    }
    maudSpeakerPanner* panner =
        maudAllocate(&def->allocator, sizeof(maudSpeakerPanner), alignof(maudSpeakerPanner));
    if (panner == nullptr)
    {
        return maud_errorCapacity;
    }
    *panner = (maudSpeakerPanner){
        .allocator = def->allocator,
        .layout = def->layout,
        .channels = maudGetLayoutChannelCount(def->layout),
    };
    Gather(panner);
    panner->kind = panner->realCount == 1   ? kind_mono
                   : panner->realCount == 2 ? kind_stereo
                                            : kind_hull;
    if (panner->kind == kind_hull)
    {
        Triangulate(panner);
    }
    *pannerOut = panner;
    return maud_success;
}

void maudDestroySpeakerPanner(maudSpeakerPanner* panner)
{
    if (panner == nullptr)
    {
        return;
    }
    maudAllocator allocator = panner->allocator;
    maudRelease(&allocator, panner, sizeof(maudSpeakerPanner), alignof(maudSpeakerPanner));
}

// Stereo: the side angle (behind folds to the front), clamped to the
// speakers' span, by VBAP between the two.
static void Stereo(const maudSpeakerPanner* panner, const float* u, float* real)
{
    float angle = atan2f(u[1], fabsf(u[0]));
    float left = atan2f(panner->points[0][1], panner->points[0][0]);
    float right = atan2f(panner->points[1][1], panner->points[1][0]);
    float high = left > right ? left : right;
    float low = left > right ? right : left;
    angle = angle > high ? high : angle < low ? low : angle;
    float x = cosf(angle);
    float y = sinf(angle);
    // Solve x = g0 l0 + g1 r0, y = g0 l1 + g1 r1.
    const float* l = panner->points[0];
    const float* r = panner->points[1];
    float det = l[0] * r[1] - l[1] * r[0];
    real[0] = (x * r[1] - y * r[0]) / det;
    real[1] = (l[0] * y - l[1] * x) / det;
}

// Adds a triangle's gains to the real speakers, an imaginary corner's
// shared equally by its neighbours.
static void Spread(const maudSpeakerPanner* panner, uint32_t t, const float* g, float* real)
{
    for (int v = 0; v < 3; ++v)
    {
        uint32_t point = panner->triangles[t][v];
        float gain = fmaxf(g[v], 0.0f);
        if (point < panner->realCount)
        {
            real[point] += gain;
            continue;
        }
        uint32_t imaginary = point - panner->realCount;
        uint32_t count = panner->neighbourCount[imaginary];
        for (uint32_t i = 0; i < count; ++i)
        {
            real[panner->neighbours[imaginary][i]] += gain / (float)count;
        }
    }
}

// Hull: the gains of every face that holds the direction, summed. Inside
// a face only it holds the direction; on an edge or corner the faces
// there agree; four speakers in one plane (7.1.4's back ones) make two
// triangulations that both hold it, and summing them keeps a source on
// the mirror plane balanced where picking one would lean to a diagonal.
// Rounding can leave no face holding it: then the one nearest to.
static void Hull(const maudSpeakerPanner* panner, const float* u, float* real)
{
    uint32_t best = 0;
    float bestLeast = -INFINITY;
    float bestGains[3] = {0.0f, 0.0f, 0.0f};
    bool held = false;
    for (uint32_t t = 0; t < panner->triangleCount; ++t)
    {
        const float* m = panner->inverses[t];
        float g[3];
        for (int i = 0; i < 3; ++i)
        {
            g[i] = m[3 * i] * u[0] + m[3 * i + 1] * u[1] + m[3 * i + 2] * u[2];
        }
        float least = fminf(g[0], fminf(g[1], g[2]));
        if (least >= -HELD)
        {
            Spread(panner, t, g, real);
            held = true;
        }
        if (least > bestLeast)
        {
            bestLeast = least;
            best = t;
            memcpy(bestGains, g, sizeof(g));
        }
    }
    if (!held)
    {
        Spread(panner, best, bestGains, real);
    }
}

// The gains of a direction for the layout's channels.
static void Gains(const maudSpeakerPanner* panner, maudVector3 direction, float* gains)
{
    float u[3];
    maudFieldAxes(direction, &u[0], &u[1], &u[2]);
    float real[MAX_CHANNELS] = {0.0f};
    if (panner->kind == kind_mono)
    {
        real[0] = 1.0f;
    }
    else if (panner->kind == kind_stereo)
    {
        Stereo(panner, u, real);
    }
    else
    {
        Hull(panner, u, real);
    }
    float energy = 0.0f;
    for (uint32_t i = 0; i < panner->realCount; ++i)
    {
        energy += real[i] * real[i];
    }
    float scale = energy > 0.0f ? 1.0f / sqrtf(energy) : 0.0f;
    for (uint32_t c = 0; c < panner->channels; ++c)
    {
        gains[c] = 0.0f;
    }
    for (uint32_t i = 0; i < panner->realCount; ++i)
    {
        gains[panner->channelOf[i]] = real[i] * scale;
    }
}

static bool Finite(maudVector3 v)
{
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}

maudResult maudGetSpeakerGains(const maudSpeakerPanner* panner, maudVector3 direction,
                               float* gainsOut)
{
    if (panner == nullptr || gainsOut == nullptr || !Finite(direction))
    {
        return maud_errorInvalid;
    }
    Gains(panner, direction, gainsOut);
    return maud_success;
}

static bool SourceValid(const maudPanSource* source)
{
    return source != nullptr && Finite(source->direction) && isfinite(source->gain);
}

static bool OutValid(const maudSpeakerPanner* panner, float* const* out)
{
    if (out == nullptr)
    {
        return false;
    }
    for (uint32_t c = 0; c < panner->channels; ++c)
    {
        if (out[c] == nullptr)
        {
            return false;
        }
    }
    return true;
}

maudResult maudPanToSpeakers(const maudSpeakerPanner* panner, const maudPanSource* from,
                             const maudPanSource* to, const float* in, float* const* out,
                             uint32_t frames)
{
    if (panner == nullptr || !SourceValid(from) || !SourceValid(to) || in == nullptr ||
        !OutValid(panner, out))
    {
        return maud_errorInvalid;
    }
    float start[MAX_CHANNELS];
    float end[MAX_CHANNELS];
    Gains(panner, from->direction, start);
    Gains(panner, to->direction, end);
    for (uint32_t c = 0; c < panner->channels; ++c)
    {
        float a = start[c] * from->gain;
        float b = end[c] * to->gain;
        if (a == 0.0f && b == 0.0f)
        {
            continue;
        }
        float step = frames > 0 ? (b - a) / (float)frames : 0.0f;
        for (uint32_t n = 0; n < frames; ++n)
        {
            out[c][n] += (a + step * (float)(n + 1)) * in[n];
        }
    }
    return maud_success;
}
