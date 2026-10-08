#pragma once

// The sun's shadow cascades (ADR-0051, D292): each slice of the view's
// depth fitted with a square seen from the sun, its texels snapped to the
// World's, so the shadows hold still as the eye moves.

#include "rawframe/render_scene/scene.h"
#include "space.h"

#include <array>

namespace rawframe::render_scene {

/// Fits `settings.cascades` slices of the view from `near` to the shadows'
/// distance, split between uniform and logarithmic by its blend, each a
/// sphere about the view's axis seen from `toSun`, into `shadows`; `axes`
/// are the view's right, up, and forward, `half` its half vertical angle.
void fitCascades(const ShadowSettings& settings,
                 const Vector& toSun,
                 const SceneCamera& camera,
                 const std::array<Vector, 3>& axes,
                 float half,
                 float aspect,
                 float near,
                 SceneShadows& shadows);

} // namespace rawframe::render_scene
