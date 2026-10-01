// Multisampling on lavapipe (D343): the edge of an unlit white box turned
// across the view, on a black sky, takes grey between the two where the
// models' passes take four samples a pixel, and none where they take one;
// so too with the ambient occlusion, which reads the depth resolved.

#include "fixture.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <cstdio>

using namespace rawframe;
using namespace rawframe::scene_fixture;
using render_scene::SceneFrame;

RAWFRAME_TEST(MultisamplingSoftensAnEdge) {
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
    // An unlit white box four meters ahead, turned a twelfth of a turn
    // about the view's axis, and nothing else lit.
    SceneFrame frame = looking();
    frame.shadows.count = 0;
    frame.dither = false;
    frame.lights.sky = {0, 0, 0};
    frame.lights.ground = {0, 0, 0};
    render_scene::SceneDraw turned = box(4, 1, {1, 1, 1, 1});
    const float kCos = std::cos(0.5236F);
    const float kSin = std::sin(0.5236F);
    turned.model[0] = kCos;
    turned.model[1] = kSin;
    turned.model[4] = -kSin;
    turned.model[5] = kCos;
    turned.normal[0] = kCos;
    turned.normal[1] = kSin;
    turned.normal[4] = -kSin;
    turned.normal[5] = kCos;
    turned.material = 1;
    frame.draws = {turned};
    render_scene::MaterialBlob unlit = render_scene::noMaterial();
    unlit[15] = 1;
    frame.materials = {render_scene::noMaterial(), unlit};
    // The texels neither the box's nor the sky's.
    const auto kGrey = [](const std::vector<std::byte>& pixels) {
        int grey = 0;
        for (std::uint32_t y = 0; y < kSide; ++y) {
            for (std::uint32_t x = 0; x < kSide; ++x) {
                const int kGreen = at(pixels, x, y)[1];
                grey += kGreen > 40 && kGreen < 160 ? 1 : 0;
            }
        }
        return grey;
    };
    const auto kOne = drawn(**framer, **made, frame, kMeshes);
    frame.samples = 4;
    const auto kFour =
        drawnWith(**framer, **made, frame, kMeshes, &render_scene_gpu::RendererStatistics::framesMultisampled);
    RAWFRAME_EXPECT(kOne.has_value() && kFour.has_value());
    if (!kOne.has_value() || !kFour.has_value()) {
        return;
    }
    const int kOneGrey = kGrey(*kOne);
    const int kFourGrey = kGrey(*kFour);
    std::printf("grey texels: one sample %d, four %d; the box's middle %d\n",
                kOneGrey,
                kFourGrey,
                at(*kFour, kSide / 2, kSide / 2)[1]);
    RAWFRAME_EXPECT(kOneGrey == 0 && kFourGrey > 20 && at(*kFour, kSide / 2, kSide / 2)[1] > 180 &&
                    (*made)->statistics().framesMultisampled > 0);
    // With the ambient occlusion, which reads the depth: resolved first.
    frame.occlusion = render_scene::occlusionOf(render_scene::AmbientOcclusion{.radius = 1, .intensity = 1});
    const auto kOccluded =
        drawnWith(**framer, **made, frame, kMeshes, &render_scene_gpu::RendererStatistics::framesOccluded);
    RAWFRAME_EXPECT(kOccluded.has_value() && kGrey(*kOccluded) > 20);
}
