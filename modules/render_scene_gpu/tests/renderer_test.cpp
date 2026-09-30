// The scene's GPU half (D284) on lavapipe: a frame the queue stage built
// drawn into a target and read back. A box ahead is drawn where the view
// puts it, over the sky; a nearer model hides a farther one whatever their
// order (the depth prepass, reversed-Z); the sun lights what faces it; a
// draw whose mesh is not given is left out; and a mesh is uploaded once,
// its draws of one mesh one instanced call. Skips where no adapter
// answers, unless RAWFRAME_REQUIRE_GPU is set. Frames are made by
// `render`'s framer (D285), as the frame participant makes them.

#include "rawframe/render/device.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/test/test.h"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace rawframe;
using render_scene::SceneDraw;
using render_scene::SceneFrame;

namespace {

bool required() {
    const char* value = std::getenv("RAWFRAME_REQUIRE_GPU");
    return value != nullptr && value[0] != '\0';
}

std::unique_ptr<render::Device> opened() {
    auto device = render::Device::request({.allowSoftware = true});
    if (!device.has_value()) {
        return nullptr;
    }
    for (int poll = 0; poll < 1000; ++poll) {
        const auto kOpen = (*device)->open();
        if (!kOpen.has_value()) {
            RAWFRAME_EXPECT(!required());
            std::puts("skip: no adapter");
            return nullptr;
        }
        if (*kOpen) {
            return std::move(*device);
        }
    }
    return nullptr;
}

constexpr std::uint32_t kSide = 64;
constexpr std::uint64_t kMissing = 9;

/// A view from the origin along -Z, a square field a quarter turn high, as
/// the queue stage would make it, with the engine's default light.
SceneFrame looking() {
    const auto kSchema = *schema::RegistryBuilder{}.freeze();
    auto scene = *render_scene::Scene::create(*kSchema, {});
    return scene->queue({.fovY = 1.5707964F, .near = 0.1F, .exposure = 15, .aspect = 1});
}

/// A box of half sides `half` at `z` meters ahead, in sRGB `color`.
SceneDraw box(float z, float half, std::array<float, 4> color, std::uint64_t mesh = render_scene::kBox) {
    SceneDraw draw{.mesh = mesh, .color = color};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        draw.model[(axis * 4) + axis] = half;
        draw.normal[(axis * 4) + axis] = 1 / half;
    }
    draw.model[14] = -z;
    draw.model[15] = 1;
    return draw;
}

std::array<int, 3> at(const std::vector<std::byte>& pixels, std::uint32_t x, std::uint32_t y) {
    const std::size_t kAt = (std::size_t{y} * kSide + x) * 4;
    return {std::to_integer<int>(pixels[kAt]),
            std::to_integer<int>(pixels[kAt + 1]),
            std::to_integer<int>(pixels[kAt + 2])};
}

