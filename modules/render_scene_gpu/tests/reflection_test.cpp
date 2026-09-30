// The screen-space reflections on lavapipe (D331): a mirror floor shows
// the red block standing on it below the block's foot with the
// reflections, where without them it shows only the sky's light; and the
// first frame, with no picture before, draws none.

#include "fixture.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/test/test.h"

#include <array>
#include <cstdio>
#include <optional>
#include <vector>

using namespace rawframe;
using namespace rawframe::scene_fixture;
using render_scene::SceneFrame;

namespace {

/// A view looking down across a mirror floor at a red block standing on
/// it, antialiased over time.
SceneFrame hall(bool reflecting) {
    const auto kSchema = *schema::RegistryBuilder{}.freeze();
    auto scene = *render_scene::Scene::create(*kSchema, {});
    SceneFrame made = scene->queue({.pitch = -0.7F, .fovY = 1.5707964F, .near = 0.1F, .exposure = 12, .aspect = 1});
    made.temporal.enabled = true;
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
    floor.material = 1;
    render_scene::SceneDraw block = box(5, 1, {1, 0, 0, 1});
    block.model[13] = -1;
    made.draws = {floor, block};
    render_scene::MaterialBlob mirror = render_scene::noMaterial();
    mirror[3] = 1;
    mirror[7] = 0;
    made.materials = {render_scene::noMaterial(), mirror};
    if (reflecting) {
        made.reflections = {.enabled = true, .distance = 20};
    }
    return made;
}

bool red(const std::array<int, 3>& pixel) {
    return pixel[0] > pixel[1] + 40;
}

} // namespace

RAWFRAME_TEST(AMirrorFloorShowsTheBlockOnItWithScreenSpaceReflections) {
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
    // Eight jittered frames, each after the first reusing the picture
    // before, as the reflections need.
    const auto kDrawn = [&](bool reflecting) {
        SceneFrame frame = hall(reflecting);
        std::optional<std::vector<std::byte>> pixels;
        for (std::uint64_t index = 0; index < 8; ++index) {
            frame.temporal.jitter = render_scene::temporalJitter(index);
            frame.temporal.history = index > 0;
            pixels = drawn(**framer, **made, frame, kMeshes);
        }
        return pixels;
    };
    const auto kPlain = kDrawn(false);
    const std::uint64_t kBefore = (*made)->statistics().framesReflected;
    const auto kReflected = kDrawn(true);
    RAWFRAME_EXPECT(kPlain.has_value() && kReflected.has_value());
    if (!kPlain.has_value() || !kReflected.has_value()) {
        return;
    }
    // The first frame has no picture before to reflect.
    RAWFRAME_EXPECT(kBefore == 0 && (*made)->statistics().framesReflected == 7);
    // The block's foot: the lowest red pixel down the middle without the
    // reflections.
    std::uint32_t foot = 0;
    for (std::uint32_t row = 0; row < kSide; ++row) {
        if (red(at(*kPlain, kSide / 2, row))) {
            foot = row;
        }
    }
    RAWFRAME_EXPECT(foot > 10 && foot + 8 < kSide);
    const std::array<int, 3> kWithout = at(*kPlain, kSide / 2, foot + 4);
    const std::array<int, 3> kWith = at(*kReflected, kSide / 2, foot + 4);
    const std::array<int, 3> kAside = at(*kReflected, 2, kSide - 3);
    std::printf("foot at row %u; below it without %d %d %d, with %d %d %d; the floor aside %d %d %d\n",
                foot,
                kWithout[0],
                kWithout[1],
                kWithout[2],
                kWith[0],
                kWith[1],
                kWith[2],
                kAside[0],
                kAside[1],
                kAside[2]);
    RAWFRAME_EXPECT(!red(kWithout) && red(kWith) && !red(kAside));
}
