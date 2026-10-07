#pragma once

// The lines a scene shows (D464): kept only when sound, and drawn as
// ribbons with the frame's beams.

#include "rawframe/render_scene/scene.h"

#include <array>
#include <span>
#include <vector>

namespace rawframe::render_scene {

/// The sound of `lines`, at most `kMostSceneLines`: finite ends and color,
/// a width above nought.
[[nodiscard]] std::vector<SceneLine> soundLines(std::span<const SceneLine> lines);

/// `lines` added to `frame` as ribbons of two points about `eye`, on the
/// first material after the frame's own, or the second for a line drawn
/// over everything; a width of the view made meters where each end is,
/// through the frame's view and projection.
void addLines(SceneFrame& frame, std::span<const SceneLine> lines, const std::array<double, 3>& eye);

} // namespace rawframe::render_scene
