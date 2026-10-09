// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Weighted least squares for small systems, used when filters are built
// (never on the audio thread).

#ifndef MAUL_AUDIO_SRC_LEAST_SQUARES_H
#define MAUL_AUDIO_SRC_LEAST_SQUARES_H

#include <stdint.h>

// For count rows of k values (row-major, k at most 16) and a weight per
// row, fills solve (k x count, row-major) with (Y^T W Y)^-1 Y^T W, the
// map from values at the rows to the weighted least-squares
// coefficients: solve times the rows is the identity. false, and solve
// untouched, when Y^T W Y is not positive definite.
bool maudWeightedPseudoInverse(const double* rows, const double* weights, uint32_t count,
                               uint32_t k, double* solve);

#endif // MAUL_AUDIO_SRC_LEAST_SQUARES_H
