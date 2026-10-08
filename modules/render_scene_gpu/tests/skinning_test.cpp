// Skinned models on the device (D508), on lavapipe: a box whose mesh is
// skinned to one joint, its palette moving that joint a box's width to the
// right, is drawn there and not where its mesh lies; the same draw without
// its palette is drawn where its mesh lies; and the skinned model is
// counted. Skips where no adapter answers, unless RAWFRAME_REQUIRE_GPU is
// set.

#include "fixture.h"
#include "rawframe/render/device.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>

using namespace rawframe;
using render_scene::SceneDraw;
using render_scene::SceneFrame;
using namespace rawframe::scene_fixture;

namespace {

constexpr std::uint64_t kSkinnedBox = 0x5c1;

/// The engine's box, every vertex following one joint bound where it lies.
std::shared_ptr<const mesh::Mesh> skinnedBox() {
    mesh::Mesh made = *render_scene::engineMesh(render_scene::kBox);
    made.skin.joints = {{.bone = {1, 1}, .inverseBind = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}};
    made.skin.influences.assign(made.positions.size(), {0, 0, 0, 0});
    made.skin.weights.assign(made.positions.size(), {1, 0, 0, 0});
    return std::make_shared<const mesh::Mesh>(std::move(made));
}

/// The red pixels' middle along the target's middle row; none if none is.
std::optional<float> redMiddle(const std::vector<std::byte>& pixels) {
    float sum = 0;
    int count = 0;
    for (std::uint32_t x = 0; x < kSide; ++x) {
        const auto [kRed, kGreen, kBlue] = at(pixels, x, kSide / 2);
        if (kRed > kGreen + 40 && kRed > kBlue + 40) {
            sum += static_cast<float>(x);
            ++count;
        }
    }
    return count == 0 ? std::nullopt : std::optional{sum / static_cast<float>(count)};
}

} // namespace

RAWFRAME_TEST(APosedModelIsDrawnWhereItsJointsPutIt) {
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
    render_scene_gpu::SceneRenderer& renderer = **made;
    const std::shared_ptr<const mesh::Mesh> kMesh = skinnedBox();
    const render_scene_gpu::MeshSource kMeshes = [&](std::uint64_t id) {
        return id == kSkinnedBox ? kMesh : render_scene::engineMesh(id);
    };

    // As bound: in the middle.
    SceneFrame frame = looking();
    frame.draws = {box(5, 0.5F, {1, 0, 0, 1}, kSkinnedBox)};
    const auto kBound = drawn(**framer, renderer, frame, kMeshes);
    RAWFRAME_EXPECT(kBound.has_value());
    if (!kBound.has_value()) {
        return;
    }
    const std::optional<float> kBoundMiddle = redMiddle(*kBound);

    // Posed: its joint two meters to the right in the model's space, a
    // meter in the World's at the box's half scale.
    frame.palette = {render_scene::Matrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 2, 0, 0, 1}};
    frame.draws[0].palette = 0;
    frame.draws[0].joints = 1;
    const auto kPosed =
        drawnWith(**framer, renderer, frame, kMeshes, &render_scene_gpu::RendererStatistics::modelsSkinned);
    RAWFRAME_EXPECT(kPosed.has_value());
    if (!kPosed.has_value()) {
        return;
    }
    const std::optional<float> kPosedMiddle = redMiddle(*kPosed);
    std::printf("red middle as bound %.1f, posed %.1f\n", kBoundMiddle.value_or(-1), kPosedMiddle.value_or(-1));
    RAWFRAME_EXPECT(kBoundMiddle.has_value() && kPosedMiddle.has_value());
    if (!kBoundMiddle.has_value() || !kPosedMiddle.has_value()) {
        return;
    }
    // A meter to the right five meters ahead, a quarter turn across the
    // view's 64 pixels: about six pixels.
    RAWFRAME_EXPECT(std::abs(*kBoundMiddle - (kSide / 2.0F)) < 1.5F);
    RAWFRAME_EXPECT(*kPosedMiddle > *kBoundMiddle + 4 && *kPosedMiddle < *kBoundMiddle + 9);
    RAWFRAME_EXPECT(renderer.statistics().modelsSkinned >= 1);
}
