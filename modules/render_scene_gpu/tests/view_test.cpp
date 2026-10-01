// A render texture's view on the device (ADR-0052, D361) on lavapipe: a
// view draws its frame into its texture, and the player's view samples it
// where a material names the render texture; before the view has drawn,
// the material samples white; a split-screen view is placed in its
// region, and one drawn at a render scale is scaled to fill it (D373).
// Skips where no adapter answers, unless RAWFRAME_REQUIRE_GPU is set.

#include "fixture.h"
#include "rawframe/render/device.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/test/test.h"

#include <array>
#include <memory>
#include <optional>

using namespace rawframe;
using namespace rawframe::scene_fixture;
using render_scene::SceneDraw;
using render_scene::SceneFrame;

RAWFRAME_TEST(AViewsTextureIsSampledByTheSceneItShows) {
    const auto kDevice = opened();
    if (kDevice == nullptr) {
        return;
    }
    constexpr std::uint64_t kScreen = 0xd1;
    auto scene = render_scene_gpu::SceneRenderer::create(*kDevice);
    auto framer = render::Framer::create(*kDevice);
    RAWFRAME_EXPECT(scene.has_value() && framer.has_value());
    if (!scene.has_value() || !framer.has_value()) {
        return;
    }
    // Its renderer draws with the player's view's pipelines.
    auto view = render_scene_gpu::TextureView::create(*kDevice, **scene, kScreen, 32, 32);
    RAWFRAME_EXPECT(view.has_value());
    if (!view.has_value()) {
        return;
    }
    const render_scene_gpu::MeshSource kMeshes = [](std::uint64_t id) {
        return render_scene::engineMesh(id);
    };
    render_scene::MaterialBlob unlit = render_scene::noMaterial();
    unlit[15] = 1 + 2;
    // What the view sees: an unlit red wall, mapped linearly, filling it.
    SceneFrame seen = looking();
    seen.shadows.count = 0;
    seen.dither = false;
    seen.tonemapper = render_scene::Tonemapper::Linear;
    SceneDraw wall = box(4, 20, {1, 0, 0, 1});
    wall.material = 1;
    seen.draws = {wall};
    seen.materials = {render_scene::noMaterial(), unlit};
    seen.textures = {{}, {}};
    // What the player sees: an unlit white box whose material's color is
    // the render texture's.
    SceneFrame shown = looking();
    shown.shadows.count = 0;
    shown.dither = false;
    shown.tonemapper = render_scene::Tonemapper::Linear;
    SceneDraw screen = box(4, 1.5F, {1, 1, 1, 1});
    screen.material = 1;
    shown.draws = {screen};
    shown.materials = {render_scene::noMaterial(), unlit};
    shown.textures = {{}, {.base = {.id = kScreen}}};
    const std::array<render_scene_gpu::TextureView*, 1> kViews = {view->get()};
    const std::array<render::FrameRecorder*, 2> kRecorders = {view->get(), scene->get()};
    bool missed = false;
    const auto kDraw = [&](const SceneFrame* drawnInto) {
        (**view).prepare(drawnInto, kMeshes);
        missed = (**view).missed();
        (**scene).prepare(&shown, kMeshes, {}, kViews);
        RAWFRAME_EXPECT((*framer)->finish(5'000'000'000).has_value());
        RAWFRAME_EXPECT(
            (*framer)->make(kRecorders, {.width = kSide, .height = kSide, .readBack = true}).value_or(false));
        RAWFRAME_EXPECT((*framer)->finish(5'000'000'000).has_value());
        return (*framer)->pixels();
    };
    // Before the view has drawn, white.
    std::optional<std::vector<std::byte>> pixels;
    for (int attempt = 0; attempt < 1000 && (**scene).statistics().frames == 0; ++attempt) {
        pixels = kDraw(nullptr);
    }
    RAWFRAME_EXPECT(pixels.has_value() && !(**view).picture().has_value());
    if (pixels.has_value()) {
        const std::array<int, 3> kWhite = at(*pixels, 32, 32);
        RAWFRAME_EXPECT(kWhite[0] > 200 && kWhite[1] > 200 && kWhite[2] > 200);
    }
    // A frame given it that no frame drew is told as missed by the next.
    (**view).prepare(&seen, kMeshes);
    const std::array<render::FrameRecorder*, 1> kWithout = {scene->get()};
    RAWFRAME_EXPECT((*framer)->make(kWithout, {.width = kSide, .height = kSide}).value_or(false));
    RAWFRAME_EXPECT((*framer)->finish(5'000'000'000).has_value());
    pixels = kDraw(&seen);
    RAWFRAME_EXPECT(missed);
    // Once it has drawn, its red, and nothing missed.
    for (int attempt = 0; attempt < 1000 && (**view).statistics().frames == 0; ++attempt) {
        pixels = kDraw(&seen);
    }
    pixels = kDraw(&seen);
    RAWFRAME_EXPECT(!missed);
    RAWFRAME_EXPECT(pixels.has_value() && (**view).picture().has_value());
    if (pixels.has_value()) {
        const std::array<int, 3> kRed = at(*pixels, 32, 32);
        RAWFRAME_EXPECT(kRed[0] > 200 && kRed[1] < 40 && kRed[2] < 40);
    }
    // A frame in which no view names it keeps what it last drew.
    pixels = kDraw(nullptr);
    if (pixels.has_value()) {
        const std::array<int, 3> kKept = at(*pixels, 32, 32);
        RAWFRAME_EXPECT(kKept[0] > 200 && kKept[1] < 40 && kKept[2] < 40);
    }
}

RAWFRAME_TEST(SplitScreenViewsArePlacedInTheirRegions) {
    // Two local players' views (D362), each drawn apart and copied into its
    // half of the frame's picture, the first clearing it.
    const auto kDevice = opened();
    if (kDevice == nullptr) {
        return;
    }
    auto scene = render_scene_gpu::SceneRenderer::create(*kDevice);
    auto framer = render::Framer::create(*kDevice);
    RAWFRAME_EXPECT(scene.has_value() && framer.has_value());
    if (!scene.has_value() || !framer.has_value()) {
        return;
    }
    auto left = render_scene_gpu::TextureView::create(*kDevice, **scene, 0, 16, 16);
    auto right = render_scene_gpu::TextureView::create(*kDevice, **scene, 0, 16, 16);
    RAWFRAME_EXPECT(left.has_value() && right.has_value());
    if (!left.has_value() || !right.has_value()) {
        return;
    }
    // Each its region's size from the next frame.
    // The right letterboxed (D369): half as tall, between blue bars.
    RAWFRAME_EXPECT((**left).resize(kSide / 2, kSide).has_value() &&
                    (**right).resize(kSide / 2, kSide / 2).has_value() && (**right).width() == kSide / 2);
    const render_scene_gpu::MeshSource kMeshes = [](std::uint64_t id) {
        return render_scene::engineMesh(id);
    };
    render_scene::MaterialBlob unlit = render_scene::noMaterial();
    unlit[15] = 1 + 2;
    const auto kWall = [&](std::array<float, 4> color) {
        SceneFrame seen = looking();
        seen.shadows.count = 0;
        seen.dither = false;
        seen.tonemapper = render_scene::Tonemapper::Linear;
        SceneDraw wall = box(4, 20, color);
        wall.material = 1;
        seen.draws = {wall};
        seen.materials = {render_scene::noMaterial(), unlit};
        seen.textures = {{}, {}};
        return seen;
    };
    const SceneFrame kRed = kWall({1, 0, 0, 1});
    const SceneFrame kGreen = kWall({0, 1, 0, 1});
    const std::array<render::FrameRecorder*, 3> kRecorders = {left->get(), right->get(), scene->get()};
    std::optional<std::vector<std::byte>> pixels;
    for (int attempt = 0; attempt < 1000 && ((**left).statistics().frames == 0 || (**right).statistics().frames == 0);
         ++attempt) {
        constexpr std::array<std::uint8_t, 3> kBlue{0, 0, 255};
        (**left).prepare(&kRed, kMeshes, {}, {}, render_scene_gpu::Placement{.x = 0, .y = 0, .bars = kBlue});
        (**right).prepare(
            &kGreen, kMeshes, {}, {}, render_scene_gpu::Placement{.x = kSide / 2, .y = kSide / 4, .bars = kBlue});
        (**scene).prepare(nullptr, kMeshes);
        RAWFRAME_EXPECT(
            (*framer)->make(kRecorders, {.width = kSide, .height = kSide, .readBack = true}).value_or(false));
        RAWFRAME_EXPECT((*framer)->finish(5'000'000'000).has_value());
        pixels = (*framer)->pixels();
    }
    RAWFRAME_EXPECT(pixels.has_value());
    if (pixels.has_value()) {
        const std::array<int, 3> kLeft = at(*pixels, kSide / 4, kSide / 2);
        const std::array<int, 3> kRight = at(*pixels, kSide * 3 / 4, kSide / 2);
        RAWFRAME_EXPECT(kLeft[0] > 200 && kLeft[1] < 40 && kLeft[2] < 40);
        RAWFRAME_EXPECT(kRight[0] < 40 && kRight[1] > 200 && kRight[2] < 40);
        const std::array<int, 3> kBar = at(*pixels, kSide * 3 / 4, 2);
        RAWFRAME_EXPECT(kBar[0] == 0 && kBar[1] == 0 && kBar[2] >= 254);
    }
}

RAWFRAME_TEST(AViewDrawnAtItsRenderScaleFillsItsRegion) {
    // A view drawn at half its region's size each way (D373), scaled into
    // the whole frame's picture: red to its corners.
    const auto kDevice = opened();
    if (kDevice == nullptr) {
        return;
    }
    auto scene = render_scene_gpu::SceneRenderer::create(*kDevice);
    auto framer = render::Framer::create(*kDevice);
    RAWFRAME_EXPECT(scene.has_value() && framer.has_value());
    if (!scene.has_value() || !framer.has_value()) {
        return;
    }
    auto view = render_scene_gpu::TextureView::create(*kDevice, **scene, 0, kSide / 2, kSide / 2);
    RAWFRAME_EXPECT(view.has_value());
    if (!view.has_value()) {
        return;
    }
    const render_scene_gpu::MeshSource kMeshes = [](std::uint64_t id) {
        return render_scene::engineMesh(id);
    };
    render_scene::MaterialBlob unlit = render_scene::noMaterial();
    unlit[15] = 1 + 2;
    SceneFrame red = looking();
    red.shadows.count = 0;
    red.dither = false;
    red.tonemapper = render_scene::Tonemapper::Linear;
    SceneDraw wall = box(4, 20, {1, 0, 0, 1});
    wall.material = 1;
    red.draws = {wall};
    red.materials = {render_scene::noMaterial(), unlit};
    red.textures = {{}, {}};
    const std::array<render::FrameRecorder*, 2> kRecorders = {view->get(), scene->get()};
    std::optional<std::vector<std::byte>> pixels;
    const auto kRed = [](const std::array<int, 3>& color) {
        return color[0] > 200 && color[1] < 40 && color[2] < 40;
    };
    for (int attempt = 0; attempt < 1000 &&
                          (!pixels.has_value() || !kRed(at(*pixels, 1, 1)) || !kRed(at(*pixels, kSide - 2, kSide - 2)));
         ++attempt) {
        (**view).prepare(
            &red, kMeshes, {}, {}, render_scene_gpu::Placement{.x = 0, .y = 0, .width = kSide, .height = kSide});
        (**scene).prepare(nullptr, kMeshes);
        RAWFRAME_EXPECT(
            (*framer)->make(kRecorders, {.width = kSide, .height = kSide, .readBack = true}).value_or(false));
        RAWFRAME_EXPECT((*framer)->finish(5'000'000'000).has_value());
        pixels = (*framer)->pixels();
    }
    RAWFRAME_EXPECT(pixels.has_value());
    if (pixels.has_value()) {
        RAWFRAME_EXPECT(kRed(at(*pixels, 1, 1)) && kRed(at(*pixels, kSide - 2, kSide - 2)) &&
                        kRed(at(*pixels, kSide / 2, kSide / 2)));
    }
}
