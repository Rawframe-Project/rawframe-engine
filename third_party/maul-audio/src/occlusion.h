// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Occlusion rays and their reduction. A source occluded by one ray casts
// it from the listener to the source. A volumetric source casts, for
// each of its sample points in a sphere around it, a ray from the source
// to the point (does the source see it?) and one from the listener to
// the point (does the listener?); its occlusion is the share of points
// the source sees that the listener does not.

#ifndef MAUL_AUDIO_SRC_OCCLUSION_H
#define MAUL_AUDIO_SRC_OCCLUSION_H

#include "maul-audio/spatializer.h"

// Fills count points in the unit ball: the first count of Roberts' R3
// sequence, mapped to the ball so that it is covered evenly by volume.
void maudBallPoints(uint32_t count, maudVector3* points);

// The rays a source casts.
uint32_t maudOcclusionRayCount(maudOcclusionMethod method, uint32_t samples);

// Writes a source's rays, maudOcclusionRayCount of them.
void maudOcclusionRays(const maudPose* listener, const maudPose* source, maudOcclusionMethod method,
                       float radius, uint32_t samples, const maudVector3* points, maudRay* rays);

// The occlusion from the rays' answers, in the order they were written.
float maudOcclusionOf(maudOcclusionMethod method, uint32_t samples, const uint8_t* occluded);

#endif // MAUL_AUDIO_SRC_OCCLUSION_H
