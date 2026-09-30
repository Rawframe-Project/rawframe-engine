// The ambient occlusion on lavapipe (D327): under a sky's light alone, the
// floor at the foot of a block takes less of it with the occlusion than
// without, the floor far from anything as much, and the block's face well
// above the floor too.

#include "fixture.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>

using namespace rawframe;
using namespace rawframe::scene_fixture;
using render_scene::SceneFrame;

namespace {

/// A view looking down a little across a white floor at a red block
/// standing on it, lit by the sky alone.
SceneFrame yard(bool occluded) {
    const auto kSchema = *schema::RegistryBuilder{}.freeze();
    auto scene = *render_scene::Scene::create(*kSchema, {});
    SceneFrame made = scene->queue({.pitch = -0.7F, .fovY = 1.5707964F, .near = 0.1F, .exposure = 12, .aspect = 1});
    made.temporal.enabled = false;
    made.shadows.count = 0;
    made.lights.sun = {0, 0, 0};
    made.lights.sky = {4000, 4000, 4000};
    made.lights.ground = {0, 0, 0};
    render_scene::SceneDraw floor = box(5, 1, {1, 1, 1, 1});
    floor.model[0] = 20;
    floor.model[5] = 0.5F;
    floor.model[10] = 20;
    floor.normal[0] = 1.0F / 20;
    floor.normal[5] = 2;
    floor.normal[10] = 1.0F / 20;
    floor.model[13] = -2.5F;
    render_scene::SceneDraw block = box(5, 1, {1, 0, 0, 1});
    block.model[13] = -1;
    made.draws = {floor, block};
    if (occluded) {
        made.occlusion = {.enabled = true, .radius = 1, .intensity = 1};
    }
    return made;
}

bool red(const std::array<int, 3>& pixel) {
    return pixel[0] > pixel[1] + 40;
}

} // namespace

RAWFRAME_TEST(TheFloorAtABlocksFootIsDarkerWithAmbientOcclusion) {
    const auto kDevice = opened();
    if (kDevice == nullptr) {
        return;
    }
    auto made = render_scene_gpu::SceneRenderer::create(*kDevice);
    auto framer = render::Framer::create(*kDevice);
    RAWFRAME_EXPECT(made.has_value() && framer.has_value());
    if (!made.has_value() || !framer.has_value()) {
        return;
    }
    const render_scene_gpu::MeshSource kMeshes = [](std::uint64_t id) {
        return render_scene::engineMesh(id);
    };
    // Each frame's light, read back (D326), so the occlusion is measured
    // in linear light.
    const auto kLit = [&](bool occluded) {
        (*made)->capture();
        const auto kPixels = drawn(**framer, **made, yard(occluded), kMeshes);
        RAWFRAME_EXPECT(kPixels.has_value());
        return std::pair{kPixels, (*made)->captured()};
    };
    const auto [kPlain, kPlainLight] = kLit(false);
    // The occlusion's pipelines asked for and made first (D337).
    static_cast<void>(
        drawnWith(**framer, **made, yard(true), kMeshes, &render_scene_gpu::RendererStatistics::framesOccluded));
    const auto [kOccluded, kOccludedLight] = kLit(true);
    RAWFRAME_EXPECT(kPlain.has_value() && kPlainLight.has_value() && kOccludedLight.has_value());
    if (!kPlain.has_value() || !kPlainLight.has_value() || !kOccludedLight.has_value()) {
        return;
    }
    // The block's foot: the lowest red pixel down the middle.
    std::uint32_t foot = 0;
    for (std::uint32_t row = 0; row < kSide; ++row) {
        if (red(at(*kPlain, kSide / 2, row))) {
            foot = row;
        }
    }
    RAWFRAME_EXPECT(foot > 10 && foot + 2 < kSide);
    // What reaches a pixel with the occlusion, of what reaches it without.
    const auto kReaching = [&](std::uint32_t x, std::uint32_t row) {
        const std::size_t kAt = ((std::size_t{row} * kSide) + x) * 3;
        return kOccludedLight->light[kAt] / std::max(kPlainLight->light[kAt], 1e-3F);
    };
    const float kFoot = kReaching(kSide / 2, foot + 1);
    const float kOpen = kReaching(4, kSide - 3);
    const float kFace = kReaching(kSide / 2, foot - 10);
    std::printf("foot at row %u: %.2f of the light reaches it; open floor %.2f; the block's face %.2f\n",
                foot,
                kFoot,
                kOpen,
                kFace);
    RAWFRAME_EXPECT(kFoot < 0.8F && kOpen > 0.98F && kFace > 0.97F);
}
