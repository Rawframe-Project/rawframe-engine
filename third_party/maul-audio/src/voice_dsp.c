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

// The series' coefficients, (-1)^(k + 1) / (k k!) for k = 1 to 29.
static const double s_series[29] = {
    1,
    -0.25,
    0.055555555555555552,
    -0.010416666666666666,
    0.0016666666666666668,
    -0.00023148148148148149,
    2.834467120181406e-05,
    -3.1001984126984127e-06,
    3.0619243582206544e-07,
    -2.7557319223985891e-08,
    2.27746439867652e-09,
    -1.7397297489890083e-10,
    1.2353110643708935e-11,
    -8.1933897126640886e-13,
    5.0981091545465446e-14,
    -2.9871733327421158e-15,
    1.6537983849091297e-16,
    -8.6773372047701253e-18,
    4.326650129802279e-19,
    -2.0551588116560825e-20,
    9.3204481254244102e-22,
    -4.0439960874775335e-23,
    1.6818131176655147e-24,
    -6.7155732129004933e-26,
    2.5787801137537893e-27,
    -9.5369087047107595e-29,
    3.4013666162205723e-30,
    -1.1713890132392279e-31,
    3.8999872022233505e-33,
};

// The series' degree for x below a limit: the fewest terms whose next
// is under 1e-17 of E1 at the limit.
static int Degree(double x)
{
    return x < 1e-4 ? 3 : x < 0.01 ? 6 : x < 0.1 ? 9 : x < 1.0 ? 18 : 29;
}

// The exponential integral E1(x) for x > 0. Below 3, its series
// -gamma - ln x + sum of (-1)^(k + 1) x^k / (k k!), by Horner's rule at
// the degree its range needs: no division and no test per term. From 3,
// its continued fraction (as Numerical Recipes' expint, n = 1), which
// converges there in a few steps. Within 2e-13 of its value throughout.
double maudExpIntegral(double x)
{
    if (x < 3.0)
    {
        int n = Degree(x);
        double sum = s_series[n - 1];
        for (int k = n - 1; k >= 1; --k)
        {
            sum = sum * x + s_series[k - 1];
        }
        return -0.57721566490153286 - log(x) + sum * x;
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
