// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Refining a path with rays alone, then what it does to sound. Vertices
// whose neighbours see each other are dropped; each interior vertex
// slides along its legs as far as the other leg stays visible (two
// passes of a 12-step bisection each way); a pattern search then moves
// it in 26 directions, from 0.25 m down to 2 mm, while that shortens the
// path and both legs stay visible. Around corners and through doors
// this lands the vertices on the edges a probe graph only comes near.
// Each corner diffracts as a half-plane does in the uniform theory of
// diffraction (0.05 m from the edge, at the bands' centres of 400 Hz,
// 4.4 kHz and 15 kHz), 1 at no turn; a path's corners multiply.

#ifndef MAUL_AUDIO_SRC_PATH_REFINE_H
#define MAUL_AUDIO_SRC_PATH_REFINE_H

#include "maul-audio/direct.h"
#include "maul-audio/spatializer.h"

#include <stdint.h>

// The diffraction table's steps: a degree each, 0 to 180.
#define MAUD_DIFFRACTION_STEPS 181u

typedef struct maudDiffraction
{
    float gain[MAUD_DIRECT_BANDS][MAUD_DIFFRACTION_STEPS];
} maudDiffraction;

void maudSetupDiffraction(maudDiffraction* diffraction);

// Refines a path's count vertices in place, its ends fixed; returns the
// count left. A NULL query leaves it as it is.
uint32_t maudRefinePath(maudAnyHitFn* anyHit, void* context, maudVector3* vertices, uint32_t count);

// The amplitude per band around a path's corners.
void maudPathDiffraction(const maudDiffraction* diffraction, const maudVector3* vertices,
                         uint32_t count, float* gains);

#endif // MAUL_AUDIO_SRC_PATH_REFINE_H
