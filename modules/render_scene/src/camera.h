#pragma once

// A camera as the scene uses it: its metering made sound (D293, D345), and
// the eye's axes (the view's geometry, D366) and matrix products its
// matrices are made with.

#include "rawframe/render_scene/scene.h"
#include "space.h"

#include <array>

namespace rawframe::render_scene {

/// The camera's metering made sound (D293): off when it asks for none, a
/// value is not finite, or its maximum is not above its minimum (a camera's
/// meter not yet set is all nought); the rates not negative, the fractions
/// within nought and one with low below high, the middle's weight within
/// nought and one (D345), and the seconds since the frame before at most a
/// quarter.
[[nodiscard]] SceneMetering meteringOf(const SceneCamera& camera) noexcept;

/// `left` times `right`, column-major.
[[nodiscard]] Matrix times(const Matrix& left, const Matrix& right) noexcept;

/// The eye's axes, by the view's geometry (D366): right, up, forward.
[[nodiscard]] std::array<Vector, 3> axesOf(float yaw, float pitch) noexcept;

} // namespace rawframe::render_scene
