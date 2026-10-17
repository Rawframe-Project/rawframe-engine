#pragma once

// What a frame the scene drew holds, for an editor's preview (D842): its
// figures (what was drawn, culled, and left out, which effects ran) and
// its steps, pass by pass, each run of draws sharing a mesh and a material
// one step with its instances.

#include "rawframe/render_scene/scene.h"
#include "rawframe/view/preview.h"

#include <vector>

namespace rawframe::render_scene {

/// `frame`'s figures and steps, replacing what `figures` and `steps` held;
/// at most `view::kMostFrameSteps` steps, the figure `steps_left_out`
/// saying how many more there were.
void describeFrame(const SceneFrame& frame,
                   std::vector<view::FrameFigure>& figures,
                   std::vector<view::FrameStep>& steps);

} // namespace rawframe::render_scene
