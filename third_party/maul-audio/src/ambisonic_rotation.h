// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Rotation matrices for real spherical harmonics, order by order, by
// Ivanic and Ruedenberg's recursion (J. Phys. Chem. 100, 1996, with the
// corrections in J. Phys. Chem. A 102, 1998). A rotation acts within each
// order, so the matrices serve SN3D and N3D alike.

#ifndef MAUL_AUDIO_SRC_AMBISONIC_ROTATION_H
#define MAUL_AUDIO_SRC_AMBISONIC_ROTATION_H

#include <stdint.h>

// The floats of an order's blocks together: 1 + 9 + 25 + 49 at order 3.
#define MAUD_ROTATION_FLOATS 84

// Fills blocks with the matrices of orders 1 to order for a rotation
// matrix in the field's axes (x ahead, y left, z up), each a row-major
// (2l + 1) square in ACN order, order after order; order 0 is the
// identity and is not stored. A direction's encoding times the matrices
// is the rotated direction's encoding.
void maudAmbisonicRotation(uint32_t order, const double rotation[3][3], float* blocks);

#endif // MAUL_AUDIO_SRC_AMBISONIC_ROTATION_H
