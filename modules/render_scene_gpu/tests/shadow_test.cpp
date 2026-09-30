// The shadow filters on lavapipe (D330): on a coarse map, the soft class
// draws a shadow's edge through more shades than the hardware's blend of
// four, the shadow as dark within and the floor as lit without.

#include "fixture.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

using namespace rawframe;
using namespace rawframe::scene_fixture;
using render_scene::SceneDraw;
using render_scene::SceneFrame;

namespace {

/// The sun's shadows' test (renderer_test.cpp), its box twice as large, on a
/// map of 128 texels a cascade, filtered `filter`.
SceneFrame yard(render_scene::ShadowFilter filter) {
    const auto kSchema = *schema::RegistryBuilder{}.freeze();
    auto scene = *render_scene::Scene::create(*kSchema, {.shadows = {.side = 128, .filter = filter}});
    SceneFrame made = scene->queue({.fovY = 1.5707964F, .near = 0.1F, .exposure = 15, .aspect = 1});
    made.temporal.enabled = false;
    SceneDraw floor = box(8, 1, {1, 1, 1, 1});
    floor.model[0] = 10;
    floor.model[5] = 0.1F;
    floor.model[10] = 10;
    floor.normal[0] = 0.1F;
    floor.normal[5] = 10;
    floor.normal[10] = 0.1F;
    floor.model[13] = -2;
    made.draws = {floor, box(8, 1, {1, 1, 1, 1})};
    made.shadows.casters = made.draws;
    for (std::size_t at = 0; at < made.shadows.count; ++at) {
        made.shadows.cascades[at].firstCaster = 0;
        made.shadows.cascades[at].casterCount = 2;
    }
    return made;
}

} // namespace

RAWFRAME_TEST(TheSoftFilterDrawsAShadowsEdgeThroughMoreShades) {
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
    // Along the row through the box's shadow on the floor: how many
    // pixels lie between the shadow's darkest and the floor's lit, well
    // clear of both.
    std::array<std::size_t, 2> between{};
    std::array<std::array<int, 2>, 2> ends{};
    for (const render_scene::ShadowFilter kFilter :
         {render_scene::ShadowFilter::Hardware, render_scene::ShadowFilter::Soft}) {
        const auto kPixels = drawn(**framer, **made, yard(kFilter), kMeshes);
        RAWFRAME_EXPECT(kPixels.has_value());
        if (!kPixels.has_value()) {
            return;
        }
        std::vector<int> row;
        for (std::uint32_t x = 16; x < 48; ++x) {
            row.push_back(at(*kPixels, x, 39)[0]);
        }
        const auto [kDarkest, kLitmost] = std::ranges::minmax(row);
        const auto kClass = static_cast<std::size_t>(kFilter);
        ends[kClass] = {kDarkest, kLitmost};
        between[kClass] = static_cast<std::size_t>(std::ranges::count_if(row, [&](int red) {
            return red > kDarkest + 8 && red < kLitmost - 8;
        }));
        std::printf("%s: shadow %d, lit %d, %zu between\n",
                    kFilter == render_scene::ShadowFilter::Soft ? "soft" : "hardware",
                    kDarkest,
                    kLitmost,
                    between[kClass]);
    }
    RAWFRAME_EXPECT(between[1] > between[0]);
    RAWFRAME_EXPECT(ends[0][1] > ends[0][0] + 30 && ends[1][1] > ends[1][0] + 30);
    RAWFRAME_EXPECT(std::abs(ends[0][1] - ends[1][1]) < 6);
}
