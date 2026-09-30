#pragma once

#include "rawframe/render_scene/scene.h"
#include "space.h"

#include <span>
#include <vector>

namespace rawframe::render_scene {

/// A model a punctual light's shadow may take (D292): its draw, and its
/// bounding sphere relative to the eye.
struct ShadowCandidate {
    SceneDraw draw;
    Vector center{};
    float radius = 0;
};

/// The view's lens as the view stage made it: whether it sees at all, half
/// its height's angle, its width over its height, and its near plane.
struct ViewShape {
    bool sees = false;
    float half = 0.5F;
    float aspect = 1;
    float near = 0.1F;
};

/// The view stage's lights (ADR-0051, D290): each punctual light that
/// lights something and reaches the view, in the order of its entity, into
/// `frame.lights3d`, named by every cluster its sphere may reach: the
/// slices its depth spans, and the tiles between the lines from the eye
/// that touch it; the rest counted. `shadowed` says which kept light asks
/// for shadows.
void clusterLights(SceneFrame& frame,
                   std::span<const LightInstance> punctual,
                   const SceneCamera& camera,
                   const std::array<Vector, 3>& axes,
                   const ViewShape& view,
                   const SceneLimits& limits,
                   std::vector<bool>& shadowed);

/// The punctual lights' shadows (D292). Those that ask, by how much of the
/// view they may cover (their range over their distance, at most one),
/// ties in their order, each granted squares of the side that cover earns
/// (the largest from a half, halving as the cover halves) or smaller where
/// the atlas is fuller, while the budget of lights lasts; the rest are
/// counted. The squares are placed largest first, each at the next free
/// place in the atlas's Morton order, which leaves nothing between them;
/// each takes the candidates within its view.
void shadowLights(SceneFrame& frame,
                  const std::vector<bool>& shadowed,
                  std::span<const ShadowCandidate> candidates,
                  const LightShadowSettings& atlas,
                  const SceneLimits& limits);

} // namespace rawframe::render_scene
