// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The estimate (reverb_estimate.h). A hit reflects toward the listener
// a Lambert share (the scattering) and a specular lobe of exponent 100
// (the rest), over 4 pi r^2 with r at least 1 m, the air taking its
// share over the whole path; the ray goes on mirrored or, with the
// scattering's probability, cosine-weighted, until each band has lost
// 40 dB, the bins end or the bounces run out. Directions out of the
// listener follow a Fibonacci lattice; the bounces' random numbers are
// hashes of the ray and the bounce, so every run is the same.

#include "reverb_estimate.h"

#include "portable_math.h"
#include "spherical_harmonics.h"

#include <math.h>
#include <string.h>

#define PI_F           3.14159265358979323846f
#define SPEED_OF_SOUND 343.0f
#define BIN_SECONDS    0.01f
// Rays leave a surface and reach the listener from 1 mm on.
#define GAP        0.001f
#define LOBE       100
#define CUT_ENERGY 1e-4f
#define MIN_TIME   0.1f
#define MAX_TIME   20.0f
// The bins whose mean sets a truncated decay's level at the cut.
#define TAIL_BINS 10u

typedef struct Path
{
    float origin[3];
    float direction[3];
    // The energy left per band after absorption (air apart).
    float energy[MAUD_DIRECT_BANDS];
    float distance;
    uint32_t ray;
    bool alive;
} Path;

uint32_t maudReverbBatches(uint32_t rays)
{
    return (rays + MAUD_REVERB_BATCH - 1) / MAUD_REVERB_BATCH;
}

static float Dot(const float* a, const float* b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// A number in [0, 1) from a ray, a bounce and a stream (lowbias32).
static float Random(uint32_t ray, uint32_t bounce, uint32_t stream)
{
    uint32_t x = ray * 0x9E3779B9u ^ bounce * 0x85EBCA6Bu ^ stream * 0xC2B2AE35u;
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return (float)(x >> 8) * (1.0f / 16777216.0f);
}

// e^x and the sine and cosine in float, through the portable functions.
static float ExpF(float x)
{
    return (float)maudExp((double)x);
}

static void SinCosF(float a, float* sine, float* cosine)
{
    double s = 0.0;
    double c = 0.0;
    maudSinCos((double)a, &s, &c);
    *sine = (float)s;
    *cosine = (float)c;
}

static void Start(const maudReverbTrace* trace, uint32_t ray, Path* path)
{
    float z = 1.0f - 2.0f * ((float)ray + 0.5f) / (float)trace->rays;
    float r = sqrtf(fmaxf(0.0f, 1.0f - z * z));
    // The golden angle, wrapped per ray to keep its float exact.
    float a = 2.39996322972865332f * (float)(ray % 65536u);
    float sine = 0.0f;
    float cosine = 0.0f;
    SinCosF(a, &sine, &cosine);
    *path = (Path){.origin = {trace->listener.x, trace->listener.y, trace->listener.z},
                   .direction = {r * cosine, r * sine, z},
                   .energy = {1.0f, 1.0f, 1.0f},
                   .ray = ray,
                   .alive = true};
}

// x^100 by squaring.
static float Lobe(float x)
{
    float x2 = x * x;
    float x4 = x2 * x2;
    float x8 = x4 * x4;
    float x16 = x8 * x8;
    float x32 = x16 * x16;
    float x64 = x32 * x32;
    return x64 * x32 * x4;
}

// A cosine-weighted direction about n (an orthonormal basis after Duff
// et al., 2017).
static void Scatter(const float* n, float u, float v, float* out)
{
    float sign = n[2] >= 0.0f ? 1.0f : -1.0f;
    float a = -1.0f / (sign + n[2]);
    float b = n[0] * n[1] * a;
    float t[3] = {1.0f + sign * n[0] * n[0] * a, sign * b, -sign * n[0]};
    float s[3] = {b, sign + n[1] * n[1] * a, -n[1]};
    float r = sqrtf(u);
    float sine = 0.0f;
    float cosine = 0.0f;
    SinCosF(2.0f * PI_F * v, &sine, &cosine);
    float x = r * cosine;
    float y = r * sine;
    float z = sqrtf(fmaxf(0.0f, 1.0f - u));
    for (int i = 0; i < 3; ++i)
    {
        out[i] = x * t[i] + y * s[i] + z * n[i];
    }
}

typedef struct Shading
{
    float point[3];
    float normal[3];
    float toListener[3];
    float listenerDistance;
    const maudAcousticMaterial* material;
    float hitDistance;
} Shading;

// The hit's geometry, or false if the ray found nothing usable.
static bool Shade(const maudReverbTrace* trace, const Path* path, const maudRay* ray,
                  const maudRayHit* hit, Shading* s)
{
    if (!(hit->distance >= ray->minDistance && hit->distance <= ray->maxDistance) ||
        hit->material >= trace->materialCount)
    {
        return false;
    }
    float n[3] = {hit->normal.x, hit->normal.y, hit->normal.z};
    float length = sqrtf(Dot(n, n));
    if (!(length > 0.0f))
    {
        return false;
    }
    // The normal faces where the ray came from.
    float side = Dot(n, path->direction) > 0.0f ? -1.0f : 1.0f;
    const float* l = &trace->listener.x;
    s->listenerDistance = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        s->normal[i] = side * n[i] / length;
        s->point[i] = path->origin[i] + hit->distance * path->direction[i];
        s->toListener[i] = l[i] - s->point[i];
    }
    s->listenerDistance = sqrtf(Dot(s->toListener, s->toListener));
    for (int i = 0; i < 3 && s->listenerDistance > 0.0f; ++i)
    {
        s->toListener[i] /= s->listenerDistance;
    }
    s->material = &trace->materials[hit->material];
    s->hitDistance = hit->distance;
    return true;
}

