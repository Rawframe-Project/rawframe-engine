// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Biquad design from Robert Bristow-Johnson's "Audio EQ Cookbook", and
// levels in dBFS.

#include "voice_dsp.h"

#include <math.h>

#define PI 3.14159265358979323846

// Normalizes and stores coefficients given a0.
static void Store(maudBiquad* filter, double b0, double b1, double b2, double a0, double a1,
                  double a2)
{
    *filter = (maudBiquad){
        .b0 = (float)(b0 / a0),
        .b1 = (float)(b1 / a0),
        .b2 = (float)(b2 / a0),
        .a1 = (float)(a1 / a0),
        .a2 = (float)(a2 / a0),
    };
}

void maudDesignHighPass(maudBiquad* filter, double frequency, double sampleRate)
{
    double w = 2.0 * PI * frequency / sampleRate;
    double alpha = sin(w) / (2.0 * sqrt(0.5));
    double c = cos(w);
    Store(filter, (1.0 + c) / 2.0, -(1.0 + c), (1.0 + c) / 2.0, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
}

void maudDesignBandPass(maudBiquad* filter, double low, double high, double sampleRate)
{
    double centre = sqrt(low * high);
    double q = centre / (high - low);
    double w = 2.0 * PI * centre / sampleRate;
    double alpha = sin(w) / (2.0 * q);
    double c = cos(w);
    Store(filter, alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
}

float maudLevelDbfs(double meanSquare)
{
    float level = (float)(10.0 * log10(meanSquare + 1e-12));
    return level > MAUD_FLOOR_DBFS ? level : MAUD_FLOOR_DBFS;
}

// The exponential integral E1(x) for x > 0: its series below 1, its
// continued fraction above (as Numerical Recipes' expint, n = 1).
double maudExpIntegral(double x)
{
    if (x < 1.0)
    {
        double sum = 0.0;
        double term = 1.0;
        for (int k = 1; k < 40; ++k)
        {
            term *= -x / k;
            sum -= term / k;
            if (fabs(term) < 1e-12 * fabs(sum))
            {
                break;
            }
        }
        return -0.57721566490153286 - log(x) + sum;
    }
    double b = x + 1.0;
    double c = 1e300;
    double d = 1.0 / b;
    double h = d;
    for (int i = 1; i < 100; ++i)
    {
        double a = -(double)i * i;
        b += 2.0;
        d = 1.0 / (a * d + b);
        c = b + a / c;
        double delta = c * d;
        h *= delta;
        if (fabs(delta - 1.0) < 1e-12)
        {
            break;
        }
    }
    return h * exp(-x);
}
