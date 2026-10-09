// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Transmission walks: from the listener towards the source, one closest
// hit at a time, every query starting at the listener with its minimum
// distance just past the last surface, so no offset builds up; each
// surface crossed multiplies the transmission by its material's.

#ifndef MAUL_AUDIO_SRC_TRANSMISSION_H
#define MAUL_AUDIO_SRC_TRANSMISSION_H

#include "maul-audio/spatializer.h"

// The ray from the listener to the source, starting at minDistance.
maudRay maudPathRay(const maudPose* listener, const maudPose* source, float minDistance);

// Crosses the surface a hit found on a ray, if it found one: multiplies
// transmission by the material's (a material outside the table blocks
// fully) and gives the next minimum distance. False when the hit is a
// miss, which ends the walk.
bool maudCrossSurface(const maudRayHit* hit, const maudRay* ray,
                      const maudAcousticMaterial* materials, uint32_t materialCount,
                      float* transmission, float* nextMin);

#endif // MAUL_AUDIO_SRC_TRANSMISSION_H
