// The sky's picture on lavapipe (D322): seen behind everything the way
// each point of the target looks, a face of the cube a way; a texture that
// is not an environment is none; a mirror reflects what is behind the eye;
// a rough white ball under a uniform picture is as bright as the sky, and
// under a picture bright above, its top outshines its underside; a point a
// reflection probe of its cluster holds reflects the probe's picture,
// projected onto its box, the first of those holding it that a point takes,
// and the sky's where no probe holds it or the probe's picture is not held
// (D325, D340).

#include "fixture.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/test/test.h"
#include "rawframe/texture/texture.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <numbers>

using namespace rawframe;
using namespace rawframe::scene_fixture;
using render_scene::SceneFrame;

namespace {

constexpr std::uint64_t kPicture = 0x51c7e0a2b9d34f68ULL;

/// A cube of one level, each face one light: +X, -X, +Y, -Y, +Z, -Z.
std::shared_ptr<const texture::Texture> cubeOf(const std::array<std::array<float, 3>, 6>& faces) {
    texture::Texture made{.format = texture::Format::Rgba16Float, .faces = 6};
    texture::Level level{.width = 4, .height = 4};
    for (const std::array<float, 3>& kFace : faces) {
        for (std::uint32_t texel = 0; texel < 16; ++texel) {
            for (const float kChannel : {kFace[0], kFace[1], kFace[2], 1.0F}) {
                const std::uint16_t kHalf = texture::halfOf(kChannel);
                level.bytes.push_back(static_cast<std::byte>(kHalf & 0xFFU));
                level.bytes.push_back(static_cast<std::byte>(kHalf >> 8U));
            }
        }
    }
    made.levels.push_back(std::move(level));
    return std::make_shared<const texture::Texture>(std::move(made));
}

/// A view along `yaw` and `pitch` with the sky's picture, bright enough to
/// read, and neither sun nor ground.
SceneFrame facing(float yaw, float pitch) {
    const auto kSchema = *schema::RegistryBuilder{}.freeze();
    auto scene = *render_scene::Scene::create(*kSchema, {});
    SceneFrame made =
        scene->queue({.yaw = yaw, .pitch = pitch, .fovY = 1.5707964F, .near = 0.1F, .exposure = 15, .aspect = 1});
    made.temporal.enabled = false;
    made.shadows.count = 0;
    made.lights.sun = {0, 0, 0};
    made.lights.ground = {0, 0, 0};
    made.lights.sky = {20000, 20000, 20000};
    made.lights.environment = kPicture;
    return made;
}

/// Whether a pixel is mostly `channel`: it outshines the others by `by`.
bool mostly(const std::array<int, 3>& pixel, std::size_t channel, int by = 40) {
    for (std::size_t other = 0; other < 3; ++other) {
        if (other != channel && pixel[channel] < pixel[other] + by) {
            return false;
        }
    }
    return true;
}

void print(const char* what, const std::array<int, 3>& pixel) {
    std::printf("%s %d %d %d\n", what, pixel[0], pixel[1], pixel[2]);
}

} // namespace

RAWFRAME_TEST(TheSkysPictureIsSeenTheWayEachPointLooks) {
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
    // Green along +X, blue along -X, white above, black below, yellow
    // behind (+Z), red ahead (-Z).
    const std::shared_ptr<const texture::Texture> kCube =
        cubeOf({{{0, 1, 0}, {0, 0, 1}, {1, 1, 1}, {0, 0, 0}, {1, 1, 0}, {1, 0, 0}}});
    const render_scene_gpu::TextureSource kTextures = [&kCube](std::uint64_t id) {
        return id == kPicture ? kCube : nullptr;
    };
    const auto kLook = [&](float yaw, float pitch) {
        const auto kPixels = drawn(**framer, **made, facing(yaw, pitch), {}, kTextures);
        RAWFRAME_EXPECT(kPixels.has_value());
        return kPixels.has_value() ? at(*kPixels, kSide / 2, kSide / 2) : std::array<int, 3>{};
    };
    const std::array<int, 3> kAhead = kLook(0, 0);
    const std::array<int, 3> kRight = kLook(-std::numbers::pi_v<float> / 2, 0);
    const std::array<int, 3> kLeft = kLook(std::numbers::pi_v<float> / 2, 0);
    const std::array<int, 3> kUp = kLook(0, 1.5F);
    print("ahead", kAhead);
    print("right", kRight);
    print("left", kLeft);
    print("up", kUp);
    RAWFRAME_EXPECT(mostly(kAhead, 0) && mostly(kRight, 1) && mostly(kLeft, 2));
    RAWFRAME_EXPECT(kUp[0] > 150 && std::abs(kUp[0] - kUp[2]) < 10);
    // A texture that is not an environment is none: the plain sky, grey.
    const std::shared_ptr<const texture::Texture> kFlat = std::make_shared<const texture::Texture>(texture::Texture{
        .format = texture::Format::Rgba8Srgb,
        .levels = {{.width = 1, .height = 1, .bytes = {std::byte{255}, std::byte{0}, std::byte{0}, std::byte{255}}}}});
    const render_scene_gpu::TextureSource kNotACube = [&kFlat](std::uint64_t) {
        return kFlat;
    };
    const auto kPlain = drawn(**framer, **made, facing(0, 0), {}, kNotACube);
    RAWFRAME_EXPECT(kPlain.has_value());
    if (kPlain.has_value()) {
        const std::array<int, 3> kGrey = at(*kPlain, kSide / 2, kSide / 2);
        print("not a cube", kGrey);
        RAWFRAME_EXPECT(kGrey[0] > 150 && kGrey[0] == kGrey[1] && kGrey[1] == kGrey[2]);
    }
}

