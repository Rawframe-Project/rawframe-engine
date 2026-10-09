// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The three-band equalizer (band_eq.h). The filters are the bilinear
// transforms of the analog prototypes (shelves of slope 1, a bell with
// the middle band's Q), realized as Simper's linear trapezoidal
// state-variable filters; their digital magnitude at a frequency is the
// prototype's at the prewarped ratio x = tan(pi f / rate) / tan(pi f0 /
// rate), which is how band means are taken here. Bands: 20 Hz to 800 Hz,
// 800 Hz to 8 kHz, 8 kHz to 20 kHz (or 0.45 of the rate, if lower).

#include "band_eq.h"

#include <math.h>

#define PI_D        3.14159265358979323846
#define LOW_EDGE    800.0
#define HIGH_EDGE   8000.0
#define SHELF_Q     0.70710678118654752
#define PROBE_DB    (-6.0)
#define REFINEMENTS 3
// A ramp's segment: its coefficients change every this many frames.
#define RAMP_FRAMES 8u

typedef enum Kind
{
    kind_lowShelf,
    kind_bell,
    kind_highShelf,
} Kind;

static double BellFrequency(void)
{
    return sqrt(LOW_EDGE * HIGH_EDGE);
}

static double BellQ(void)
{
    return BellFrequency() / (HIGH_EDGE - LOW_EDGE);
}

void maudSetupBandEq(maudBandEqSetup* setup, float sampleRate)
{
    double rate = (double)sampleRate;
    setup->sampleRate = sampleRate;
    double centres[MAUD_BANDS] = {LOW_EDGE, BellFrequency(), HIGH_EDGE};
    double top = fmin(20000.0, 0.45 * rate);
    double edges[MAUD_BANDS][2] = {{20.0, LOW_EDGE}, {LOW_EDGE, HIGH_EDGE}, {HIGH_EDGE, top}};
    for (int b = 0; b < MAUD_BANDS; ++b)
    {
        setup->warped[b] = tan(PI_D * centres[b] / rate);
        // Midpoints of a log grid over the band.
        double ratio = pow(edges[b][1] / edges[b][0], 1.0 / MAUD_BAND_POINTS);
        for (int p = 0; p < MAUD_BAND_POINTS; ++p)
        {
            double f = edges[b][0] * pow(ratio, (double)p + 0.5);
            setup->points[b][p] = tan(PI_D * f / rate);
        }
    }
    // The interaction matrix: each filter alone at the probe gain, its
    // effect on each band's mean per dB; then its inverse by cofactors.
    double m[MAUD_BANDS][MAUD_BANDS];
    for (int j = 0; j < MAUD_BANDS; ++j)
    {
        double gains[MAUD_BANDS] = {0.0, 0.0, 0.0};
        gains[j] = PROBE_DB;
        double means[MAUD_BANDS];
        maudBandEqMeans(setup, gains, means);
        for (int i = 0; i < MAUD_BANDS; ++i)
        {
            m[i][j] = means[i] / PROBE_DB;
        }
    }
    double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                 m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                 m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    for (int i = 0; i < MAUD_BANDS; ++i)
    {
        for (int j = 0; j < MAUD_BANDS; ++j)
        {
            // The cofactor of m[j][i] over the determinant.
            int r0 = (j + 1) % 3;
            int r1 = (j + 2) % 3;
            int c0 = (i + 1) % 3;
            int c1 = (i + 2) % 3;
            setup->inverse[i][j] = (m[r0][c0] * m[r1][c1] - m[r0][c1] * m[r1][c0]) / det;
        }
    }
}

// The squared magnitude of a prototype with gain A = 10^(dB / 40) at the
// ratio x.
static double Power(Kind kind, double a, double x)
{
    double x2 = x * x;
    if (kind == kind_bell)
    {
        double q = BellQ();
        double n = (1.0 - x2) * (1.0 - x2) + (a * x / q) * (a * x / q);
        double d = (1.0 - x2) * (1.0 - x2) + (x / (a * q)) * (x / (a * q));
        return n / d;
    }
    double s = sqrt(a) * x / SHELF_Q;
    double low = (a - x2) * (a - x2) + s * s;
    double high = (1.0 - a * x2) * (1.0 - a * x2) + s * s;
    return kind == kind_lowShelf ? a * a * low / high : a * a * high / low;
}

void maudBandEqMeans(const maudBandEqSetup* setup, const double gains[MAUD_BANDS],
                     double means[MAUD_BANDS])
{
    double a[MAUD_BANDS];
    for (int i = 0; i < MAUD_BANDS; ++i)
    {
        a[i] = pow(10.0, gains[i] / 40.0);
    }
    for (int b = 0; b < MAUD_BANDS; ++b)
    {
        double sum = 0.0;
        for (int p = 0; p < MAUD_BAND_POINTS; ++p)
        {
            double power = 1.0;
            for (int i = 0; i < MAUD_BANDS; ++i)
            {
                power *= Power((Kind)i, a[i], setup->points[b][p] / setup->warped[i]);
            }
            sum += 10.0 * log10(power);
        }
        means[b] = sum / MAUD_BAND_POINTS;
    }
}

