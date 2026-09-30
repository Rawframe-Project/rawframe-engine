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
/// the queue stage would make it, with the engine's default light; not
/// antialiased over time unless asked, so a frame is drawn alone.
SceneFrame looking(bool temporal = false) {
    const auto kSchema = *schema::RegistryBuilder{}.freeze();
    auto scene = *render_scene::Scene::create(*kSchema, {});
    SceneFrame made = scene->queue({.fovY = 1.5707964F, .near = 0.1F, .exposure = 15, .aspect = 1});
    made.temporal.enabled = temporal;
    return made;
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

/// A dark room's frame (D292): a white floor two meters below the eye, a
/// half-meter box standing on it eight ahead, and a light to its right
/// casting its shadow left: a spot three meters up shining down, or a lamp
/// at the eye's height; one cluster holding it, and its squares of a small
/// atlas taking both models, as the queue stage would place them; exposed
/// for a dim room.
SceneFrame lamplit(bool spot) {
    SceneFrame frame = looking();
    frame.lights.sun = {0, 0, 0};
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
