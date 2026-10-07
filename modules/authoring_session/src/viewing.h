#pragma once

// A scene's view moved as an editor's viewport is (D469): the wheel brings
// the eye nearer its target or farther, and a drag with the right button
// carries it round the target. Editor state, never the scene's data
// (ADR-0066).

#include "rawframe/authoring/authored_scene.h"

namespace rawframe::authoring_session {

/// `view` with its eye `detents` wheel turns farther from its target
/// (toward the author positive, away nearer), each a sixth again, held
/// between half a meter and ten kilometers; and turned round the target
/// `across` pixels about the upright (a pixel a two hundredth of a
/// radian, rightward carrying the eye left) and `up` pixels over it (down
/// raising the eye), its height held short of straight over or under.
[[nodiscard]] authoring::SceneView
movedView(const authoring::SceneView& view, double detents, double across, double up) noexcept;

} // namespace rawframe::authoring_session