static uint32_t Channels(const maudReverbTrace* trace)
{
    return (trace->fieldOrder + 1) * (trace->fieldOrder + 1);
}

// A lit hit's energy on the harmonics of its arrival direction (from the
// listener toward the hit), into bin of the field.
static void Project(const maudReverbTrace* trace, const Shading* s, const float* energy,
                    uint32_t bin, float* field)
{
    maudVector3 arrival = {-s->toListener[0], -s->toListener[1], -s->toListener[2]};
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    maudFieldAxes(arrival, &x, &y, &z);
    float g[16];
    maudSphericalHarmonics(trace->fieldOrder, x, y, z, g);
    uint32_t channels = Channels(trace);
    for (uint32_t c = 0; c < channels; ++c)
    {
        for (uint32_t b = 0; b < MAUD_DIRECT_BANDS; ++b)
        {
            field[(c * MAUD_DIRECT_BANDS + b) * trace->fieldBins + bin] += g[c] * energy[b];
        }
    }
}

// The energy a lit hit sends the listener, into the histogram.
static void Gather(const maudReverbTrace* trace, const Path* path, const Shading* s,
                   maudReverbHistogram* histogram)
{
    float total = path->distance + s->hitDistance + s->listenerDistance;
    float bin = floorf(total / (SPEED_OF_SOUND * BIN_SECONDS));
    if (!(bin < (float)MAUD_REVERB_BINS))
    {
        return;
    }
    float half[3];
    for (int i = 0; i < 3; ++i)
    {
        half[i] = s->toListener[i] - path->direction[i];
    }
    float halfLength = sqrtf(Dot(half, half));
    float specular = halfLength > 0.0f ? fmaxf(Dot(half, s->normal) / halfLength, 0.0f) : 0.0f;
    float scattering = s->material->scattering;
    float lambert = scattering * fmaxf(Dot(s->normal, s->toListener), 0.0f) / PI_F;
    float lobe = (1.0f - scattering) * (float)(LOBE + 2) / (8.0f * PI_F) * Lobe(specular);
    float r = fmaxf(s->listenerDistance, 1.0f);
    // Each ray stands for its share of the sphere.
    float share = 4.0f * PI_F / (float)trace->rays;
    float spread = share * (lambert + lobe) / (4.0f * PI_F * r * r);
    float energy[MAUD_DIRECT_BANDS];
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        float air = ExpF(-2.0f * trace->air[b] * total);
        energy[b] = spread * (1.0f - s->material->absorption[b]) * path->energy[b] * air;
        histogram->energy[b][(uint32_t)bin] += energy[b];
    }
    if (trace->fieldOrder > 0 && bin < (float)trace->fieldBins)
    {
        Project(trace, s, energy, (uint32_t)bin, histogram->field);
    }
}

