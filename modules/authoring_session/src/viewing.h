#pragma once

// A scene's view moved as an editor's viewport is (D469): the wheel brings
// the eye nearer its target or farther, and a drag with the right button
// carries it round the target; with Shift, freelook's (D472), the drag
// turns the view about its eye and the wheel flies it along. Editor state, never the scene's data
// (ADR-0066).

#include "rawframe/authoring/authored_scene.h"
#include "rawframe/document/json.h"

#include <array>

namespace rawframe::authoring_session {

/// `view` with its eye `detents` wheel turns farther from its target
/// (toward the author positive, away nearer), each a sixth again, held
/// between half a meter and ten kilometers; and turned round the target
/// `across` pixels about the upright (a pixel a two hundredth of a
/// radian, rightward carrying the eye left) and `up` pixels over it (down
/// raising the eye), its height held short of straight over or under.
[[nodiscard]] authoring::SceneView
movedView(const authoring::SceneView& view, double detents, double across, double up) noexcept;

/// `view` turned about its eye, freelook's (D472): its target carried
/// round the eye `across` pixels about the upright and `up` pixels over it
/// (down looking down), a pixel a two hundredth of a radian, its height
/// held short of straight over or under; the eye where it was.
[[nodiscard]] authoring::SceneView lookedView(const authoring::SceneView& view, double across, double up) noexcept;

/// `view` flown along itself, eye and target together, `detents` wheel
/// turns back (toward the author positive, away forward), each a sixth of
/// the eye's distance from the target.
[[nodiscard]] authoring::SceneView flownView(const authoring::SceneView& view, double detents) noexcept;

/// What a preview's wheel and drags with the right button came to (D469,
/// D472): as totals `tooling.clicked` answers, or their change since a
/// reading.
struct ViewMotion {
    double wheel = 0;
    std::array<double, 2> orbit{};
    double fly = 0;
    std::array<double, 2> look{};
    friend bool operator==(const ViewMotion&, const ViewMotion&) = default;
};

/// The totals a `tooling.clicked` answer gives, nought for any it lacks.
[[nodiscard]] ViewMotion motionOf(const document::Value& clicked);

/// What moved from `read` to `now`.
[[nodiscard]] ViewMotion motionSince(const ViewMotion& now, const ViewMotion& read) noexcept;

/// `view` moved by `change`: orbited and zoomed, then turned about its eye
/// and flown.
[[nodiscard]] authoring::SceneView movedBy(const authoring::SceneView& view, const ViewMotion& change) noexcept;

} // namespace rawframe::authoring_session
