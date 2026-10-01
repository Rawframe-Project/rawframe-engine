// The contact shadows on lavapipe (D338): with the shadow maps off and the
// sun low on the left, the floor just right of a block standing on it is
// darker with the contact shadows than without, and the floor far from
// the block is as lit.

#include "fixture.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/test/test.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace rawframe;
using namespace rawframe::scene_fixture;
using render_scene::SceneFrame;

namespace {

/// A view looking down a little across a white floor at a red block
/// standing on it, lit by a low sun from the left and a dim sky.
SceneFrame yard(bool contacted) {
    const auto kSchema = *schema::RegistryBuilder{}.freeze();
    auto scene = *render_scene::Scene::create(*kSchema, {});
    SceneFrame made = scene->queue({.pitch = -0.7F, .fovY = 1.5707964F, .near = 0.1F, .exposure = 12, .aspect = 1});
    made.temporal.enabled = false;
    made.shadows.count = 0;
    made.dither = false;
    const float kLength = std::sqrt(1.25F);
    made.lights.toSun = {-1 / kLength, 0.5F / kLength, 0};
    made.lights.sun = {8000, 8000, 8000};
    made.lights.sky = {1000, 1000, 1000};
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
    if (contacted) {
        made.contactShadows = {.enabled = true, .length = 1};
    }
    return made;
}

bool red(const std::array<int, 3>& pixel) {
    return pixel[0] > pixel[1] + 40;
}

} // namespace

RAWFRAME_TEST(TheFloorBesideABlockIsShadedByContactShadows) {
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
    const auto kPlain = drawn(**framer, **made, yard(false), kMeshes);
    const auto kShaded =
        drawnWith(**framer, **made, yard(true), kMeshes, &render_scene_gpu::RendererStatistics::framesContactShadowed);
    RAWFRAME_EXPECT(kPlain.has_value() && kShaded.has_value());
    if (!kPlain.has_value() || !kShaded.has_value()) {
        return;
    }
    // The block's foot, the lowest red pixel down the middle, and its right
    // edge two rows above it.
    std::uint32_t foot = 0;
    for (std::uint32_t row = 0; row < kSide; ++row) {
        if (red(at(*kPlain, kSide / 2, row))) {
            foot = row;
        }
    }
    std::uint32_t right = kSide / 2;
    while (right + 1 < kSide && red(at(*kPlain, right + 1, foot - 2))) {
        ++right;
    }
    RAWFRAME_EXPECT(foot > 10 && right + 6 < kSide);
    const std::array<int, 3> kBesideLit = at(*kPlain, right + 3, foot - 2);
    const std::array<int, 3> kBesideShaded = at(*kShaded, right + 3, foot - 2);
    const std::array<int, 3> kFarLit = at(*kPlain, 3, kSide - 3);
    const std::array<int, 3> kFarShaded = at(*kShaded, 3, kSide - 3);
    std::printf("foot at row %u, right edge at %u; beside it %d, with contact shadows %d; far off %d and %d\n",
                foot,
                right,
                kBesideLit[1],
                kBesideShaded[1],
                kFarLit[1],
                kFarShaded[1]);
    RAWFRAME_EXPECT(kBesideShaded[1] + 30 < kBesideLit[1] && std::abs(kFarShaded[1] - kFarLit[1]) <= 3);
}