// Absorbs, moves and turns a path at its hit; false once it is spent.
static bool Bounce(const maudReverbTrace* trace, uint32_t bounce, const Shading* s, Path* path)
{
    path->distance += s->hitDistance;
    float left = 0.0f;
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        path->energy[b] *= 1.0f - s->material->absorption[b];
        left = fmaxf(left, path->energy[b] * ExpF(-2.0f * trace->air[b] * path->distance));
    }
    if (!(left > CUT_ENERGY) ||
        !(path->distance < SPEED_OF_SOUND * BIN_SECONDS * (float)MAUD_REVERB_BINS))
    {
        return false;
    }
    memcpy(path->origin, s->point, sizeof(path->origin));
    if (Random(path->ray, bounce, 0) < s->material->scattering)
    {
        Scatter(s->normal, Random(path->ray, bounce, 1), Random(path->ray, bounce, 2),
                path->direction);
        return true;
    }
    float d = 2.0f * Dot(path->direction, s->normal);
    for (int i = 0; i < 3; ++i)
    {
        path->direction[i] -= d * s->normal[i];
    }
    return true;
}

static maudRay RayOf(const Path* path)
{
    return (maudRay){{path->origin[0], path->origin[1], path->origin[2]},
                     {path->direction[0], path->direction[1], path->direction[2]},
                     GAP,
                     INFINITY};
}

// One bounce of a batch's live paths; returns how many stay alive.
static uint32_t Step(const maudReverbTrace* trace, uint32_t bounce, Path* paths, uint32_t count,
                     maudReverbHistogram* histogram)
{
    maudRay rays[MAUD_REVERB_BATCH];
    maudRayHit hits[MAUD_REVERB_BATCH];
    for (uint32_t i = 0; i < count; ++i)
    {
        rays[i] = RayOf(&paths[i]);
    }
    trace->closestHit(rays, count, hits, trace->context);
    Shading shading[MAUD_REVERB_BATCH];
    maudRay shadows[MAUD_REVERB_BATCH];
    uint8_t lit[MAUD_REVERB_BATCH];
    uint32_t which[MAUD_REVERB_BATCH];
    uint32_t shadowCount = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        paths[i].alive = Shade(trace, &paths[i], &rays[i], &hits[i], &shading[i]);
        const Shading* s = &shading[i];
        if (paths[i].alive && s->listenerDistance > GAP && Dot(s->normal, s->toListener) > 0.0f)
        {
            shadows[shadowCount] = (maudRay){{s->point[0], s->point[1], s->point[2]},
                                             {s->toListener[0], s->toListener[1], s->toListener[2]},
                                             GAP,
                                             s->listenerDistance};
            which[shadowCount++] = i;
        }
    }
    if (shadowCount > 0)
    {
        trace->anyHit(shadows, shadowCount, lit, trace->context);
    }
    for (uint32_t k = 0; k < shadowCount; ++k)
    {
        if (lit[k] == 0)
        {
            Gather(trace, &paths[which[k]], &shading[which[k]], histogram);
        }
    }
    // The live paths move to the front, in order.
    uint32_t live = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (paths[i].alive && Bounce(trace, bounce, &shading[i], &paths[i]))
        {
            paths[live++] = paths[i];
        }
    }
    return live;
}

