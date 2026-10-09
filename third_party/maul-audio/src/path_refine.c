// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The refinement and diffraction (path_refine.h). Every visibility test
// is one ray, so the refinement asks the host one ray at a time; the
// diffraction term follows Steam Audio's half-plane, in pairs of doubles
// for its complex values.

#include "path_refine.h"

#include <math.h>

#define PI_D         3.14159265358979323846
#define SLIDE_PASSES 2
#define SLIDE_STEPS  12
#define SEARCH_FROM  0.25f
#define SEARCH_TO    0.002f
// The pattern search's moves a vertex makes at most.
#define SEARCH_MOVES  64
#define DIRECTIONS    26
#define SOUND_SPEED   343.0
#define EDGE_DISTANCE 0.05

typedef struct Complex
{
    double re;
    double im;
} Complex;

static Complex Times(Complex a, Complex b)
{
    return (Complex){a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re};
}

static Complex Scaled(Complex a, double s)
{
    return (Complex){a.re * s, a.im * s};
}

static Complex Polar(double radius, double angle)
{
    return (Complex){radius * cos(angle), radius * sin(angle)};
}

// The transition function's approximation.
static Complex Transition(double x)
{
    Complex e = Polar(1.0, 0.25 * PI_D * sqrt(x / (x + 1.4)));
    if (x < 0.8)
    {
        return Scaled(e, sqrt(PI_D * x) * (1.0 - sqrt(x) / (0.7 * sqrt(x) + 1.2)));
    }
    return Scaled(e, 1.0 - 0.8 / ((x + 1.25) * (x + 1.25)));
}

// One of the four terms; plus selects the cotangent's sign.
static Complex Term(double beta, bool plus, double k)
{
    const double n = 2.0;
    double N = plus ? (beta <= PI_D * (n - 1.0) ? 0.0 : 1.0)
                    : (beta < PI_D * (1.0 - n)    ? -1.0
                       : beta <= PI_D * (1.0 + n) ? 0.0
                                                  : 1.0);
    double arg = plus ? (PI_D + beta) / (2.0 * n) : (PI_D - beta) / (2.0 * n);
    double c = cos(PI_D * n * N - 0.5 * beta);
    double x = k * EDGE_DISTANCE * 2.0 * c * c;
    if (fabs(sin(arg)) > 1e-12 && fabs(cos(arg) / sin(arg)) < 1e7)
    {
        return Scaled(Transition(x), cos(arg) / sin(arg));
    }
    double eps = plus ? beta - 2.0 * PI_D * n * N + PI_D : -(beta - 2.0 * PI_D * n * N - PI_D);
    Complex e = Polar(1.0, -0.25 * PI_D);
    Complex second = Scaled(e, 2.0 * k * EDGE_DISTANCE * eps);
    Complex inner = {sqrt(2.0 * PI_D * k * EDGE_DISTANCE) * (eps > 0.0 ? 1.0 : -1.0) - second.re,
                     -second.im};
    return Scaled(Times(e, inner), n);
}

// The magnitude of the diffraction coefficient for a turn at a
// frequency.
static double Coefficient(double turn, double hz)
{
    double k = 2.0 * PI_D * hz / SOUND_SPEED;
    double beta = PI_D + turn;
    Complex sum = {0.0, 0.0};
    for (int t = 0; t < 4; ++t)
    {
        Complex d = Term(beta, t % 2 == 0, k);
        sum.re += d.re;
        sum.im += d.im;
    }
    Complex d0 = Scaled(Polar(1.0, -0.25 * PI_D), 1.0 / (4.0 * sqrt(2.0 * PI_D * k)));
    Complex v = Times(d0, sum);
    return hypot(v.re, v.im);
}

void maudSetupDiffraction(maudDiffraction* diffraction)
{
    const double centres[MAUD_DIRECT_BANDS] = {400.0, 4400.0, 15000.0};
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        double reference = Coefficient(1e-8, centres[b]);
        for (uint32_t d = 0; d < MAUD_DIFFRACTION_STEPS; ++d)
        {
            double turn = d == 0 ? 1e-8 : (double)d * PI_D / 180.0;
            diffraction->gain[b][d] = (float)fmin(1.0, Coefficient(turn, centres[b]) / reference);
        }
    }
}

static maudVector3 Lerp(maudVector3 a, maudVector3 b, float t)
{
    return (maudVector3){a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z)};
}

static float Length(maudVector3 a, maudVector3 b)
{
    float x = b.x - a.x;
    float y = b.y - a.y;
    float z = b.z - a.z;
    return sqrtf(x * x + y * y + z * z);
}

typedef struct Rays
{
    maudAnyHitFn* anyHit;
    void* context;
} Rays;

static bool Visible(const Rays* r, maudVector3 a, maudVector3 b)
{
    float d = Length(a, b);
    if (d == 0.0f)
    {
        return true;
    }
    maudRay ray = {a, {(b.x - a.x) / d, (b.y - a.y) / d, (b.z - a.z) / d}, 0.0f, d};
    uint8_t occluded = 0;
    r->anyHit(&ray, 1, &occluded, r->context);
    return occluded == 0;
}

