// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An octave graphic equalizer for feedback delay network lines, after
// Prawda, Valimaki and Schlecht (2019): ten peak-notch filters at
// 31.25 Hz to 16 kHz, 1.5 octaves wide, and a first-order high shelf at
// 5 kHz carrying the top band's loss; the targets are shifted by their
// median, which returns as a broadband gain, and the bells' gains are
// fitted by least squares to what the shelf leaves, at the centres and
// the points between them, with weights that make the error relative in
// reverberation time.

#ifndef MAUL_AUDIO_SRC_OCTAVE_EQ_H
#define MAUL_AUDIO_SRC_OCTAVE_EQ_H

#include <stdint.h>

#define MAUD_OCTAVES       10
#define MAUD_OCTAVE_POINTS 19
// The bells, then the shelf.
#define MAUD_OCTAVE_FILTERS 11

// A biquad in transposed direct form II, normalized (a0 = 1).
typedef struct maudBiquad
{
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
} maudBiquad;

typedef struct maudOctaveEqSetup
{
    double rate;
    // The design points' angular frequencies, and their cosines and
    // those of twice them.
    double points[MAUD_OCTAVE_POINTS];
    double cos1[MAUD_OCTAVE_POINTS];
    double cos2[MAUD_OCTAVE_POINTS];
    // Each bell's cosine and alpha (sin w / 2Q) at its centre.
    double bellCos[MAUD_OCTAVES];
    double bellAlpha[MAUD_OCTAVES];
    // Each bell's response in dB at each point per dB of gain.
    double basis[MAUD_OCTAVE_POINTS][MAUD_OCTAVES];
} maudOctaveEqSetup;

// The octave centres, 31.25 Hz times powers of 2.
double maudOctaveCentre(int octave);

// Fills setup for a rate from 44,100 to 384,000 Hz.
void maudSetupOctaveEq(maudOctaveEqSetup* setup, double rate);

// The weighted least-squares map for targets of this shape (per-octave
// dB, each below 0): solve, MAUD_OCTAVES by MAUD_OCTAVE_POINTS. Targets
// scaled by any positive factor share it. False if the fit fails.
bool maudFitOctaveEq(const maudOctaveEqSetup* setup, const double* targets, double* solve);

// The filters meeting the targets through solve (from maudFitOctaveEq
// on targets of the same shape), and the broadband gain with them.
void maudDesignOctaveEq(const maudOctaveEqSetup* setup, const double* solve, const double* targets,
                        maudBiquad* filters, float* gain);

#endif // MAUL_AUDIO_SRC_OCTAVE_EQ_H
