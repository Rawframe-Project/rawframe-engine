// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The near-field filter of one ear: how a rigid sphere of the head's
// radius changes the sound of a source at one distance against a source
// at the distance the HRTF set was measured at, as a gain and a
// first-order high shelf. Only the head's effect: the 1/r change of
// pressure is the host's distance attenuation.

#ifndef MAUL_AUDIO_SRC_NEAR_FIELD_H
#define MAUL_AUDIO_SRC_NEAR_FIELD_H

#include "maul-audio/base.h"

// y[n] = b0 x[n] + b1 x[n - 1] - a1 y[n - 1]; |a1| < 1.
typedef struct maudNearFieldFilter
{
    float b0;
    float b1;
    float a1;
} maudNearFieldFilter;

// The filter for an incidence angle in degrees (the angle at the head's
// centre between the ear and the source, 0 to 180), the source's and
// the set's distances as the head radius over the distance (0 for
// infinity; past 1 / 1.15 counts as 1 / 1.15), a head radius in metres
// and a sample rate. Equal distances give exactly the identity.
maudNearFieldFilter maudNearField(float angleDegrees, float inverseDistance,
                                  float setInverseDistance, float headRadius, float sampleRate);

// The direction, from the head's centre, at which an ear's response is
// looked up for a source at position (metres, the listener's frame):
// where the ray from the ear, at earX on the x axis, through the source
// meets the sphere the set was measured on (parallax). Unit length. A
// source at the ear, or an ear outside the sphere, looks up the
// source's own direction; a zero position, straight ahead.
maudVector3 maudEarDirection(maudVector3 position, float earX, float setDistance);

#endif // MAUL_AUDIO_SRC_NEAR_FIELD_H