static uint32_t Drop(const Rays* r, maudVector3* v, uint32_t count)
{
    uint32_t k = 1;
    while (k + 1 < count)
    {
        if (Visible(r, v[k - 1], v[k + 1]))
        {
            for (uint32_t i = k; i + 1 < count; ++i)
            {
                v[i] = v[i + 1];
            }
            count -= 1;
        }
        else
        {
            k += 1;
        }
    }
    return count;
}

// How far along from v toward a point it can slide with other still
// in sight, as a fraction, by bisection.
static float Bisect(const Rays* r, maudVector3 v, maudVector3 toward, maudVector3 other)
{
    float low = 0.0f;
    float high = 1.0f;
    for (int step = 0; step < SLIDE_STEPS; ++step)
    {
        float t = 0.5f * (low + high);
        if (Visible(r, Lerp(v, toward, t), other))
        {
            low = t;
        }
        else
        {
            high = t;
        }
    }
    return low;
}

static void Slide(const Rays* r, maudVector3* v, uint32_t count)
{
    for (int pass = 0; pass < SLIDE_PASSES; ++pass)
    {
        for (uint32_t k = 1; k + 1 < count; ++k)
        {
            v[k] = Lerp(v[k], v[k - 1], Bisect(r, v[k], v[k - 1], v[k + 1]));
            v[k] = Lerp(v[k], v[k + 1], Bisect(r, v[k], v[k + 1], v[k - 1]));
        }
    }
}

// The pattern search's directions: the 26 neighbours of a cube's
// centre, unit length.
static void Directions(maudVector3* d)
{
    uint32_t n = 0;
    for (int i = -1; i <= 1; ++i)
    {
        for (int j = -1; j <= 1; ++j)
        {
            for (int k = -1; k <= 1; ++k)
            {
                if (i != 0 || j != 0 || k != 0)
                {
                    float s = 1.0f / sqrtf((float)(i * i + j * j + k * k));
                    d[n++] = (maudVector3){(float)i * s, (float)j * s, (float)k * s};
                }
            }
        }
    }
}

// Moves a vertex while a step shortens the path with both legs visible.
static void Search(const Rays* r, const maudVector3* d, maudVector3* v, uint32_t k)
{
    maudVector3 a = v[k - 1];
    maudVector3 b = v[k + 1];
    float step = SEARCH_FROM;
    for (int moves = 0; step > SEARCH_TO && moves < SEARCH_MOVES;)
    {
        float now = Length(a, v[k]) + Length(v[k], b);
        bool moved = false;
        for (int i = 0; i < DIRECTIONS && !moved; ++i)
        {
            maudVector3 p = {v[k].x + step * d[i].x, v[k].y + step * d[i].y,
                             v[k].z + step * d[i].z};
            if (Length(a, p) + Length(p, b) < now && Visible(r, a, p) && Visible(r, p, b))
            {
                v[k] = p;
                moved = true;
                moves += 1;
            }
        }
        step = moved ? step : 0.5f * step;
    }
}

uint32_t maudRefinePath(maudAnyHitFn* anyHit, void* context, maudVector3* vertices, uint32_t count)
{
    if (anyHit == nullptr)
    {
        return count;
    }
    Rays r = {anyHit, context};
    count = Drop(&r, vertices, count);
    Slide(&r, vertices, count);
    maudVector3 d[DIRECTIONS];
    Directions(d);
    for (uint32_t k = 1; k + 1 < count; ++k)
    {
        Search(&r, d, vertices, k);
    }
    return Drop(&r, vertices, count);
}

void maudPathDiffraction(const maudDiffraction* diffraction, const maudVector3* vertices,
                         uint32_t count, float* gains)
{
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        gains[b] = 1.0f;
    }
    for (uint32_t k = 1; k + 1 < count; ++k)
    {
        maudVector3 in = vertices[k - 1];
        maudVector3 at = vertices[k];
        maudVector3 out = vertices[k + 1];
        float a = Length(in, at);
        float c = Length(at, out);
        if (a == 0.0f || c == 0.0f)
        {
            continue;
        }
        float cosine = ((at.x - in.x) * (out.x - at.x) + (at.y - in.y) * (out.y - at.y) +
                        (at.z - in.z) * (out.z - at.z)) /
                       (a * c);
        float degrees = acosf(fminf(1.0f, fmaxf(-1.0f, cosine))) * (float)(180.0 / PI_D);
        uint32_t i = (uint32_t)degrees;
        i = i >= MAUD_DIFFRACTION_STEPS - 1 ? MAUD_DIFFRACTION_STEPS - 2 : i;
        float t = degrees - (float)i;
        for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
        {
            const float* g = diffraction->gain[b];
            gains[b] *= g[i] + t * (g[i + 1] - g[i]);
        }
    }
}