RAWFRAME_TEST(SurfacesAreLitByThePictureAndReflectIt) {
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
    std::shared_ptr<const texture::Texture> cube =
        cubeOf({{{0, 1, 0}, {0, 0, 1}, {1, 1, 1}, {0, 0, 0}, {1, 1, 0}, {1, 0, 0}}});
    const render_scene_gpu::TextureSource kTextures = [&cube](std::uint64_t id) {
        return id == kPicture ? cube : nullptr;
    };
    // A mirror ball four meters ahead: head on, it shows what is behind the
    // eye, yellow, amid the red ahead (well within its face: the cube is
    // filtered across its edges).
    SceneFrame frame = facing(0, 0);
    render_scene::SceneDraw ball = box(4, 1.5F, {1, 1, 1, 1}, render_scene::kSphere);
    ball.material = 1;
    render_scene::MaterialBlob mirror = render_scene::noMaterial();
    mirror[3] = 1;
    mirror[7] = 0;
    frame.materials = {render_scene::noMaterial(), mirror};
    frame.draws = {ball};
    const auto kMirrored = drawn(**framer, **made, frame, kMeshes, kTextures);
    RAWFRAME_EXPECT(kMirrored.has_value());
    if (kMirrored.has_value()) {
        print("mirror", at(*kMirrored, kSide / 2, kSide / 2));
        print("beside", at(*kMirrored, 8, kSide / 2));
        const std::array<int, 3> kCenter = at(*kMirrored, kSide / 2, kSide / 2);
        RAWFRAME_EXPECT(kCenter[0] > 100 && kCenter[1] > 100 && kCenter[2] + 60 < kCenter[1] &&
                        mostly(at(*kMirrored, 8, kSide / 2), 0));
    }
    // A rough white ball under a uniform picture: as bright as the sky.
    cube = cubeOf({{{1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}}});
    frame.materials = {render_scene::noMaterial()};
    frame.draws = {box(4, 1.5F, {1, 1, 1, 1}, render_scene::kSphere)};
    frame.lights.sky = {4000, 4000, 4000};
    const auto kUniform = drawn(**framer, **made, frame, kMeshes, kTextures);
    RAWFRAME_EXPECT(kUniform.has_value());
    if (kUniform.has_value()) {
        print("white ball", at(*kUniform, kSide / 2, kSide / 2));
        print("sky", at(*kUniform, 2, 2));
        RAWFRAME_EXPECT(std::abs(at(*kUniform, kSide / 2, kSide / 2)[0] - at(*kUniform, 2, 2)[0]) < 12);
    }
    // Under a picture bright above and dim elsewhere, its top outshines
    // its underside.
    cube = cubeOf({{{0.1F, 0.1F, 0.1F},
                    {0.1F, 0.1F, 0.1F},
                    {4, 4, 4},
                    {0.1F, 0.1F, 0.1F},
                    {0.1F, 0.1F, 0.1F},
                    {0.1F, 0.1F, 0.1F}}});
    const auto kAbove = drawn(**framer, **made, frame, kMeshes, kTextures);
    RAWFRAME_EXPECT(kAbove.has_value());
    if (kAbove.has_value()) {
        print("top", at(*kAbove, kSide / 2, 20));
        print("underside", at(*kAbove, kSide / 2, 44));
        RAWFRAME_EXPECT(at(*kAbove, kSide / 2, 20)[0] > at(*kAbove, kSide / 2, 44)[0] + 60);
    }
}