/// One frame drawn and read back through `render`'s framer (D285); the
/// pipelines are made as the device answers, a few frames at most.
std::optional<std::vector<std::byte>> drawn(render::Framer& framer,
                                            render_scene_gpu::SceneRenderer& renderer,
                                            const SceneFrame& frame,
                                            const render_scene_gpu::MeshSource& meshes) {
    const std::array<render::FrameRecorder*, 1> kRecorders = {&renderer};
    const std::uint64_t kBefore = renderer.statistics().frames;
    for (int attempt = 0; attempt < 1000 && renderer.statistics().frames == kBefore; ++attempt) {
        renderer.prepare(&frame, meshes);
        RAWFRAME_EXPECT(framer.finish(5'000'000'000).has_value());
        const auto kMade = framer.make(kRecorders, {.width = kSide, .height = kSide, .readBack = true});
        RAWFRAME_EXPECT(kMade.has_value());
        if (!kMade.has_value()) {
            std::printf("%s\n", std::string{kMade.error().description()}.c_str());
            return std::nullopt;
        }
    }
    RAWFRAME_EXPECT(framer.finish(5'000'000'000).has_value());
    return framer.pixels();
}

} // namespace

RAWFRAME_TEST(TheSceneDrawsItsModelsInDepth) {
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
    std::uint64_t asked = 0;
    const render_scene_gpu::MeshSource kMeshes = [&](std::uint64_t id) {
        ++asked;
        return render_scene::engineMesh(id);
    };

    // A red box a meter across five meters ahead, and a green one six
    // across behind it, drawn after it: the red one hides the green one's
    // middle, and the sky is behind both.
    SceneFrame frame = looking();
    frame.draws = {box(5, 0.5F, {1, 0, 0, 1}), box(8, 3, {0, 1, 0, 1}), box(5, 1, {1, 1, 1, 1}, kMissing)};
    const auto kPixels = drawn(**framer, renderer, frame, kMeshes);
    RAWFRAME_EXPECT(kPixels.has_value() && kPixels->size() == std::size_t{kSide} * kSide * 4);
    if (!kPixels.has_value() || kPixels->size() != std::size_t{kSide} * kSide * 4) {
        return;
    }
    const auto [kRed, kRedGreen, kRedBlue] = at(*kPixels, kSide / 2, kSide / 2);
    const auto [kGreenRed, kGreen, kGreenBlue] = at(*kPixels, kSide / 2, kSide / 2 + 12);
    const auto [kSkyRed, kSkyGreen, kSkyBlue] = at(*kPixels, 0, 0);
    std::printf("middle %d %d %d, below %d %d %d, corner %d %d %d\n",
                kRed,
                kRedGreen,
                kRedBlue,
                kGreenRed,
                kGreen,
                kGreenBlue,
                kSkyRed,
                kSkyGreen,
                kSkyBlue);
    RAWFRAME_EXPECT(kRed > kRedGreen + 40 && kRed > kRedBlue + 40);
    RAWFRAME_EXPECT(kGreen > kGreenRed + 40 && kGreen > kGreenBlue + 40);
    // The white sky, grey after exposure, and not black.
    RAWFRAME_EXPECT(kSkyRed > 20 && std::abs(kSkyRed - kSkyGreen) < 4 && std::abs(kSkyRed - kSkyBlue) < 4);
    // The two boxes are one instanced draw; the missing mesh's model is left
    // out.
    const render_scene_gpu::RendererStatistics& kCounts = renderer.statistics();
    RAWFRAME_EXPECT(kCounts.frames == 1 && kCounts.models == 2 && kCounts.drawCalls == 1 &&
                    kCounts.modelsLeftOut == 1 && kCounts.meshesUploaded == 1);

    // Without the sun, the red box's face toward the eye is lit by the sky
    // alone: darker. Its mesh is not asked for or uploaded again.
    const std::uint64_t kAsked = asked;
    SceneFrame unlit = frame;
    unlit.lights.sun = {0, 0, 0};
    unlit.draws.resize(1);
    const auto kDark = drawn(**framer, renderer, unlit, kMeshes);
    RAWFRAME_EXPECT(kDark.has_value());
    if (kDark.has_value()) {
        const auto [kDarkRed, kDarkGreen, kDarkBlue] = at(*kDark, kSide / 2, kSide / 2);
        RAWFRAME_EXPECT(kDarkRed + 10 < kRed && kDarkRed > kDarkGreen);
    }
    RAWFRAME_EXPECT(renderer.statistics().meshesUploaded == 1 && asked == kAsked);
}

RAWFRAME_TEST(AnEmptySceneIsTheSky) {
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
    const auto kPixels = drawn(**framer, **made, looking(), {});
    RAWFRAME_EXPECT(kPixels.has_value());
    if (kPixels.has_value()) {
        RAWFRAME_EXPECT(at(*kPixels, 0, 0) == at(*kPixels, kSide - 1, kSide - 1) && at(*kPixels, 0, 0)[0] > 20);
    }
    // A picture past the limit is refused.
    const std::array<render::FrameRecorder*, 1> kRecorders = {made->get()};
    RAWFRAME_EXPECT(!(*framer)->make(kRecorders, {.width = 1U << 20U, .height = 1}).has_value());
}
