// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// One source's direct result from the listener's pose and its own:
// distance, direction in the listener's frame and directivity. Occlusion
// and transmission are left clear for the ray passes to fill.

#ifndef MAUL_AUDIO_SRC_DIRECT_STEP_H
#define MAUL_AUDIO_SRC_DIRECT_STEP_H

#include "maul-audio/spatializer.h"

// Whether a pose is finite with a nonzero orientation.
bool maudPoseValid(const maudPose* pose);

// v turned by the inverse of the rotation q (q of any nonzero length).
maudVector3 maudUnrotate(maudQuaternion q, maudVector3 v);

// The result for a source with a directivity pattern.
void maudDirectGeometry(const maudPose* listener, const maudPose* source,
                        const maudDirectivityPattern* pattern, maudDirectResult* result);

#endif // MAUL_AUDIO_SRC_DIRECT_STEP_H