static void Apply(const double inverse[MAUD_BANDS][MAUD_BANDS], const double* error, double* gains)
{
    for (int i = 0; i < MAUD_BANDS; ++i)
    {
        for (int j = 0; j < MAUD_BANDS; ++j)
        {
            gains[i] += inverse[i][j] * error[j];
        }
    }
}

void maudSolveBandEq(const maudBandEqSetup* setup, const double targets[MAUD_BANDS],
                     double gains[MAUD_BANDS])
{
    for (int i = 0; i < MAUD_BANDS; ++i)
    {
        gains[i] = 0.0;
    }
    Apply(setup->inverse, targets, gains);
    for (int step = 0; step < REFINEMENTS; ++step)
    {
        double means[MAUD_BANDS];
        maudBandEqMeans(setup, gains, means);
        double error[MAUD_BANDS];
        for (int b = 0; b < MAUD_BANDS; ++b)
        {
            error[b] = targets[b] - means[b];
        }
        Apply(setup->inverse, error, gains);
    }
}

void maudDesignBandEq(const maudBandEqSetup* setup, const double gains[MAUD_BANDS],
                      maudBandEqFilters* filters)
{
    for (int i = 0; i < MAUD_BANDS; ++i)
    {
        double a = pow(10.0, gains[i] / 40.0);
        double g = setup->warped[i];
        double k = 1.0 / SHELF_Q;
        double m0 = 1.0;
        double m1 = 0.0;
        double m2 = 0.0;
        if (i == kind_lowShelf)
        {
            g /= sqrt(a);
            m1 = k * (a - 1.0);
            m2 = a * a - 1.0;
        }
        else if (i == kind_bell)
        {
            k = 1.0 / (BellQ() * a);
            m1 = k * (a * a - 1.0);
        }
        else
        {
            g *= sqrt(a);
            m0 = a * a;
            m1 = k * (1.0 - a) * a;
            m2 = 1.0 - a * a;
        }
        filters->g[i] = (float)g;
        filters->k[i] = (float)k;
        filters->m0[i] = (float)m0;
        filters->m1[i] = (float)m1;
        filters->m2[i] = (float)m2;
    }
}

// All three filters per sample: their recursions are independent across
// samples, so the processor overlaps one filter's work on a sample with
// the next filter's on the sample before.
static void RunSteady(maudBandEqState* state, const maudBandEqFilters* f, const float* in,
                      float* out, uint32_t frames)
{
    float a1[MAUD_BANDS];
    float a2[MAUD_BANDS];
    float a3[MAUD_BANDS];
    float ic1[MAUD_BANDS];
    float ic2[MAUD_BANDS];
    for (int i = 0; i < MAUD_BANDS; ++i)
    {
        a1[i] = 1.0f / (1.0f + f->g[i] * (f->g[i] + f->k[i]));
        a2[i] = f->g[i] * a1[i];
        a3[i] = f->g[i] * a2[i];
        ic1[i] = state->ic1[i];
        ic2[i] = state->ic2[i];
    }
    for (uint32_t n = 0; n < frames; ++n)
    {
        float v0 = in[n];
        for (int i = 0; i < MAUD_BANDS; ++i)
        {
            float v3 = v0 - ic2[i];
            float v1 = a1[i] * ic1[i] + a2[i] * v3;
            float v2 = ic2[i] + a2[i] * ic1[i] + a3[i] * v3;
            ic1[i] = 2.0f * v1 - ic1[i];
            ic2[i] = 2.0f * v2 - ic2[i];
            v0 = f->m0[i] * v0 + f->m1[i] * v1 + f->m2[i] * v2;
        }
        out[n] = v0;
    }
    for (int i = 0; i < MAUD_BANDS; ++i)
    {
        state->ic1[i] = ic1[i];
        state->ic2[i] = ic2[i];
    }
}

void maudRunBandEq(maudBandEqState* state, const maudBandEqFilters* from,
                   const maudBandEqFilters* to, const float* in, float* out, uint32_t frames)
{
    if (from == to)
    {
        RunSteady(state, to, in, out, frames);
        return;
    }
    // Segments of RAMP_FRAMES, each run with the coefficients at its end.
    for (uint32_t start = 0; start < frames; start += RAMP_FRAMES)
    {
        uint32_t count = frames - start < RAMP_FRAMES ? frames - start : RAMP_FRAMES;
        float t = (float)(start + count) / (float)frames;
        maudBandEqFilters at;
        for (int i = 0; i < MAUD_BANDS; ++i)
        {
            at.g[i] = from->g[i] + t * (to->g[i] - from->g[i]);
            at.k[i] = from->k[i] + t * (to->k[i] - from->k[i]);
            at.m0[i] = from->m0[i] + t * (to->m0[i] - from->m0[i]);
            at.m1[i] = from->m1[i] + t * (to->m1[i] - from->m1[i]);
            at.m2[i] = from->m2[i] + t * (to->m2[i] - from->m2[i]);
        }
        RunSteady(state, &at, in + start, out + start, count);
    }
}
