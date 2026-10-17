// What a frame the scene drew holds, as an editor's preview is told
// (D842): a run of draws sharing a mesh and a material is one step with
// its instances, the translucent after the opaque, and the figures say
// what was drawn and which effects ran.

#include "../src/frame_figures.h"
#include "rawframe/test/test.h"

#include <algorithm>

using namespace rawframe;
using namespace rawframe::render_scene;

namespace {

std::int64_t figure(const std::vector<view::FrameFigure>& figures, std::string_view name) {
    const auto kAt = std::ranges::find(figures, name, &view::FrameFigure::name);
    return kAt != figures.end() ? kAt->value : -1;
}

} // namespace

RAWFRAME_TEST(AFramesDrawsAreStepsByMeshAndMaterial) {
    SceneFrame frame;
    // Three of mesh 1, two of mesh 2 in material 4, then one translucent.
    for (const auto& [kMesh, kMaterial] :
         {std::pair{1ULL, 0U}, {1ULL, 0U}, {1ULL, 0U}, {2ULL, 4U}, {2ULL, 4U}, {3ULL, 1U}}) {
        frame.draws.push_back(SceneDraw{.mesh = kMesh, .material = kMaterial});
    }
    frame.translucent = 1;
    frame.drawn = 6;
    frame.culled = 2;
    frame.bloom.enabled = true;
    std::vector<view::FrameFigure> figures;
    std::vector<view::FrameStep> steps;
    describeFrame(frame, figures, steps);
    RAWFRAME_EXPECT(steps.size() == 3);
    RAWFRAME_EXPECT(steps.size() == 3 && steps[0].pass == "opaque" && steps[0].mesh == 1 && steps[0].instances == 3 &&
                    steps[1].mesh == 2 && steps[1].material == 4 && steps[1].instances == 2 &&
                    steps[2].pass == "translucent" && steps[2].mesh == 3);
    RAWFRAME_EXPECT(figure(figures, "draws") == 6 && figure(figures, "opaque") == 5 &&
                    figure(figures, "opaque_batches") == 2 && figure(figures, "culled") == 2);
    RAWFRAME_EXPECT(figure(figures, "bloom") == 1 && figure(figures, "temporal") == 0 && figure(figures, "steps") == 3);
}

RAWFRAME_TEST(ACrowdedFrameSaysWhatItsStepsLeftOut) {
    SceneFrame frame;
    // Each draw its own mesh: a step each, past the limit.
    for (std::uint64_t each = 0; each < view::kMostFrameSteps + 10; ++each) {
        frame.draws.push_back(SceneDraw{.mesh = each + 1});
    }
    std::vector<view::FrameFigure> figures;
    std::vector<view::FrameStep> steps;
    describeFrame(frame, figures, steps);
    RAWFRAME_EXPECT(steps.size() == view::kMostFrameSteps);
    RAWFRAME_EXPECT(figure(figures, "steps_left_out") == 10);
    // Described again, it starts afresh.
    describeFrame(SceneFrame{}, figures, steps);
    RAWFRAME_EXPECT(steps.empty() && figure(figures, "draws") == 0);
}