void maudTraceReverbBatch(const maudReverbTrace* trace, uint32_t batch,
                          maudReverbHistogram* histogram)
{
    float* field = histogram->field;
    memset(histogram, 0, sizeof(*histogram));
    histogram->field = field;
    if (trace->fieldOrder > 0)
    {
        memset(field, 0,
               (size_t)Channels(trace) * MAUD_DIRECT_BANDS * trace->fieldBins * sizeof(float));
    }
    Path paths[MAUD_REVERB_BATCH];
    uint32_t first = batch * MAUD_REVERB_BATCH;
    uint32_t count =
        trace->rays - first < MAUD_REVERB_BATCH ? trace->rays - first : MAUD_REVERB_BATCH;
    for (uint32_t i = 0; i < count; ++i)
    {
        Start(trace, first + i, &paths[i]);
    }
    for (uint32_t bounce = 0; bounce < trace->maxBounces && count > 0; ++bounce)
    {
        count = Step(trace, bounce, paths, count, histogram);
    }
    histogram->truncated = INFINITY;
    for (uint32_t i = 0; i < count; ++i)
    {
        histogram->truncated = fminf(histogram->truncated, paths[i].distance / SPEED_OF_SOUND);
    }
}

// The backward integral of the bins and past them tail, and the
// crossings of -5 and -25 dB; false if it never falls 25 dB.
static bool Integrate(const double* energy, double tail, double* decay, uint32_t* start,
                      uint32_t* end)
{
    double sum = tail;
    for (uint32_t i = MAUD_REVERB_BINS; i-- > 0;)
    {
        sum += energy[i];
        decay[i] = sum;
    }
    *start = MAUD_REVERB_BINS;
    *end = MAUD_REVERB_BINS;
    for (uint32_t i = 0; i < MAUD_REVERB_BINS && *end == MAUD_REVERB_BINS; ++i)
    {
        *start = *start == MAUD_REVERB_BINS && decay[i] < sum * 0.31622776601683794 ? i : *start;
        *end = decay[i] < sum * 0.0031622776601683794 ? i : *end;
    }
    return *end < MAUD_REVERB_BINS;
}

// The least-squares slope, in dB per second, of levels[i] (dB) over
// bins [start, end), skipping those that are not finite.
static double Slope(const double* levels, uint32_t start, uint32_t end)
{
    double n = 0.0;
    double st = 0.0;
    double sd = 0.0;
    double stt = 0.0;
    double std = 0.0;
    for (uint32_t i = start; i < end; ++i)
    {
        if (isfinite(levels[i]))
        {
            double t = ((double)i + 0.5) * (double)BIN_SECONDS;
            n += 1.0;
            st += t;
            sd += levels[i];
            stt += t * t;
            std += t * levels[i];
        }
    }
    return n < 2.0 ? 0.0 : (n * std - st * sd) / (n * stt - st * st);
}

// Fills the bins from the cut on, and returns the energy past the last,
// at the rate the raw bins decay at between the integral's -5 dB and the
// cut (their levels' least-squares slope), from the level of the last
// bins (their mean, half their span before the cut). False if the bins
// do not decay.
static bool Extend(double* energy, uint32_t bins, double* tail)
{
    double decay[MAUD_REVERB_BINS];
    uint32_t start = 0;
    uint32_t end = 0;
    (void)Integrate(energy, 0.0, decay, &start, &end);
    double levels[MAUD_REVERB_BINS];
    for (uint32_t i = 0; i < bins; ++i)
    {
        levels[i] = 10.0 * maudLog10(energy[i]);
    }
    double slope = Slope(levels, start < bins ? start : 0, bins);
    if (!(slope < 0.0))
    {
        return false;
    }
    uint32_t span = bins < TAIL_BINS ? bins : TAIL_BINS;
    double level = 0.0;
    for (uint32_t i = bins - span; i < bins; ++i)
    {
        level += energy[i] / (double)span;
    }
    double rate = maudPow10(slope * (double)BIN_SECONDS / 10.0);
    double fill = level * maudPow(rate, ((double)span - 1.0) / 2.0);
    for (uint32_t i = bins; i < MAUD_REVERB_BINS; ++i)
    {
        fill *= rate;
        energy[i] = fill;
    }
    *tail = fill * rate / (1.0 - rate);
    return true;
}

// The decay's two-slope fit: times on a grid in log from MIN_TIME to
// MAX_TIME, coarse first, then finer around the best; at each pair the
// amplitudes by least squares in relative terms (each bin's error over
// the decay there), kept non-negative.
#define COARSE_TIMES 40u
#define FINE_STEPS   8u
// Two slopes where one misses by more than this (dB, RMS, to -40 dB)...
#define ONE_SLOPE_MISS 1.5
// ... two halve it, their times this far apart, the slower this share.
#define SLOPE_RATIO    1.5
#define MIN_TAIL_SHARE 0.001