RAWFRAME_TEST(APointReflectsTheProbesThatHoldIt) {
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
    // The sky's picture and the room's: green along +X, blue along -X,
    // white above, black below, yellow behind (+Z), red ahead (-Z); and a
    // green room.
    constexpr std::uint64_t kRoom = 0x3a9e1c75d20b48f6ULL;
    constexpr std::uint64_t kGreen = 0x7c2d5e18a94b03f1ULL;
    constexpr std::uint64_t kMissing = 0x0e6b93d4c1a7582fULL;
    const std::shared_ptr<const texture::Texture> kCube =
        cubeOf({{{0, 1, 0}, {0, 0, 1}, {1, 1, 1}, {0, 0, 0}, {1, 1, 0}, {1, 0, 0}}});
    const std::shared_ptr<const texture::Texture> kGreenCube =
        cubeOf({{{0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}}});
    const render_scene_gpu::TextureSource kTextures = [&](std::uint64_t id) -> std::shared_ptr<const texture::Texture> {
        if (id == kPicture || id == kRoom) {
            return kCube;
        }
        return id == kGreen ? kGreenCube : nullptr;
    };
    // A frame that draws a probe waits for the probes' pipeline (D337).
    const auto kCenter = [&](const SceneFrame& frame, bool probed = true) {
        const auto kPixels =
            probed
                ? drawnWith(
                      **framer, **made, frame, kMeshes, &render_scene_gpu::RendererStatistics::probesDrawn, kTextures)
                : drawn(**framer, **made, frame, kMeshes, kTextures);
        RAWFRAME_EXPECT(kPixels.has_value());
        return kPixels.has_value() ? at(*kPixels, kSide / 2, kSide / 2) : std::array<int, 3>{};
    };
    render_scene::MaterialBlob mirror = render_scene::noMaterial();
    mirror[3] = 1;
    mirror[7] = 0;
    // A mirror ball four meters ahead in a green room around it: green,
    // where the sky's picture would show yellow.
    SceneFrame frame = facing(0, 0);
    render_scene::SceneDraw ball = box(4, 0.5F, {1, 1, 1, 1}, render_scene::kSphere);
    ball.material = 1;
    frame.materials = {render_scene::noMaterial(), mirror};
    frame.draws = {ball};
    // One cluster, naming the frame's probes in their order.
    const auto kNamed = [&frame] {
        frame.clusters = {.tilesX = 1, .tilesY = 1, .slices = 1};
        frame.clusters.ranges = {0, 0, 0, static_cast<std::uint32_t>(frame.probes.size())};
        frame.clusters.indices.clear();
        for (std::uint32_t at = 0; at < frame.probes.size(); ++at) {
            frame.clusters.indices.push_back(at);
        }
    };
    const render_scene::SceneProbe kGreenRoomProbe{
        .position = {0, 0, -4}, .half = {3, 3, 3}, .environment = kGreen, .intensity = 20000};
    frame.probes = {kGreenRoomProbe};
    kNamed();
    const std::array<int, 3> kGreenRoom = kCenter(frame);
    print("green room", kGreenRoom);
    RAWFRAME_EXPECT(mostly(kGreenRoom, 1));
    // In a long low room whose middle is five meters to its right, what it
    // reflects behind the eye meets the room's back wall far to the left of
    // the room's middle, where its picture is blue: projected onto the box.
    const render_scene::SceneProbe kLowRoom{
        .position = {5, 0, -4}, .half = {6, 3, 1.5F}, .environment = kRoom, .intensity = 20000};
    frame.probes = {kLowRoom};
    kNamed();
    const std::array<int, 3> kProjected = kCenter(frame);
    print("projected", kProjected);
    RAWFRAME_EXPECT(mostly(kProjected, 2));
    // Both holding it, the first a point takes: the green room, then the
    // low room.
    frame.probes = {kGreenRoomProbe, kLowRoom};
    kNamed();
    const std::array<int, 3> kGreenFirst = kCenter(frame);
    frame.probes = {kLowRoom, kGreenRoomProbe};
    kNamed();
    const std::array<int, 3> kLowFirst = kCenter(frame);
    print("green first", kGreenFirst);
    print("low first", kLowFirst);
    RAWFRAME_EXPECT(mostly(kGreenFirst, 1) && mostly(kLowFirst, 2));
    // A probe whose box is well away, or whose picture is not held, is
    // passed over for the sky's: yellow behind the eye.
    const auto kYellow = [](const std::array<int, 3>& pixel) {
        return pixel[0] > 100 && pixel[1] > 100 && pixel[2] + 60 < pixel[1];
    };
    render_scene::SceneProbe away = kGreenRoomProbe;
    away.position = {20, 0, -4};
    frame.probes = {away};
    kNamed();
    const std::array<int, 3> kAway = kCenter(frame);
    render_scene::SceneProbe missing = kLowRoom;
    missing.environment = kMissing;
    frame.probes = {missing};
    kNamed();
    const std::array<int, 3> kSky = kCenter(frame, false);
    print("away", kAway);
    print("sky in its place", kSky);
    RAWFRAME_EXPECT(kYellow(kAway) && kYellow(kSky));
}
