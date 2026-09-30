// The scene's GPU half (D284) on lavapipe: a frame the queue stage built
// drawn into a target and read back. A box ahead is drawn where the view
// puts it, over the sky; a nearer model hides a farther one whatever their
// order (the depth prepass, reversed-Z); the sun lights what faces it; a
// draw whose mesh is not given is left out; a mesh is uploaded once, its
// draws of one mesh one instanced call; the sun casts shadows; a point
// light lights what is near it, a spot only what its cone reaches, and
// both cast shadows through their squares of one atlas; a metered camera
// finds the exposure the scene's light asks for; a camera's grade
// brightens, warms, and greys the picture as asked; its tonemapper keeps
// middle grey where AgX puts it; and,
// antialiased over time, an edge's texels blend what the jittered frames
// saw of it, and a moving box leaves no ghost where it was. Skips where no
// adapter answers, unless RAWFRAME_REQUIRE_GPU is set. Frames are made by
// `render`'s framer (D285), as the frame participant makes them.

#include "fixture.h"
#include "rawframe/render/device.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <numbers>
#include <optional>

using namespace rawframe;
using render_scene::SceneDraw;
using render_scene::SceneFrame;
using namespace rawframe::scene_fixture;

namespace {

constexpr std::uint64_t kMissing = 9;

/// A dark room's frame (D292): a white floor two meters below the eye, a
/// half-meter box standing on it eight ahead, and a light to its right
/// casting its shadow left: a spot three meters up shining down, or a lamp
/// at the eye's height; one cluster holding it, and its squares of a small
/// atlas taking both models, as the queue stage would place them; exposed
/// for a dim room.
SceneFrame lamplit(bool spot) {
    SceneFrame frame = looking();
    frame.lights.sun = {0, 0, 0};
    frame.lights.ground = {0, 0, 0};
    frame.lights.sky = {0, 0, 0};
    frame.exposure = 0;
    frame.shadows.count = 0;
    SceneDraw floor = box(8, 1, {1, 1, 1, 1});
    floor.model[0] = 10;
    floor.model[5] = 0.1F;
    floor.model[10] = 10;
    floor.normal[0] = 0.1F;
    floor.normal[5] = 10;
    floor.normal[10] = 0.1F;
    floor.model[13] = -2;
    SceneDraw cube = box(8, 0.5F, {1, 1, 1, 1});
    cube.model[13] = -1.4F;
    frame.draws = {floor, cube};
    render_scene::SceneLight light{.position = {2, spot ? 1.0F : 0.0F, -8}, .range = 10};
    const float kCandela = spot ? 300 / std::numbers::pi_v<float> : 1200 / (4 * std::numbers::pi_v<float>);
    light.intensity = {kCandela, kCandela, kCandela};
    if (spot) {
        light.spot = true;
        light.direction = {0, -1, 0};
        light.cosInner = std::cos(0.9F);
        light.cosOuter = std::cos(1.1F);
    }
    light.shadowSlots = spot ? 1 : 6;
    frame.lights3d = {light};
    frame.clusters = {.tilesX = 1, .tilesY = 1, .slices = 1, .ranges = {0, 1}, .indices = {0}};
    frame.lightShadows.side = 512;
    frame.lightShadows.casters = {floor, cube};
    for (std::uint32_t face = 0; face < light.shadowSlots; ++face) {
        render_scene::ShadowSlot slot = render_scene::shadowSlotOf(light, face);
        slot.x = (face % 4) * 128;
        slot.y = (face / 4) * 128;
        slot.side = 128;
        slot.casterCount = 2;
        frame.lightShadows.slots.push_back(slot);
    }
    return frame;
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
    // The clear sky behind them, blue, and not black.
    RAWFRAME_EXPECT(kSkyRed > 20 && kSkyBlue > kSkyGreen && kSkyGreen > kSkyRed);
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
    unlit.lights.ground = {0, 0, 0};
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
    // Undithered, the sky is one color; dithered (D332), each channel
    // takes the two eight-bit steps about it somewhere, and never another.
    render_scene::SceneFrame plain = looking();
    plain.dither = false;
    const auto kPlain = drawn(**framer, **made, plain, {});
    const auto kDithered = drawn(**framer, **made, looking(), {});
    RAWFRAME_EXPECT(kPlain.has_value() && kDithered.has_value());
    if (!kPlain.has_value() || !kDithered.has_value()) {
        return;
    }
    const std::array<int, 3> kSky = at(*kPlain, 0, 0);
    RAWFRAME_EXPECT(kSky == at(*kPlain, kSide - 1, kSide - 1) && kSky[0] > 20);
    std::array<int, 3> lowest = kSky;
    std::array<int, 3> highest = kSky;
    for (std::uint32_t y = 0; y < kSide; ++y) {
        for (std::uint32_t x = 0; x < kSide; ++x) {
            for (std::size_t channel = 0; channel < 3; ++channel) {
                lowest[channel] = std::min(lowest[channel], at(*kDithered, x, y)[channel]);
                highest[channel] = std::max(highest[channel], at(*kDithered, x, y)[channel]);
            }
        }
    }
    std::printf("the sky %d %d %d; dithered from %d %d %d to %d %d %d\n",
                kSky[0],
                kSky[1],
                kSky[2],
                lowest[0],
                lowest[1],
                lowest[2],
                highest[0],
                highest[1],
                highest[2]);
    for (std::size_t channel = 0; channel < 3; ++channel) {
        RAWFRAME_EXPECT(highest[channel] - lowest[channel] == 1 && lowest[channel] >= kSky[channel] - 1 &&
                        highest[channel] <= kSky[channel] + 1);
    }
    // A picture past the limit is refused.
    const std::array<render::FrameRecorder*, 1> kRecorders = {made->get()};
    RAWFRAME_EXPECT(!(*framer)->make(kRecorders, {.width = 1U << 20U, .height = 1}).has_value());
}

RAWFRAME_TEST(TheSunCastsShadows) {
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
    // A white floor two meters below the eye, and a box above it eight
    // meters ahead; the default sun, travelling along (-0.3, -1, -0.4),
    // lays the box's shadow on the floor behind it and to its left.
    SceneFrame frame = looking();
    RAWFRAME_EXPECT(frame.shadows.count == 4);
    SceneDraw floor = box(8, 1, {1, 1, 1, 1});
    floor.model[0] = 10;
    floor.model[5] = 0.1F;
    floor.model[10] = 10;
    floor.normal[0] = 0.1F;
    floor.normal[5] = 10;
    floor.normal[10] = 0.1F;
    floor.model[13] = -2;
    frame.draws = {floor, box(8, 0.5F, {1, 1, 1, 1})};
    // Both cast into every cascade.
    frame.shadows.casters = frame.draws;
    for (std::size_t at = 0; at < frame.shadows.count; ++at) {
        frame.shadows.cascades[at].firstCaster = 0;
        frame.shadows.cascades[at].casterCount = 2;
    }
    const render_scene_gpu::MeshSource kMeshes = [](std::uint64_t id) {
        return render_scene::engineMesh(id);
    };
    const auto kPixels = drawn(**framer, **made, frame, kMeshes);
    RAWFRAME_EXPECT(kPixels.has_value());
    if (!kPixels.has_value()) {
        return;
    }
    // Where the floor is in the box's shadow, and where it is not: the
    // floor's top at 1.9 below the eye, 8.76 ahead, 0.57 left or 1.5 right.
    const auto kShaded = at(*kPixels, 30, 39);
    const auto kLit = at(*kPixels, 37, 39);
    std::printf("shaded %d %d %d, lit %d %d %d\n", kShaded[0], kShaded[1], kShaded[2], kLit[0], kLit[1], kLit[2]);
    RAWFRAME_EXPECT(kShaded[0] + 30 < kLit[0]);
    // Without cascades, the same floor is lit everywhere.
    frame.shadows.count = 0;
    const auto kUnshaded = drawn(**framer, **made, frame, kMeshes);
    RAWFRAME_EXPECT(kUnshaded.has_value() && std::abs(at(*kUnshaded, 30, 39)[0] - kLit[0]) < 6);
}

RAWFRAME_TEST(PointAndSpotLightsLightWhatTheyReach) {
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
    // The floor of the shadows' test in the dark, and a lamp of ten
    // candela a meter above it, eight ahead, the view exposed for a dim
    // room; one cluster holds it, as the queue stage's tests prove the
    // clusters themselves.
    SceneFrame frame = looking();
    frame.lights.sun = {0, 0, 0};
    frame.lights.ground = {0, 0, 0};
    frame.lights.sky = {0, 0, 0};
    frame.exposure = 0;
    frame.shadows.count = 0;
    SceneDraw floor = box(8, 1, {1, 1, 1, 1});
    floor.model[0] = 10;
    floor.model[5] = 0.1F;
    floor.model[10] = 10;
    floor.normal[0] = 0.1F;
    floor.normal[5] = 10;
    floor.normal[10] = 0.1F;
    floor.model[13] = -2;
    frame.draws = {floor};
    const render_scene_gpu::MeshSource kMeshes = [](std::uint64_t id) {
        return render_scene::engineMesh(id);
    };
    const auto kDark = drawn(**framer, **made, frame, kMeshes);
    frame.lights3d = {{.position = {0, -1, -8}, .range = 10, .intensity = {10, 10, 10}}};
    frame.clusters = {.tilesX = 1, .tilesY = 1, .slices = 1, .ranges = {0, 1}, .indices = {0}};
    const auto kLamp = drawn(**framer, **made, frame, kMeshes);
    // A spot there shining down lights the floor below it; shining up, not.
    frame.lights3d[0].spot = true;
    frame.lights3d[0].direction = {0, -1, 0};
    frame.lights3d[0].cosInner = std::cos(0.3F);
    frame.lights3d[0].cosOuter = std::cos(0.6F);
    const auto kDown = drawn(**framer, **made, frame, kMeshes);
    frame.lights3d[0].direction = {0, 1, 0};
    const auto kUp = drawn(**framer, **made, frame, kMeshes);
    RAWFRAME_EXPECT(kDark.has_value() && kLamp.has_value() && kDown.has_value() && kUp.has_value());
    if (!kDark.has_value() || !kLamp.has_value() || !kDown.has_value() || !kUp.has_value()) {
        return;
    }
    // The floor below the lamp, 1.9 below the eye and 8 ahead, and its
    // far corner, beyond the lamp's reach.
    const auto kBelow = at(*kLamp, 32, 39);
    std::printf("dark %d, lamp %d far %d, spot down %d up %d\n",
                at(*kDark, 32, 39)[0],
                kBelow[0],
                at(*kLamp, 2, 34)[0],
                at(*kDown, 32, 39)[0],
                at(*kUp, 32, 39)[0]);
    RAWFRAME_EXPECT(at(*kDark, 32, 39)[0] < 10 && kBelow[0] > 100 && at(*kLamp, 2, 34)[0] < kBelow[0] / 2);
    RAWFRAME_EXPECT(at(*kDown, 32, 39)[0] > 100 && at(*kUp, 32, 39)[0] < 10);
}

RAWFRAME_TEST(TemporalAntiAliasingBlendsEdgesWithoutGhosts) {
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
    // A box whose near face's left edge falls a quarter texel right of
    // texel 27's center, on the middle row, still: its motion nought.
    SceneFrame frame = looking(true);
    RAWFRAME_EXPECT(frame.temporal.enabled && !frame.temporal.history);
    frame.shadows.count = 0;
    // Pixels compared exactly: undithered.
    frame.dither = false;
    SceneDraw still = box(8, 0.9379F, {1, 1, 1, 1});
    still.previous = still.model;
    frame.draws = {still};
    SceneFrame alone = frame;
    alone.temporal.enabled = false;
    const auto kAlone = drawn(**framer, **made, alone, kMeshes);
    RAWFRAME_EXPECT(kAlone.has_value());
    if (!kAlone.has_value()) {
        return;
    }
    const int kSky = at(*kAlone, 20, 32)[0];
    const int kBox = at(*kAlone, 32, 32)[0];
    RAWFRAME_EXPECT(at(*kAlone, 27, 32)[0] == kSky && at(*kAlone, 28, 32)[0] == kBox);
    // Twenty-four jittered frames, each reusing the picture before: the
    // edge's texel shows the box's share of it.
    std::optional<std::vector<std::byte>> blended;
    for (std::uint64_t index = 0; index < 24; ++index) {
        frame.temporal.jitter = render_scene::temporalJitter(index);
        frame.temporal.history = index > 0;
        blended = drawn(**framer, **made, frame, kMeshes);
    }
    RAWFRAME_EXPECT(blended.has_value());
    if (!blended.has_value()) {
        return;
    }
    const int kEdge = at(*blended, 27, 32)[0];
    std::printf("sky %d, box %d, the edge alone %d, blended %d, inside %d\n",
                kSky,
                kBox,
                at(*kAlone, 27, 32)[0],
                kEdge,
                at(*blended, 32, 32)[0]);
    RAWFRAME_EXPECT(kEdge > kSky + 10 && kEdge < kBox - 10 && std::abs(at(*blended, 32, 32)[0] - kBox) < 4 &&
                    std::abs(at(*blended, 20, 32)[0] - kSky) < 4);
    RAWFRAME_EXPECT((*made)->statistics().framesResolved >= 24 && (*made)->statistics().historyReused >= 23);
    // The box moves two meters right in one frame: where it was is the sky
    // again at once, and where it is, the box.
    SceneDraw moved = still;
    moved.model[12] = 2;
    frame.draws = {moved};
    frame.temporal.jitter = render_scene::temporalJitter(24);
    const auto kMoved = drawn(**framer, **made, frame, kMeshes);
    RAWFRAME_EXPECT(kMoved.has_value());
    if (!kMoved.has_value()) {
        return;
    }
    std::printf("where it was %d, where it is %d\n", at(*kMoved, 30, 32)[0], at(*kMoved, 42, 32)[0]);
    RAWFRAME_EXPECT(std::abs(at(*kMoved, 30, 32)[0] - kSky) < 6 && std::abs(at(*kMoved, 42, 32)[0] - kBox) < 6);
}

RAWFRAME_TEST(PunctualLightsCastShadowsThroughTheirAtlas) {
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
    for (const bool kSpot : {true, false}) {
        // The floor a meter left of the box, in its shadow, and two and a
        // half right, lit; then the same light without shadows.
        SceneFrame frame = lamplit(kSpot);
        RAWFRAME_EXPECT(frame.lights3d.size() == 1 && frame.lightShadows.slots.size() == (kSpot ? 1U : 6U));
        const auto kShadowed = drawn(**framer, **made, frame, kMeshes);
        frame.lights3d[0].shadowSlots = 0;
        const auto kUnshadowed = drawn(**framer, **made, frame, kMeshes);
        RAWFRAME_EXPECT(kShadowed.has_value() && kUnshadowed.has_value());
        if (!kShadowed.has_value() || !kUnshadowed.has_value()) {
            return;
        }
        const int kInShadow = at(*kShadowed, 27, 39)[0];
        const int kWithout = at(*kUnshadowed, 27, 39)[0];
        const int kLit = at(*kShadowed, 42, 39)[0];
        std::printf("%s: in its shadow %d, without shadows %d; lit %d and %d\n",
                    kSpot ? "spot" : "lamp",
                    kInShadow,
                    kWithout,
                    kLit,
                    at(*kUnshadowed, 42, 39)[0]);
        RAWFRAME_EXPECT(kWithout > 60 && kInShadow < kWithout / 3 && kLit > 60 &&
                        std::abs(kLit - at(*kUnshadowed, 42, 39)[0]) < 4);
    }
}

RAWFRAME_TEST(AMeteredExposureFindsTheScenesLight) {
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
    // A dim sky of 50 candela per square meter and nothing else, the camera
    // starting from a sunny day's EV100 of 15, metered at ten EV a second,
    // a tenth of a second a frame.
    SceneFrame frame = looking();
    frame.lights.sun = {0, 0, 0};
    frame.lights.ground = {0, 0, 0};
    frame.lights.sky = {50, 50, 50};
    frame.shadows.count = 0;
    frame.metering = {
        .enabled = true,
        .settings =
            {.minimum = 0, .maximum = 20, .brighten = 10, .darken = 10, .compensation = 0, .low = 0.1F, .high = 0.9F},
        .elapsed = 0.1F};
    std::vector<int> shades;
    for (int drawnFrames = 0; drawnFrames < 14; ++drawnFrames) {
        const auto kPixels = drawn(**framer, **made, frame, kMeshes);
        RAWFRAME_EXPECT(kPixels.has_value());
        if (!kPixels.has_value()) {
            return;
        }
        shades.push_back(at(*kPixels, 32, 32)[0]);
    }
    // The sky's luminance is 2^5.64: an EV100 of 5.64 + log2(100 / 12.5),
    // reached a stop a frame and then held; the picture as a camera set to
    // it by hand shows it.
    SceneFrame fixed = frame;
    fixed.metering = {};
    fixed.exposure = std::log2(50.0F) + 3;
    auto plain = render_scene_gpu::SceneRenderer::create(*kDevice);
    const auto kFixed = drawn(**framer, **plain, fixed, kMeshes);
    RAWFRAME_EXPECT(kFixed.has_value());
    if (!kFixed.has_value()) {
        return;
    }
    std::printf("metered shades:");
    for (const int kShade : shades) {
        std::printf(" %d", kShade);
    }
    std::printf("; by hand %d\n", at(*kFixed, 32, 32)[0]);
    RAWFRAME_EXPECT(shades.front() < 10 && shades[3] > shades[1] && shades[5] > shades[3]);
    RAWFRAME_EXPECT(std::abs(shades.back() - at(*kFixed, 32, 32)[0]) < 4 && shades.back() == shades[shades.size() - 2]);
    RAWFRAME_EXPECT((*made)->statistics().framesMetered >= 14);
    // Starting, or after a cut, the exposure goes at once to what it
    // measures.
    auto snapping = render_scene_gpu::SceneRenderer::create(*kDevice);
    frame.metering.snap = true;
    const auto kAtOnce = drawn(**framer, **snapping, frame, kMeshes);
    frame.metering.snap = false;
    const auto kThen = drawn(**framer, **snapping, frame, kMeshes);
    RAWFRAME_EXPECT(kAtOnce.has_value() && kThen.has_value());
    if (kAtOnce.has_value() && kThen.has_value()) {
        std::printf("at once %d, then %d\n", at(*kAtOnce, 32, 32)[0], at(*kThen, 32, 32)[0]);
        // The first frame is drawn with the camera's exposure; the next
        // with the one measured.
        RAWFRAME_EXPECT(at(*kAtOnce, 32, 32)[0] < 10 && std::abs(at(*kThen, 32, 32)[0] - shades.back()) < 3);
    }
}

RAWFRAME_TEST(ACamerasGradeShapesThePicture) {
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
    // A white sky exposed to middle grey, then a straw-coloured one.
    SceneFrame frame = looking();
    frame.lights.sun = {0, 0, 0};
    frame.lights.ground = {0, 0, 0};
    frame.lights.sky = {50, 50, 50};
    frame.shadows.count = 0;
    frame.exposure = std::log2(50.0F) + 3;
    const auto kSky = [&](const std::optional<render_scene::Grading>& grade) {
        frame.grading = render_scene::gradingOf(grade);
        const auto kPixels = drawn(**framer, **made, frame, kMeshes);
        RAWFRAME_EXPECT(kPixels.has_value());
        return kPixels.has_value() ? at(*kPixels, 32, 32) : std::array<int, 3>{};
    };
    const std::array<int, 3> kPlain = kSky(std::nullopt);
    const std::array<int, 3> kNeutral = kSky(render_scene::Grading{});
    const std::array<int, 3> kBrighter = kSky(render_scene::Grading{.slopeR = 2, .slopeG = 2, .slopeB = 2});
    const std::array<int, 3> kWarmer = kSky(render_scene::Grading{.temperature = 1});
    frame.lights.sky = {60, 50, 20};
    const std::array<int, 3> kStraw = kSky(std::nullopt);
    const std::array<int, 3> kGrey = kSky(render_scene::Grading{.saturation = 0});
    std::printf("plain %d, neutral %d, brighter %d, warmer %d %d, straw %d %d %d, grey %d %d %d\n",
                kPlain[0],
                kNeutral[0],
                kBrighter[0],
                kWarmer[0],
                kWarmer[2],
                kStraw[0],
                kStraw[1],
                kStraw[2],
                kGrey[0],
                kGrey[1],
                kGrey[2]);
    RAWFRAME_EXPECT(std::abs(kNeutral[0] - kPlain[0]) <= 1 && kBrighter[0] > kPlain[0] + 20);
    RAWFRAME_EXPECT(kWarmer[0] > kWarmer[2] + 20 && kStraw[0] > kStraw[2] + 40);
    RAWFRAME_EXPECT(std::abs(kGrey[0] - kGrey[2]) <= 2 && std::abs(kGrey[0] - kGrey[1]) <= 2);
}

RAWFRAME_TEST(EveryTonemapperKeepsMiddleGrey) {
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
    // A sky of 50 candela per square meter exposed to 0.18, middle grey;
    // then one ten times brighter.
    SceneFrame frame = looking();
    frame.lights.sun = {0, 0, 0};
    frame.lights.ground = {0, 0, 0};
    frame.lights.sky = {50, 50, 50};
    frame.shadows.count = 0;
    frame.exposure = std::log2(50.0F / (1.2F * 0.18F));
    const auto kSky = [&](render_scene::Tonemapper tonemapper) {
        frame.tonemapper = tonemapper;
        const auto kPixels = drawn(**framer, **made, frame, kMeshes);
        RAWFRAME_EXPECT(kPixels.has_value());
        return kPixels.has_value() ? at(*kPixels, 32, 32)[0] : -1;
    };
    const int kAgx = kSky(render_scene::Tonemapper::Agx);
    const int kNeutral = kSky(render_scene::Tonemapper::PbrNeutral);
    const int kLinear = kSky(render_scene::Tonemapper::Linear);
    frame.lights.sky = {500, 500, 500};
    const int kBrightAgx = kSky(render_scene::Tonemapper::Agx);
    const int kBrightLinear = kSky(render_scene::Tonemapper::Linear);
    std::printf("middle grey: agx %d, neutral %d, linear %d; ten times: agx %d, linear %d\n",
                kAgx,
                kNeutral,
                kLinear,
                kBrightAgx,
                kBrightLinear);
    // 0.2145 linear is 127 in sRGB.
    RAWFRAME_EXPECT(std::abs(kAgx - 127) <= 2 && std::abs(kNeutral - kAgx) <= 2 && std::abs(kLinear - kAgx) <= 2);
    RAWFRAME_EXPECT(kBrightLinear == 255 && kBrightAgx < 250 && kBrightAgx > kAgx);
}

RAWFRAME_TEST(FxaaSoftensSlantedEdgesAlone) {
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
    // A black box turned a twelfth of a turn about the view's axis, before
    // a middle grey sky: its edges are stairs of whole texels.
    SceneFrame frame = looking();
    frame.lights.sun = {0, 0, 0};
    frame.lights.ground = {0, 0, 0};
    frame.lights.sky = {50, 50, 50};
    frame.shadows.count = 0;
    frame.exposure = std::log2(50.0F / (1.2F * 0.18F));
    SceneDraw turned = box(8, 2, {0, 0, 0, 1});
    const float kCos = std::cos(std::numbers::pi_v<float> / 6);
    const float kSin = std::sin(std::numbers::pi_v<float> / 6);
    turned.model[0] = 2 * kCos;
    turned.model[1] = 2 * kSin;
    turned.model[4] = -2 * kSin;
    turned.model[5] = 2 * kCos;
    turned.normal[0] = kCos / 2;
    turned.normal[1] = kSin / 2;
    turned.normal[4] = -kSin / 2;
    turned.normal[5] = kCos / 2;
    frame.draws = {turned};
    const auto kHard = drawn(**framer, **made, frame, kMeshes);
    frame.fxaa = true;
    const auto kSoft = drawn(**framer, **made, frame, kMeshes);
    RAWFRAME_EXPECT(kHard.has_value() && kSoft.has_value());
    if (!kHard.has_value() || !kSoft.has_value()) {
        return;
    }
    // Texels neither sky nor box: the stairs' blend.
    const int kSky = at(*kHard, 2, 2)[0];
    const int kBox = at(*kHard, 32, 32)[0];
    const auto kBetween = [kSky, kBox](const std::vector<std::byte>& pixels) {
        int count = 0;
        for (std::uint32_t y = 0; y < kSide; ++y) {
            for (std::uint32_t x = 0; x < kSide; ++x) {
                const int kRed = at(pixels, x, y)[0];
                count += kRed > kBox + 12 && kRed < kSky - 12 ? 1 : 0;
            }
        }
        return count;
    };
    std::printf("sky %d, box %d; between, hard %d, with FXAA %d\n", kSky, kBox, kBetween(*kHard), kBetween(*kSoft));
    RAWFRAME_EXPECT(kSky > kBox + 100 && kBetween(*kHard) < 8 && kBetween(*kSoft) > 30);
    // Away from the edges, nothing changes.
    RAWFRAME_EXPECT(at(*kSoft, 2, 2) == at(*kHard, 2, 2) && at(*kSoft, 32, 32) == at(*kHard, 32, 32));
    RAWFRAME_EXPECT((*made)->statistics().framesSmoothed >= 1);
}

RAWFRAME_TEST(PbrNeutralMeetsItsConformanceVectors) {
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
    // A sky exposed so the operator is fed its light unchanged: the
    // exposure's factor undoes the normalization's 1.41371 (ADR-0047,
    // D295).
    SceneFrame frame = looking();
    frame.lights.sun = {0, 0, 0};
    frame.lights.ground = {0, 0, 0};
    frame.shadows.count = 0;
    frame.tonemapper = render_scene::Tonemapper::PbrNeutral;
    frame.exposure = std::log2(1.41371F / 1.2F);
    // Khronos PBR Neutral's vectors, in sRGB: within its guarantee range
    // (every channel from 0.08, none past 0.76 once 0.04 is taken off)
    // each channel is its input less 0.04; below 0.08 the offset
    // shrinks; past 0.76 the peak is compressed and desaturated.
    struct Vector {
        std::array<float, 3> light;
        std::array<int, 3> shown;
    };
    constexpr std::array<Vector, 8> kVectors = {{{{0.5F, 0.3F, 0.2F}, {181, 139, 111}},
                                                 {{0.1F, 0.7F, 0.35F}, {69, 212, 151}},
                                                 {{0.75F, 0.75F, 0.75F}, {219, 219, 219}},
                                                 {{0.02F, 0.02F, 0.02F}, {8, 8, 8}},
                                                 {{0.3F, 0.05F, 0.6F}, {141, 34, 198}},
                                                 {{1, 1, 1}, {240, 240, 240}},
                                                 {{2, 1, 0.5F}, {250, 193, 154}},
                                                 {{10, 10, 10}, {254, 254, 254}}}};
    for (const Vector& kVector : kVectors) {
        frame.lights.sky = kVector.light;
        const auto kPixels = drawn(**framer, **made, frame, kMeshes);
        RAWFRAME_EXPECT(kPixels.has_value());
        if (!kPixels.has_value()) {
            return;
        }
        const std::array<int, 3> kShown = at(*kPixels, 32, 32);
        std::printf("neutral %.2f %.2f %.2f: %d %d %d, expected %d %d %d\n",
                    kVector.light[0],
                    kVector.light[1],
                    kVector.light[2],
                    kShown[0],
                    kShown[1],
                    kShown[2],
                    kVector.shown[0],
                    kVector.shown[1],
                    kVector.shown[2]);
        for (std::size_t channel = 0; channel < 3; ++channel) {
            RAWFRAME_EXPECT(std::abs(kShown[channel] - kVector.shown[channel]) <= 1);
        }
    }
}

RAWFRAME_TEST(TheLitModelReflectsTheSunAndTheSky) {
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
    // A black ball four meters ahead under the default sun and sky: with
    // no diffuse light at all, what shows is reflected (D299). Where its
    // normal halves the way to the sun and to the eye, the sun's highlight;
    // at its rim, seen at a grazing angle, the sky more than head on.
    SceneFrame frame = looking();
    frame.shadows.count = 0;
    frame.draws = {box(4, 1.5F, {0, 0, 0, 1}, render_scene::kSphere)};
    const auto kPixels = drawn(**framer, **made, frame, kMeshes);
    RAWFRAME_EXPECT(kPixels.has_value());
    if (!kPixels.has_value()) {
        return;
    }
    const int kHighlight = at(*kPixels, 35, 22)[0];
    const int kBelow = at(*kPixels, 32, 40)[0];
    // The sky alone, three stops brighter.
    frame.lights.sun = {0, 0, 0};
    frame.lights.ground = {0, 0, 0};
    frame.exposure -= 3;
    const auto kSkyOnly = drawn(**framer, **made, frame, kMeshes);
    RAWFRAME_EXPECT(kSkyOnly.has_value());
    if (!kSkyOnly.has_value()) {
        return;
    }
    const int kHeadOn = at(*kSkyOnly, 32, 30)[0];
    const int kRim = at(*kSkyOnly, 20, 30)[0];
    std::printf("highlight %d, below %d; sky alone head on %d, rim %d\n", kHighlight, kBelow, kHeadOn, kRim);
    RAWFRAME_EXPECT(kHighlight > kBelow + 60 && kHeadOn > 0 && kRim > 2 * kHeadOn);
}

RAWFRAME_TEST(AMaterialShapesItsModelsSurface) {
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
    // A box four meters ahead in the dark, exposed so fifty nits are
    // middle grey, with its material (D303) second in the frame's.
    SceneFrame frame = looking();
    frame.shadows.count = 0;
    frame.lights.sun = {0, 0, 0};
    frame.lights.ground = {0, 0, 0};
    frame.lights.sky = {0, 0, 0};
    frame.exposure = std::log2(50.0F / (1.2F * 0.18F));
    SceneDraw shown = box(4, 1.5F, {1, 1, 1, 1});
    shown.material = 1;
    frame.draws = {shown};
    const auto kCenter = [&](const render_scene::MaterialBlob& blob) {
        frame.materials = {render_scene::noMaterial(), blob};
        const auto kPixels = drawn(**framer, **made, frame, kMeshes);
        RAWFRAME_EXPECT(kPixels.has_value());
        return kPixels.has_value() ? at(*kPixels, 32, 32) : std::array<int, 3>{};
    };
    const std::array<int, 3> kDark = kCenter(render_scene::noMaterial());
    // Unlit, its color stands whatever the light; emitting fifty nits, it
    // shows middle grey in the dark.
    render_scene::MaterialBlob unlit = render_scene::noMaterial();
    unlit[0] = 0.05F;
    unlit[1] = 0.5F;
    unlit[2] = 0.05F;
    unlit[15] = 1;
    const std::array<int, 3> kUnlit = kCenter(unlit);
    render_scene::MaterialBlob glowing = render_scene::noMaterial();
    glowing[8] = 50;
    glowing[9] = 50;
    glowing[10] = 50;
    const std::array<int, 3> kGlowing = kCenter(glowing);
    // A black ball under the default sun: a rougher one spreads the sun's
    // highlight thinner.
    SceneFrame lit = looking();
    lit.shadows.count = 0;
    SceneDraw ball = box(4, 1.5F, {0, 0, 0, 1}, render_scene::kSphere);
    ball.material = 1;
    lit.draws = {ball};
    const auto kHighlight = [&](float roughness) {
        render_scene::MaterialBlob rough = render_scene::noMaterial();
        rough[7] = roughness;
        lit.materials = {render_scene::noMaterial(), rough};
        const auto kPixels = drawn(**framer, **made, lit, kMeshes);
        RAWFRAME_EXPECT(kPixels.has_value());
        return kPixels.has_value() ? at(*kPixels, 35, 22)[0] : -1;
    };
    const int kSmooth = kHighlight(0.3F);
    const int kRough = kHighlight(0.9F);
    std::printf("dark %d, unlit %d %d %d, glowing %d %d %d, highlight smooth %d rough %d\n",
                kDark[0],
                kUnlit[0],
                kUnlit[1],
                kUnlit[2],
                kGlowing[0],
                kGlowing[1],
                kGlowing[2],
                kSmooth,
                kRough);
    RAWFRAME_EXPECT(kDark[0] == 0 && kUnlit[1] > kUnlit[0] + 60 && std::abs(kGlowing[0] - 128) <= 3 &&
                    kSmooth > kRough + 30);
}

RAWFRAME_TEST(TheGroundLightsWhatFacesDown) {
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
    // A white ball under the default sun and sky (D304): its underside is
    // lit by what the ground gives back, and its top is not.
    SceneFrame frame = looking();
    frame.shadows.count = 0;
    frame.draws = {box(4, 1.5F, {1, 1, 1, 1}, render_scene::kSphere)};
    const auto kWith = drawn(**framer, **made, frame, kMeshes);
    frame.lights.ground = {0, 0, 0};
    const auto kWithout = drawn(**framer, **made, frame, kMeshes);
    RAWFRAME_EXPECT(kWith.has_value() && kWithout.has_value());
    if (!kWith.has_value() || !kWithout.has_value()) {
        return;
    }
    std::printf("underside %d without the ground %d; top %d and %d\n",
                at(*kWith, 32, 43)[0],
                at(*kWithout, 32, 43)[0],
                at(*kWith, 32, 21)[0],
                at(*kWithout, 32, 21)[0]);
    RAWFRAME_EXPECT(at(*kWith, 32, 43)[0] > at(*kWithout, 32, 43)[0] + 20 &&
                    std::abs(at(*kWith, 32, 21)[0] - at(*kWithout, 32, 21)[0]) <= 3);
}

RAWFRAME_TEST(TranslucentModelsBlendOverWhatIsBehindThem) {
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
    // A red box eight meters ahead, and before it a blue pane of glass
    // half opaque (D305), wide enough to cover the box and the sky beside.
    SceneFrame frame = looking();
    frame.shadows.count = 0;
    SceneDraw pane = box(4, 1, {0, 0, 1, 1});
    pane.model[0] = 3;
    pane.model[10] = 0.05F;
    pane.material = 1;
    render_scene::MaterialBlob glass = render_scene::noMaterial();
    glass[12] = 0.5F;
    const auto kDrawn = [&](bool paned) {
        frame.draws = {box(8, 1, {1, 0, 0, 1})};
        if (paned) {
            frame.draws.push_back(pane);
        }
        frame.translucent = paned ? 1 : 0;
        frame.materials = {render_scene::noMaterial(), glass};
        const auto kPixels = drawn(**framer, **made, frame, kMeshes);
        RAWFRAME_EXPECT(kPixels.has_value());
        return kPixels.has_value() ? *kPixels : std::vector<std::byte>{};
    };
    const std::vector<std::byte> kBare = kDrawn(false);
    const std::vector<std::byte> kPaned = kDrawn(true);
    if (kBare.empty() || kPaned.empty()) {
        return;
    }
    const std::array<int, 3> kBox = at(kBare, 32, 32);
    const std::array<int, 3> kBoxBehind = at(kPaned, 32, 32);
    const std::array<int, 3> kSky = at(kBare, 52, 32);
    const std::array<int, 3> kSkyBehind = at(kPaned, 52, 32);
    std::printf("box %d %d %d, behind glass %d %d %d; sky %d %d %d, behind glass %d %d %d\n",
                kBox[0],
                kBox[1],
                kBox[2],
                kBoxBehind[0],
                kBoxBehind[1],
                kBoxBehind[2],
                kSky[0],
                kSky[1],
                kSky[2],
                kSkyBehind[0],
                kSkyBehind[1],
                kSkyBehind[2]);
    // The red shows through, less of it, and the glass's blue joins it.
    RAWFRAME_EXPECT(kBoxBehind[0] > 40 && kBoxBehind[0] < kBox[0] - 20 && kBoxBehind[2] > kBox[2] + 20);
    RAWFRAME_EXPECT(kSkyBehind != kSky && kSkyBehind[2] > 20);
}

RAWFRAME_TEST(AMaterialsTextureColorsItsModel) {
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
    // Two by two texels, red and green above, blue and white below.
    texture::Texture quarters{.format = texture::Format::Rgba8Srgb};
    quarters.levels.push_back({.width = 2,
                               .height = 2,
                               .bytes = {std::byte{255},
                                         std::byte{0},
                                         std::byte{0},
                                         std::byte{255},
                                         std::byte{0},
                                         std::byte{255},
                                         std::byte{0},
                                         std::byte{255},
                                         std::byte{0},
                                         std::byte{0},
                                         std::byte{255},
                                         std::byte{255},
                                         std::byte{255},
                                         std::byte{255},
                                         std::byte{255},
                                         std::byte{255}}});
    const auto kQuarters = std::make_shared<const texture::Texture>(std::move(quarters));
    std::uint64_t asked = 0;
    const render_scene_gpu::TextureSource kTextures = [&](std::uint64_t id) {
        ++asked;
        return id == 0x77 ? kQuarters : nullptr;
    };
    // An unlit box whose material's color is the texture's, sampled
    // nearest: its face toward the eye shows each texel in its quarter,
    // the first row at the top.
    SceneFrame frame = looking();
    frame.shadows.count = 0;
    // Pixels compared exactly: undithered (D332).
    frame.dither = false;
    SceneDraw shown = box(4, 1.5F, {1, 1, 1, 1});
    shown.material = 1;
    frame.draws = {shown};
    render_scene::MaterialBlob unlit = render_scene::noMaterial();
    unlit[15] = 1 + 2;
    frame.materials = {render_scene::noMaterial(), unlit};
    const auto kQuartersOf = [&](std::uint64_t texture) {
        frame.textures = {
            {}, {.base = {.id = texture, .filter = material::Filter::Nearest, .address = material::Address::Clamp}}};
        (**made).prepare(&frame, kMeshes, kTextures);
        const std::array<render::FrameRecorder*, 1> kRecorders = {&**made};
        std::optional<std::vector<std::byte>> pixels;
        const std::uint64_t kBefore = (**made).statistics().frames;
        for (int attempt = 0; attempt < 1000 && (**made).statistics().frames == kBefore; ++attempt) {
            RAWFRAME_EXPECT((**framer).finish(5'000'000'000).has_value());
            RAWFRAME_EXPECT(
                (**framer).make(kRecorders, {.width = kSide, .height = kSide, .readBack = true}).has_value());
        }
        RAWFRAME_EXPECT((**framer).finish(5'000'000'000).has_value());
        pixels = (**framer).pixels();
        RAWFRAME_EXPECT(pixels.has_value());
        return pixels.has_value()
                   ? std::array{at(*pixels, 24, 24), at(*pixels, 40, 24), at(*pixels, 24, 40), at(*pixels, 40, 40)}
                   : std::array<std::array<int, 3>, 4>{};
    };
    const auto [kRed, kGreen, kBlue, kWhite] = kQuartersOf(0x77);
    // Moved half a texture left (D311), clamped: the green column fills
    // the face's left half as well as its right.
    unlit[18] = 0.5F;
    frame.materials = {render_scene::noMaterial(), unlit};
    const std::array<int, 3> kMoved = kQuartersOf(0x77)[0];
    unlit[18] = 0;
    frame.materials = {render_scene::noMaterial(), unlit};
    // A texture that is not there is sampled as white.
    const auto kNone = kQuartersOf(0x55);
    std::printf("red %d %d %d, green %d %d %d, blue %d %d %d, white %d %d %d, none %d %d %d, uploaded %llu\n",
                kRed[0],
                kRed[1],
                kRed[2],
                kGreen[0],
                kGreen[1],
                kGreen[2],
                kBlue[0],
                kBlue[1],
                kBlue[2],
                kWhite[0],
                kWhite[1],
                kWhite[2],
                kNone[0][0],
                kNone[0][1],
                kNone[0][2],
                static_cast<unsigned long long>((**made).statistics().texturesUploaded));
    RAWFRAME_EXPECT(kRed[0] > kRed[1] + 60 && kRed[0] > kRed[2] + 60);
    RAWFRAME_EXPECT(kGreen[1] > kGreen[0] + 60 && kGreen[1] > kGreen[2] + 60);
    RAWFRAME_EXPECT(kBlue[2] > kBlue[0] + 60 && kBlue[2] > kBlue[1] + 60);
    RAWFRAME_EXPECT(kWhite[0] > 150 && std::abs(kWhite[0] - kWhite[2]) < 10);
    RAWFRAME_EXPECT(kMoved == kGreen);
    for (const std::array<int, 3>& kQuarter : kNone) {
        RAWFRAME_EXPECT(kQuarter == kWhite);
    }
    // The texture, white, and the dark cube bound for no sky's picture
    // (D322), each uploaded once.
    RAWFRAME_EXPECT(asked > 0 && (**made).statistics().texturesUploaded == 3);
}

RAWFRAME_TEST(AMaskedMaterialIsCutWhereItsTextureIsClear) {
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
    // White texels, the top row opaque, the bottom row clear.
    texture::Texture stencil{.format = texture::Format::Rgba8Srgb};
    stencil.levels.push_back({.width = 2, .height = 2, .bytes = std::vector<std::byte>(16, std::byte{255})});
    for (const std::size_t kClear : {11U, 15U}) {
        stencil.levels[0].bytes[kClear] = std::byte{0};
    }
    const auto kStencil = std::make_shared<const texture::Texture>(std::move(stencil));
    const render_scene_gpu::TextureSource kTextures = [&](std::uint64_t id) {
        return id == 0x77 ? kStencil : nullptr;
    };
    // An unlit red box masked by the texture's alpha at half, before an
    // unlit green one: the red above, the green seen through below.
    SceneFrame frame = looking();
    frame.shadows.count = 0;
    SceneDraw cut = box(4, 1.5F, {1, 0, 0, 1});
    cut.material = 1;
    SceneDraw behind = box(10, 5, {0, 1, 0, 1});
    behind.material = 2;
    frame.draws = {cut, behind};
    render_scene::MaterialBlob masked = render_scene::noMaterial();
    masked[14] = 0.5F;
    masked[15] = 1 + 4;
    render_scene::MaterialBlob unlit = render_scene::noMaterial();
    unlit[15] = 1;
    frame.materials = {render_scene::noMaterial(), masked, unlit};
    frame.textures = {
        {}, {.base = {.id = 0x77, .filter = material::Filter::Nearest, .address = material::Address::Clamp}}, {}};
    (**made).prepare(&frame, kMeshes, kTextures);
    const std::array<render::FrameRecorder*, 1> kRecorders = {&**made};
    const std::uint64_t kBefore = (**made).statistics().frames;
    for (int attempt = 0; attempt < 1000 && (**made).statistics().frames == kBefore; ++attempt) {
        RAWFRAME_EXPECT((**framer).finish(5'000'000'000).has_value());
        RAWFRAME_EXPECT((**framer).make(kRecorders, {.width = kSide, .height = kSide, .readBack = true}).has_value());
    }
    RAWFRAME_EXPECT((**framer).finish(5'000'000'000).has_value());
    const auto kPixels = (**framer).pixels();
    RAWFRAME_EXPECT(kPixels.has_value());
    if (!kPixels.has_value()) {
        return;
    }
    const std::array<int, 3> kAbove = at(*kPixels, 32, 24);
    const std::array<int, 3> kBelow = at(*kPixels, 32, 40);
    std::printf("above %d %d %d, below %d %d %d\n", kAbove[0], kAbove[1], kAbove[2], kBelow[0], kBelow[1], kBelow[2]);
    RAWFRAME_EXPECT(kAbove[0] > kAbove[1] + 60 && kBelow[1] > kBelow[0] + 60);
}

RAWFRAME_TEST(AMaskedCasterCastsOnlyWhatIsLeftOfIt) {
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
    // The spot's room with its box masked by a texture opaque or clear:
    // opaque, the floor left of the box is in its shadow; clear, the box is
    // cut away from the light, and the floor is lit.
    const auto kFloor = [&](std::byte alpha) {
        texture::Texture one{.format = texture::Format::Rgba8Srgb};
        one.levels.push_back(
            {.width = 1, .height = 1, .bytes = {std::byte{255}, std::byte{255}, std::byte{255}, alpha}});
        const auto kOne = std::make_shared<const texture::Texture>(std::move(one));
        const render_scene_gpu::TextureSource kTextures = [&](std::uint64_t id) {
            return id == 0x77 ? kOne : nullptr;
        };
        SceneFrame frame = lamplit(true);
        frame.draws[1].material = 1;
        frame.lightShadows.casters[1].material = 1;
        render_scene::MaterialBlob masked = render_scene::noMaterial();
        masked[14] = 0.5F;
        masked[15] = 4;
        frame.materials = {render_scene::noMaterial(), masked};
        frame.textures = {{}, {.base = {.id = 0x77}}};
        const auto kPixels = drawn(**framer, **made, frame, kMeshes, kTextures);
        RAWFRAME_EXPECT(kPixels.has_value());
        return kPixels.has_value() ? at(*kPixels, 27, 39)[0] : -1;
    };
    const int kShadowed = kFloor(std::byte{255});
    const int kLit = kFloor(std::byte{0});
    std::printf("left of the box: opaque %d, clear %d\n", kShadowed, kLit);
    RAWFRAME_EXPECT(kLit > 60 && kShadowed < kLit / 3);
}

RAWFRAME_TEST(PackedAndEmissionTexturesShapeTheSurface) {
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
    // One texture, white above and black below, linear in its texels.
    texture::Texture halves{.format = texture::Format::Rgba8};
    halves.levels.push_back({.width = 1, .height = 2, .bytes = std::vector<std::byte>(8, std::byte{255})});
    for (const std::size_t kBelow : {4U, 5U, 6U}) {
        halves.levels[0].bytes[kBelow] = std::byte{0};
    }
    const auto kHalves = std::make_shared<const texture::Texture>(std::move(halves));
    const render_scene_gpu::TextureSource kTextures = [&](std::uint64_t id) {
        return id == 0x77 ? kHalves : nullptr;
    };
    const render_scene::SceneTexture kSampled{
        .id = 0x77, .filter = material::Filter::Nearest, .address = material::Address::Clamp};
    SceneDraw shown = box(4, 1.5F, {1, 1, 1, 1});
    shown.material = 1;
    const auto kHalvesOf = [&](SceneFrame& frame, const material::Material& surface) {
        frame.shadows.count = 0;
        frame.draws = {shown};
        frame.materials = {render_scene::noMaterial(), material::blobOf(surface)};
        const auto kPixels = drawn(**framer, **made, frame, kMeshes, kTextures);
        RAWFRAME_EXPECT(kPixels.has_value());
        return kPixels.has_value() ? std::pair{at(*kPixels, 32, 24)[0], at(*kPixels, 32, 40)[0]} : std::pair{-1, -1};
    };
    // Glowing fifty nits in the dark where the emission texture is white,
    // exposed so that is middle grey.
    SceneFrame dark = looking();
    dark.lights.sun = {0, 0, 0};
    dark.lights.sky = {0, 0, 0};
    dark.lights.ground = {0, 0, 0};
    dark.exposure = std::log2(50.0F / (1.2F * 0.18F));
    material::Material glowing;
    glowing.surface.emissionLuminance = 50;
    glowing.textures.emission.id = 0x77;
    dark.textures = {{}, {.emission = kSampled}};
    const auto [kGlowAbove, kGlowBelow] = kHalvesOf(dark, glowing);
    // Under the sky alone, occluded where the packed texture's red is
    // nought.
    SceneFrame sky = looking();
    sky.lights.sun = {0, 0, 0};
    material::Material occluded;
    occluded.textures.packed.id = 0x77;
    occluded.textures.occlusion = material::Channel::Red;
    sky.textures = {{}, {.packed = kSampled}};
    const auto [kSkyAbove, kSkyBelow] = kHalvesOf(sky, occluded);
    std::printf(
        "glowing %d above, %d below; under the sky %d above, %d below\n", kGlowAbove, kGlowBelow, kSkyAbove, kSkyBelow);
    RAWFRAME_EXPECT(std::abs(kGlowAbove - 128) <= 3 && kGlowBelow == 0 && kSkyAbove > 60 && kSkyBelow < 5);
}

RAWFRAME_TEST(ANormalTextureBendsTheLight) {
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
    // A tangent-space normal leaning 45 degrees one way, as glTF encodes
    // it: toward rising u (red), or up the image (green).
    const auto kLeaning = [](std::byte red, std::byte green) {
        texture::Texture leaning{.format = texture::Format::Rgba8};
        leaning.levels.push_back({.width = 1, .height = 1, .bytes = {red, green, std::byte{219}, std::byte{255}}});
        return std::make_shared<const texture::Texture>(std::move(leaning));
    };
    const auto kRight = kLeaning(std::byte{218}, std::byte{128});
    const auto kUp = kLeaning(std::byte{128}, std::byte{218});
    std::shared_ptr<const texture::Texture> given;
    const render_scene_gpu::TextureSource kTextures = [&](std::uint64_t id) {
        return id == 0x77 ? given : nullptr;
    };
    // A rough white box facing the eye, lit by the sun alone from a side
    // at 45 degrees; the middle of its face read.
    material::Material rough;
    rough.surface.baseColor = {1, 1, 1};
    rough.surface.specularRoughness = 1;
    const auto kLit = [&](std::array<float, 3> toSun, bool bent) {
        SceneFrame frame = looking();
        frame.shadows.count = 0;
        frame.lights.sky = {0, 0, 0};
        frame.lights.ground = {0, 0, 0};
        frame.lights.toSun = toSun;
        SceneDraw shown = box(4, 1.5F, {1, 1, 1, 1});
        shown.material = 1;
        frame.draws = {shown};
        material::Material surface = rough;
        if (bent) {
            surface.textures.normal.id = 0x77;
        }
        frame.materials = {render_scene::noMaterial(), material::blobOf(surface)};
        frame.textures = {{}, {.normal = {.id = bent ? 0x77ULL : 0ULL}}};
        const auto kPixels = drawn(**framer, **made, frame, kMeshes, kTextures);
        RAWFRAME_EXPECT(kPixels.has_value());
        return kPixels.has_value() ? at(*kPixels, 32, 32)[0] : -1;
    };
    const float kSlant = std::sqrt(0.5F);
    const int kFlatRight = kLit({kSlant, 0, kSlant}, false);
    const int kFlatLeft = kLit({-kSlant, 0, kSlant}, false);
    given = kRight;
    const int kBentTowardRight = kLit({kSlant, 0, kSlant}, true);
    const int kBentAwayLeft = kLit({-kSlant, 0, kSlant}, true);
    given = kUp;
    const int kBentTowardAbove = kLit({0, kSlant, kSlant}, true);
    const int kBentAwayBelow = kLit({0, -kSlant, kSlant}, true);
    std::printf("flat %d right, %d left; leaning right: sun right %d, left %d; leaning up: sun above %d, below %d\n",
                kFlatRight,
                kFlatLeft,
                kBentTowardRight,
                kBentAwayLeft,
                kBentTowardAbove,
                kBentAwayBelow);
    RAWFRAME_EXPECT(std::abs(kFlatRight - kFlatLeft) <= 2 && kBentTowardRight > kFlatRight + 10 &&
                    kBentAwayLeft + 60 < kFlatLeft && kBentTowardAbove > kFlatRight + 10 &&
                    kBentAwayBelow + 60 < kFlatLeft);
}