typedef struct Decay
{
    // The decay (its backward integral, 1 at the start) and its inverse,
    // its bins down to -45 dB, and its crossing of -40 dB.
    const double* decay;
    const double* inverse;
    uint32_t bins;
    uint32_t end40;
} Decay;

typedef struct Slopes
{
    double fast;
    double slow;
    double fastAmount;
    double slowAmount;
} Slopes;

// The grid's time at a (fractional) coarse step.
static double TimeAt(double step)
{
    double span = maudLog((double)MAX_TIME / (double)MIN_TIME);
    return (double)MIN_TIME * maudExp(span * step / (double)(COARSE_TIMES - 1u));
}

// A slope's decay over one bin.
static double RateOf(double time)
{
    return maudExp(-13.815510557964274 * (double)BIN_SECONDS / time);
}

// The least squares of one (slow = 0) or two slopes: their amounts, and
// the residual; an infinite residual where an amount would be negative.
static double Solve(const Decay* d, Slopes* s)
{
    // Each slope's term at bin i over the decay there; a slope's value
    // goes down by its rate a bin.
    double fastRate = RateOf(s->fast);
    double slowRate = s->slow > 0.0 ? RateOf(s->slow) : 0.0;
    double aa = 0.0;
    double ab = 0.0;
    double bb = 0.0;
    double ay = 0.0;
    double by = 0.0;
    double fast = 1.0;
    double slow = s->slow > 0.0 ? 1.0 : 0.0;
    for (uint32_t i = 0; i < d->bins; ++i)
    {
        double a = fast * d->inverse[i];
        double b = slow * d->inverse[i];
        aa += a * a;
        ab += a * b;
        bb += b * b;
        ay += a;
        by += b;
        fast *= fastRate;
        slow *= slowRate;
    }
    if (s->slow > 0.0)
    {
        double det = aa * bb - ab * ab;
        if (!(det > 0.0))
        {
            return (double)INFINITY;
        }
        s->fastAmount = (ay * bb - by * ab) / det;
        s->slowAmount = (by * aa - ay * ab) / det;
    }
    else
    {
        s->fastAmount = ay / aa;
        s->slowAmount = 0.0;
    }
    if (s->fastAmount < 0.0 || s->slowAmount < 0.0)
    {
        return (double)INFINITY;
    }
    double residual = 0.0;
    fast = s->fastAmount;
    slow = s->slowAmount;
    for (uint32_t i = 0; i < d->bins; ++i)
    {
        double r = (fast + slow) * d->inverse[i] - 1.0;
        residual += r * r;
        fast *= fastRate;
        slow *= slowRate;
    }
    return residual;
}

// The best fit found so far, and where on the grid it lies.
typedef struct Best
{
    Slopes slopes;
    double residual;
    double fast;
    double slow;
} Best;

// Fits the slopes at grid steps fast and slow (none for one slope),
// keeping them if they fit better.
static void Try(const Decay* d, double fast, double slow, bool two, Best* best)
{
    Slopes s = {.fast = TimeAt(fast), .slow = two ? TimeAt(slow) : 0.0};
    double r = Solve(d, &s);
    if (r < best->residual)
    {
        *best = (Best){s, r, fast, slow};
    }
}

// The best one slope (two = false) or two, on the coarse grid and then
// within a coarse step of the best, FINE_STEPS to the step.
static Slopes Search(const Decay* d, bool two)
{
    Best best = {.residual = (double)INFINITY};
    for (uint32_t i = 0; i < COARSE_TIMES; ++i)
    {
        for (uint32_t j = two ? i + 1 : 0; j < (two ? COARSE_TIMES : 1u); ++j)
        {
            Try(d, (double)i, (double)j, two, &best);
        }
    }
    if (!(best.residual < (double)INFINITY))
    {
        return best.slopes;
    }
    double fine = 1.0 / (double)FINE_STEPS;
    double fastAt = best.fast;
    double slowAt = best.slow;
    int span = (int)FINE_STEPS;
    for (int i = -span; i <= span; ++i)
    {
        for (int j = two ? -span : 0; j <= (two ? span : 0); ++j)
        {
            double fast = fastAt + i * fine;
            double slow = slowAt + j * fine;
            bool inside =
                fast >= 0.0 && slow <= (double)(COARSE_TIMES - 1u) && (!two || slow > fast);
            if (inside)
            {
                Try(d, fast, slow, two, &best);
            }
        }
    }
    return best.slopes;
}

