// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The octave equalizer (octave_eq.h). The bells are the audio EQ
// cookbook's peaking filters; the shelf is the bilinear transform of a
// first-order high shelf. Everything fixed per rate (the design points'
// cosines, each bell's centre) is worked out once in the setup.

#include "octave_eq.h"

#include "least_squares.h"

#include <math.h>

#define PI_D      3.14159265358979323846
#define BANDWIDTH 1.5
#define PROBE_DB  (-6.0)
#define SHELF_HZ  5000.0

double maudOctaveCentre(int octave)
{
    return 31.25 * pow(2.0, (double)octave);
}

static double BellQ(void)
{
    return sqrt(pow(2.0, BANDWIDTH)) / (pow(2.0, BANDWIDTH) - 1.0);
}

static maudBiquad Bell(double c, double alpha, double db)
{
    double a = pow(10.0, db / 40.0);
    double a0 = 1.0 + alpha / a;
    return (maudBiquad){(float)((1.0 + alpha * a) / a0), (float)(-2.0 * c / a0),
                        (float)((1.0 - alpha * a) / a0), (float)(-2.0 * c / a0),
                        (float)((1.0 - alpha / a) / a0)};
}

static maudBiquad Shelf(double rate, double db)
{
    double t = tan(PI_D * SHELF_HZ / rate);
    double g = pow(10.0, db / 20.0);
    return (maudBiquad){(float)((t + g) / (t + 1.0)), (float)((t - g) / (t + 1.0)), 0.0f,
                        (float)((t - 1.0) / (t + 1.0)), 0.0f};
}

// A biquad's magnitude in dB from the cosines of w and 2w.
static double Db(const maudBiquad* f, double c1, double c2)
{
    // |b0 + b1 z^-1 + b2 z^-2|^2 over |1 + a1 z^-1 + a2 z^-2|^2.
    double b0 = (double)f->b0;
    double b1 = (double)f->b1;
    double b2 = (double)f->b2;
    double a1 = (double)f->a1;
    double a2 = (double)f->a2;
    double num = b0 * b0 + b1 * b1 + b2 * b2 + 2.0 * (b0 * b1 + b1 * b2) * c1 + 2.0 * b0 * b2 * c2;
    double den = 1.0 + a1 * a1 + a2 * a2 + 2.0 * (a1 + a1 * a2) * c1 + 2.0 * a2 * c2;
    return 10.0 * log10(num / den);
}

void maudSetupOctaveEq(maudOctaveEqSetup* setup, double rate)
{
    setup->rate = rate;
    for (int p = 0; p < MAUD_OCTAVE_POINTS; ++p)
    {
        // Even points are the centres, odd ones lie between.
        double hz = maudOctaveCentre(p / 2) * (p % 2 == 1 ? sqrt(2.0) : 1.0);
        setup->points[p] = 2.0 * PI_D * hz / rate;
        setup->cos1[p] = cos(setup->points[p]);
        setup->cos2[p] = cos(2.0 * setup->points[p]);
    }
    for (int m = 0; m < MAUD_OCTAVES; ++m)
    {
        double w = 2.0 * PI_D * maudOctaveCentre(m) / rate;
        setup->bellCos[m] = cos(w);
        setup->bellAlpha[m] = sin(w) / (2.0 * BellQ());
        maudBiquad bell = Bell(setup->bellCos[m], setup->bellAlpha[m], PROBE_DB);
        for (int p = 0; p < MAUD_OCTAVE_POINTS; ++p)
        {
            setup->basis[p][m] = Db(&bell, setup->cos1[p], setup->cos2[p]) / PROBE_DB;
        }
    }
}

static double Median(const double* values)
{
    double sorted[MAUD_OCTAVES];
    for (int i = 0; i < MAUD_OCTAVES; ++i)
    {
        int j = i;
        for (; j > 0 && sorted[j - 1] > values[i]; --j)
        {
            sorted[j] = sorted[j - 1];
        }
        sorted[j] = values[i];
    }
    return 0.5 * (sorted[MAUD_OCTAVES / 2 - 1] + sorted[MAUD_OCTAVES / 2]);
}

// The targets at the design points: the centres', and between two
// centres their mean (linear interpolation in log frequency).
static double AtPoint(const double* targets, int p)
{
    return p % 2 == 0 ? targets[p / 2] : 0.5 * (targets[p / 2] + targets[p / 2 + 1]);
}

bool maudFitOctaveEq(const maudOctaveEqSetup* setup, const double* targets, double* solve)
{
    double weights[MAUD_OCTAVE_POINTS];
    for (int p = 0; p < MAUD_OCTAVE_POINTS; ++p)
    {
        double target = AtPoint(targets, p);
        // Squared residuals weighted by 1 / target^2: relative error.
        weights[p] = 1.0 / (target * target);
    }
    return maudWeightedPseudoInverse(&setup->basis[0][0], weights, MAUD_OCTAVE_POINTS, MAUD_OCTAVES,
                                     solve);
}

void maudDesignOctaveEq(const maudOctaveEqSetup* setup, const double* solve, const double* targets,
                        maudBiquad* filters, float* gain)
{
    double median = Median(targets);
    // The shelf first; the bells fit what it leaves. Added after the fit
    // instead, as Prawda et al. do, it deepens the loss toward 8 kHz and
    // shortens the time there by up to a fifth. Inside the fit, a low
    // corner (4 to 6 kHz measured best) lets the shelf carry the broad
    // fall of the highs and the bells the detail: times at the octave
    // centres within 2 % against 5 % with the corner at 14 kHz.
    filters[MAUD_OCTAVES] = Shelf(setup->rate, targets[MAUD_OCTAVES - 1] - median);
    double remainder[MAUD_OCTAVE_POINTS];
    for (int p = 0; p < MAUD_OCTAVE_POINTS; ++p)
    {
        remainder[p] = AtPoint(targets, p) - median -
                       Db(&filters[MAUD_OCTAVES], setup->cos1[p], setup->cos2[p]);
    }
    for (int m = 0; m < MAUD_OCTAVES; ++m)
    {
        double g = 0.0;
        for (int p = 0; p < MAUD_OCTAVE_POINTS; ++p)
        {
            g += solve[m * MAUD_OCTAVE_POINTS + p] * remainder[p];
        }
        filters[m] = Bell(setup->bellCos[m], setup->bellAlpha[m], g);
    }
    *gain = (float)pow(10.0, median / 20.0);
}
