// The motion blur on lavapipe (D334): a box moving fast across the view
// smears its edges over several pixels with the blur (at 64 pixels high,
// its least reach of four either way) and over none without it, and a box
// standing still stays sharp with the blur on.

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

/// A red box four meters ahead, which moved `moved` meters right since the
/// frame before, with or without the blur.
SceneFrame moving(float moved, bool blurred) {
    SceneFrame frame = looking();
    frame.shadows.count = 0;
    frame.dither = false;
    render_scene::SceneDraw draw = box(4, 1, {1, 0, 0, 1});
    draw.previous = draw.model;
    draw.previous[12] -= moved;
    frame.draws = {draw};
    if (blurred) {
        frame.motionBlur = {.enabled = true, .shutter = 1};
    }
    return frame;
}

/// The pixels along the middle row neither the sky's red nor the box's.
int between(const std::vector<std::byte>& pixels) {
    const int kSky = at(pixels, 0, kSide / 2)[0];
    const int kBox = at(pixels, kSide / 2, kSide / 2)[0];
    int count = 0;
    for (std::uint32_t x = 0; x < kSide; ++x) {
        const int kRed = at(pixels, x, kSide / 2)[0];
        count += kRed > kSky + 8 && kRed < kBox - 8 ? 1 : 0;
    }
    return count;
}

} // namespace

RAWFRAME_TEST(AMovingBoxIsSmearedByMotionBlurAndAStillOneIsNot) {
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
    const auto kSharp = drawn(**framer, **made, moving(2, false), kMeshes);
    const auto kBlurred = drawnWith(
        **framer, **made, moving(2, true), kMeshes, &render_scene_gpu::RendererStatistics::framesMotionBlurred);
    const auto kStill = drawn(**framer, **made, moving(0, true), kMeshes);
    RAWFRAME_EXPECT(kSharp.has_value() && kBlurred.has_value() && kStill.has_value());
    if (!kSharp.has_value() || !kBlurred.has_value() || !kStill.has_value()) {
        return;
    }
    std::printf("between the sky and the box: moving %d, blurred %d, still and blurred %d; its middle %d, blurred %d\n",
                between(*kSharp),
                between(*kBlurred),
                between(*kStill),
                at(*kSharp, kSide / 2, kSide / 2)[0],
                at(*kBlurred, kSide / 2, kSide / 2)[0]);
    RAWFRAME_EXPECT(between(*kSharp) <= 2 && between(*kBlurred) >= 6 && between(*kStill) <= 2);
    RAWFRAME_EXPECT((*made)->statistics().framesMotionBlurred == 2);
}