// How far the slopes' decay is from the decay down to -40 dB (dB, RMS).
static double Miss(const Decay* d, const Slopes* s)
{
    double sum = 0.0;
    for (uint32_t i = 0; i < d->end40; ++i)
    {
        double t = (double)i * (double)BIN_SECONDS;
        double model = s->fastAmount * maudExp(-13.815510557964274 * t / s->fast);
        model += s->slow > 0.0 ? s->slowAmount * maudExp(-13.815510557964274 * t / s->slow) : 0.0;
        double r = 10.0 * maudLog10(model / d->decay[i]);
        sum += r * r;
    }
    return d->end40 > 0 ? sqrt(sum / (double)d->end40) : 0.0;
}

// Two slopes in place of the one fitted from -5 to -25 dB, where the
// decay asks for them (the conditions above): the fit's band takes them.
static void FitTail(const double* decay, uint32_t band, maudReverbFit* fit)
{
    double inverse[MAUD_REVERB_BINS];
    Decay d = {
        .decay = decay, .inverse = inverse, .bins = MAUD_REVERB_BINS, .end40 = MAUD_REVERB_BINS};
    for (uint32_t i = 0; i < MAUD_REVERB_BINS; ++i)
    {
        inverse[i] = decay[i] > 0.0 ? 1.0 / decay[i] : 0.0;
        d.end40 = d.end40 == MAUD_REVERB_BINS && decay[i] < 1e-4 ? i : d.end40;
        if (decay[i] < 3.1622776601683794e-5)
        {
            d.bins = i;
            break;
        }
    }
    if (d.bins < 10u || d.end40 < 2u)
    {
        return;
    }
    Slopes one = Search(&d, false);
    Slopes two = Search(&d, true);
    if (!(one.fast > 0.0) || !(two.fast > 0.0))
    {
        return;
    }
    double missOne = Miss(&d, &one);
    double missTwo = Miss(&d, &two);
    double share = two.slowAmount / (two.fastAmount + two.slowAmount);
    if (missOne > ONE_SLOPE_MISS && missTwo <= missOne / 2.0 &&
        two.slow >= SLOPE_RATIO * two.fast && share >= MIN_TAIL_SHARE)
    {
        fit->times[band] = (float)two.fast;
        fit->tailTimes[band] = (float)two.slow;
        fit->tailShares[band] = (float)share;
    }
}

// A band's time from its first bins of energy (all of them unless rays
// were cut short, the rest then filled in by Extend), and its two slopes
// where it has them.
static float Fit(const float* energy, uint32_t bins, uint32_t band, maudReverbFit* fit)
{
    double extended[MAUD_REVERB_BINS];
    double total = 0.0;
    for (uint32_t i = 0; i < MAUD_REVERB_BINS; ++i)
    {
        extended[i] = i < bins ? (double)energy[i] : 0.0;
        total += extended[i];
    }
    if (!(total > 0.0))
    {
        return MIN_TIME;
    }
    double tail = 0.0;
    if (bins < MAUD_REVERB_BINS && !Extend(extended, bins, &tail))
    {
        return MAX_TIME;
    }
    double decay[MAUD_REVERB_BINS];
    uint32_t start = 0;
    uint32_t end = 0;
    if (!Integrate(extended, tail, decay, &start, &end))
    {
        return MAX_TIME;
    }
    if (end < start + 2)
    {
        return MIN_TIME;
    }
    double normalized[MAUD_REVERB_BINS];
    for (uint32_t i = 0; i < MAUD_REVERB_BINS; ++i)
    {
        normalized[i] = decay[i] / decay[0];
    }
    for (uint32_t i = start; i < end; ++i)
    {
        decay[i] = 10.0 * maudLog10(normalized[i]);
    }
    double slope = Slope(decay, start, end);
    float time = slope < 0.0 ? (float)fmin(fmax(-60.0 / slope, (double)MIN_TIME), (double)MAX_TIME)
                             : MAX_TIME;
    fit->times[band] = time;
    FitTail(normalized, band, fit);
    return fit->times[band];
}

