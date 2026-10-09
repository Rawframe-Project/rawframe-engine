// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Air absorption by ISO 9613 part 1 (pure-tone atmospheric attenuation from
// the relaxation of oxygen and nitrogen), averaged in dB over each band
// on a log grid of 64 midpoints: 20 Hz to 800 Hz, 800 Hz to 8 kHz, 8 kHz
// to 20 kHz, at 101.325 kPa.

#include "air_absorption.h"

#include "portable_math.h"

#include "maul-audio/direct.h"

#include <math.h>

#define POINTS 64

// The standard's attenuation in dB per metre at f Hz, temperature t
// kelvin and relative humidity rh percent, at the reference pressure.
static double Attenuation(double f, double t, double rh)
{
    const double t0 = 293.15;
    const double t01 = 273.16;
    double psat = maudPow(10.0, -6.8346 * maudPow(t01 / t, 1.261) + 4.6151);
    double h = rh * psat;
    double frO = 24.0 + 4.04e4 * h * (0.02 + h) / (0.391 + h);
    double frN = maudPow(t / t0, -0.5) *
                 (9.0 + 280.0 * h * maudExp(-4.170 * (maudPow(t / t0, -1.0 / 3.0) - 1.0)));
    return 8.686 * f * f *
           (1.84e-11 * sqrt(t / t0) +
            maudPow(t / t0, -2.5) * (0.01275 * maudExp(-2239.1 / t) / (frO + f * f / frO) +
                                     0.1068 * maudExp(-3352.0 / t) / (frN + f * f / frN)));
}

void maudAirAbsorptionOf(double celsius, double humidity, float* absorption)
{
    const double edges[MAUD_DIRECT_BANDS + 1] = {20.0, 800.0, 8000.0, 20000.0};
    double t = celsius + 273.15;
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        double ratio = maudPow(edges[b + 1] / edges[b], 1.0 / POINTS);
        double sum = 0.0;
        for (int p = 0; p < POINTS; ++p)
        {
            sum += Attenuation(edges[b] * maudPow(ratio, (double)p + 0.5), t, humidity);
        }
        // dB per metre to an amplitude exponent per metre.
        absorption[b] = (float)(sum / POINTS * maudLog(10.0) / 20.0);
    }
}

maudResult maudGetAirAbsorption(float temperatureCelsius, float humidityPercent,
                                float* absorptionOut)
{
    if (absorptionOut == nullptr || !(temperatureCelsius >= -20.0f) ||
        !(temperatureCelsius <= 50.0f) || !(humidityPercent >= 10.0f) ||
        !(humidityPercent <= 100.0f))
    {
        return maud_errorInvalid;
    }
    maudAirAbsorptionOf((double)temperatureCelsius, (double)humidityPercent, absorptionOut);
    return maud_success;
}
