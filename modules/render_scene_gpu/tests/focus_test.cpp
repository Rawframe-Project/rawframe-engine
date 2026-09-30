// The depth of field on lavapipe (D336): through a long lens focused two
// meters ahead, a box there keeps sharp edges while a box sixteen meters
// away blurs its edges over several pixels; without the depth of field,
// both are sharp.

#include "fixture.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>
#include <vector>

using namespace rawframe;
using namespace rawframe::scene_fixture;
using render_scene::SceneFrame;

namespace {

/// Through a lens of about 80 millimeters, a small red box two meters
/// ahead on the left and a large one sixteen meters ahead on the right.
SceneFrame lensed(bool focused) {
    const auto kSchema = *schema::RegistryBuilder{}.freeze();
    auto scene = *render_scene::Scene::create(*kSchema, {});
    SceneFrame made = scene->queue({.fovY = 0.3F, .near = 0.1F, .exposure = 15, .aspect = 1});
    made.shadows.count = 0;
    made.dither = false;
    render_scene::SceneDraw nearBox = box(2, 0.1F, {1, 0, 0, 1});
    nearBox.model[12] = -0.15F;
    render_scene::SceneDraw farBox = box(16, 0.8F, {1, 0, 0, 1});
    farBox.model[12] = 1.2F;
    made.draws = {nearBox, farBox};
    if (focused) {
        made.depthOfField = {.enabled = true, .focus = 2, .aperture = 0.5F};
    }
    return made;
}

/// The pixels along the middle row, from `first` up to `last`, neither
/// the sky's red nor a box's.
int between(const std::vector<std::byte>& pixels, std::uint32_t first, std::uint32_t last) {
    const int kSky = at(pixels, first, 2)[0];
    int box = 0;
    for (std::uint32_t x = first; x < last; ++x) {
        box = std::max(box, at(pixels, x, kSide / 2)[0]);
    }
    int count = 0;
    for (std::uint32_t x = first; x < last; ++x) {
        const int kRed = at(pixels, x, kSide / 2)[0];
        count += kRed > kSky + 8 && kRed < box - 8 ? 1 : 0;
    }
    return count;
}

} // namespace

RAWFRAME_TEST(ALensKeepsItsFocusSharpAndBlursWhatIsFarBeyondIt) {
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
    const auto kPlain = drawn(**framer, **made, lensed(false), kMeshes);
    const auto kFocused = drawn(**framer, **made, lensed(true), kMeshes);
    RAWFRAME_EXPECT(kPlain.has_value() && kFocused.has_value());
    if (!kPlain.has_value() || !kFocused.has_value()) {
        return;
    }
    const int kNearPlain = between(*kPlain, 0, kSide / 2);
    const int kFarPlain = between(*kPlain, kSide / 2, kSide);
    const int kNear = between(*kFocused, 0, kSide / 2);
    const int kFar = between(*kFocused, kSide / 2, kSide);
    std::printf("edges between the sky and a box: near %d, far %d; with the lens near %d, far %d\n",
                kNearPlain,
                kFarPlain,
                kNear,
                kFar);
    RAWFRAME_EXPECT(kNearPlain <= 2 && kFarPlain <= 2 && kNear <= 2 && kFar >= 4);
    RAWFRAME_EXPECT((*made)->statistics().framesFocused == 1);
}
