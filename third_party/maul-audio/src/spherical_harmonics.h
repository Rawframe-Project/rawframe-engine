// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Real spherical harmonics as AmbiX uses them: SN3D normalization, no
// Condon-Shortley phase, ACN order, the field's axes x ahead, y left and
// z up.

#ifndef MAUL_AUDIO_SRC_SPHERICAL_HARMONICS_H
#define MAUL_AUDIO_SRC_SPHERICAL_HARMONICS_H

#include "maul-audio/base.h"

#include <stdint.h>

// A listener-frame direction (+x right, +y up, -z ahead) as a unit
// vector in the field's axes; straight ahead for a zero vector.
void maudFieldAxes(maudVector3 v, float* x, float* y, float* z);

// The (order + 1)^2 harmonics, up to order 3, of a unit direction in the
// field's axes, into g.
void maudSphericalHarmonics(uint32_t order, float x, float y, float z, float* g);

#endif // MAUL_AUDIO_SRC_SPHERICAL_HARMONICS_H