void maudFitReverb(maudReverbHistogram* histograms, uint32_t count, maudReverbFit* fit)
{
    *fit = (maudReverbFit){0};
    for (uint32_t h = 1; h < count; ++h)
    {
        for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
        {
            for (uint32_t i = 0; i < MAUD_REVERB_BINS; ++i)
            {
                histograms[0].energy[b][i] += histograms[h].energy[b][i];
            }
        }
    }
    float truncated = INFINITY;
    for (uint32_t h = 0; h < count; ++h)
    {
        truncated = fminf(truncated, histograms[h].truncated);
    }
    float cut = floorf(truncated / BIN_SECONDS);
    uint32_t bins = cut < (float)MAUD_REVERB_BINS ? (uint32_t)cut : MAUD_REVERB_BINS;
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        fit->times[b] = Fit(histograms[0].energy[b], bins, (uint32_t)b, fit);
    }
}

void maudSumReverbFields(const maudReverbTrace* trace, maudReverbHistogram* histograms,
                         uint32_t count)
{
    size_t values = (size_t)Channels(trace) * MAUD_DIRECT_BANDS * trace->fieldBins;
    for (uint32_t h = 1; h < count && trace->fieldOrder > 0; ++h)
    {
        for (size_t i = 0; i < values; ++i)
        {
            histograms[0].field[i] += histograms[h].field[i];
        }
    }
}

// The level (dB, -96 to 24) that makes the reverb at time at, decaying
// in time, give energy traced (there, per 10 ms bin).
static float LevelOf(double traced, double time, float at, float delay)
{
    // The reverb's W energy per 10 ms bin at its start, for a unit
    // impulse (measured, the same for every time and rate).
    const double start = 0.0144;
    double rate = 13.815510557964274 / time;
    double reverb = start * maudExp(-rate * ((double)at - (double)delay));
    double db = traced > 0.0 ? 10.0 * maudLog10(traced / reverb) : -96.0;
    return (float)fmin(fmax(db, -96.0), 24.0);
}

void maudReverbLevels(const maudReverbHistogram* summed, const maudReverbFit* fit, float at,
                      float delay, float* levels, float* tailLevels)
{
    uint32_t last = (uint32_t)lround((double)at / (double)BIN_SECONDS);
    last = last < 1 ? 1 : last > MAUD_REVERB_BINS ? MAUD_REVERB_BINS : last;
    uint32_t first = last > 5 ? last - 5 : 0;
    double centre = 0.5 * (double)(first + last) * (double)BIN_SECONDS;
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        double mean = 0.0;
        for (uint32_t i = first; i < last; ++i)
        {
            mean += (double)summed->energy[b][i] / (double)(last - first);
        }
        double time = (double)fit->times[b];
        double tail = (double)fit->tailTimes[b];
        // The energy at the centre splits as the slopes' densities do
        // there (an amount A of the decay at rate k gives A k exp(-k t)).
        double share = 0.0;
        if (tail > 0.0)
        {
            double tailAmount = (double)fit->tailShares[b];
            double fastRate = 13.815510557964274 / time;
            double tailRate = 13.815510557964274 / tail;
            double fast = (1.0 - tailAmount) * fastRate * maudExp(-fastRate * centre);
            double slow = tailAmount * tailRate * maudExp(-tailRate * centre);
            share = slow / (fast + slow);
        }
        double rate = 13.815510557964274 / time;
        levels[b] =
            LevelOf(mean * (1.0 - share) * maudExp(-rate * ((double)at - centre)), time, at, delay);
        tailLevels[b] = -96.0f;
        if (tail > 0.0)
        {
            double tailRate = 13.815510557964274 / tail;
            tailLevels[b] =
                LevelOf(mean * share * maudExp(-tailRate * ((double)at - centre)), tail, at, delay);
        }
    }
}
