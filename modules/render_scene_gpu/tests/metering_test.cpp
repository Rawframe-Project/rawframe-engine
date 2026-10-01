// Centered metering on lavapipe (D345): a sunlit white box in the middle
// of a dim sky. Every pixel counted alike, the box's few bright ones fall
// in the brightest tenth left out and the sky sets the exposure; with the
// middle counting more, the box does, and the sky shows darker.

#include "fixture.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/test/test.h"

#include <cstdio>

using namespace rawframe;
using namespace rawframe::scene_fixture;
using render_scene::SceneFrame;

RAWFRAME_TEST(CenteredMeteringCountsTheMiddleMore) {
    const auto kDevice = opened();
    if (kDevice == nullptr) {
        return;
    }
    auto framer = render::Framer::create(*kDevice);
    RAWFRAME_EXPECT(framer.has_value());
    if (!framer.has_value()) {
        return;
    }
    const render_scene_gpu::MeshSource kMeshes = [](std::uint64_t id) {
        return render_scene::engineMesh(id);
    };
    SceneFrame frame = looking();
    frame.lights.sun = {100000, 100000, 100000};
    frame.lights.toSun = {0, 0, 1};
    frame.lights.ground = {0, 0, 0};
    frame.lights.sky = {50, 50, 50};
    frame.shadows.count = 0;
    frame.dither = false;
    frame.draws = {box(4, 1, {1, 1, 1, 1})};
    // The sky's corner as drawn with the exposure the first frame measured.
    const auto kCorner = [&](float centered) {
        auto made = render_scene_gpu::SceneRenderer::create(*kDevice);
        RAWFRAME_EXPECT(made.has_value());
        if (!made.has_value()) {
            return -1;
        }
        frame.metering = {.enabled = true,
                          .settings = {.minimum = 0,
                                       .maximum = 20,
                                       .brighten = 10,
                                       .darken = 10,
                                       .compensation = 0,
                                       .low = 0.1F,
                                       .high = 0.9F,
                                       .centered = centered},
                          .elapsed = 0,
                          .snap = true};
        static_cast<void>(drawn(**framer, **made, frame, kMeshes));
        frame.metering.snap = false;
        const auto kPixels = drawn(**framer, **made, frame, kMeshes);
        RAWFRAME_EXPECT(kPixels.has_value());
        return kPixels.has_value() ? at(*kPixels, 2, 2)[0] : -1;
    };
    const int kEvenly = kCorner(0);
    const int kCentered = kCorner(1);
    std::printf("the sky's corner: every pixel alike %d, the middle counting more %d\n", kEvenly, kCentered);
    RAWFRAME_EXPECT(kEvenly > 60 && kCentered + 20 < kEvenly);
}
