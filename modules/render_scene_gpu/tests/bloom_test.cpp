// The bloom on lavapipe (D328): a small, very bright glowing box in the
// dark spreads light around itself with the bloom, where without it the
// dark stays dark; the more the camera asks, the more it spreads.

#include "fixture.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/test/test.h"

#include <array>
#include <cstdio>

using namespace rawframe;
using namespace rawframe::scene_fixture;
using render_scene::SceneFrame;

namespace {

/// A box 0.6 meters across, four meters ahead, glowing a million nits
/// in no other light, with `intensity` of bloom.
SceneFrame glowing(float intensity) {
    SceneFrame made = looking();
    made.shadows.count = 0;
    made.lights.sun = {0, 0, 0};
    made.lights.sky = {0, 0, 0};
    made.lights.ground = {0, 0, 0};
    render_scene::MaterialBlob glow = render_scene::noMaterial();
    glow[0] = 0;
    glow[1] = 0;
    glow[2] = 0;
    glow[8] = 1000000;
    glow[9] = 1000000;
    glow[10] = 1000000;
    render_scene::SceneDraw box = scene_fixture::box(4, 0.3F, {1, 1, 1, 1});
    box.material = 1;
    made.materials = {render_scene::noMaterial(), glow};
    made.draws = {box};
    if (intensity > 0) {
        made.bloom = {.enabled = true, .intensity = intensity};
    }
    return made;
}

} // namespace

RAWFRAME_TEST(ABrightGlowSpreadsWithTheBloom) {
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
    std::array<std::array<int, 3>, 3> beside{};
    std::array<std::array<int, 3>, 3> middle{};
    const std::array<float, 3> kIntensities = {0, 0.1F, 0.4F};
    for (std::size_t which = 0; which < kIntensities.size(); ++which) {
        const auto kPixels = drawn(**framer, **made, glowing(kIntensities[which]), kMeshes);
        RAWFRAME_EXPECT(kPixels.has_value());
        if (!kPixels.has_value()) {
            return;
        }
        middle[which] = at(*kPixels, kSide / 2, kSide / 2);
        beside[which] = at(*kPixels, kSide / 2 + 8, kSide / 2);
        std::printf(
            "bloom %.1f: middle %d, eight pixels beside %d\n", kIntensities[which], middle[which][0], beside[which][0]);
    }
    RAWFRAME_EXPECT(middle[0][0] > 200 && beside[0][0] < 3);
    RAWFRAME_EXPECT(beside[1][0] > beside[0][0] + 10 && beside[2][0] > beside[1][0] + 10);
    RAWFRAME_EXPECT(middle[2][0] > 150);
}
