// SPEC-0026's feature groups drawn on lavapipe, each from a graph's
// material as rawframe.material writes it, built by tools/gen_shaders.py
// as the cook builds a game's (D484): a coat over a rough base (D716) and
// the probes it reflects (D725), an anisotropy's stretched highlight
// (D717), glass showing what lies behind it (D720), blurring it when rough
// (D723), and letting the sun through (D724), and a see-through caster's
// partial shadow (D726). Skips where no adapter answers, unless
// RAWFRAME_REQUIRE_GPU is set.

#include "fixture.h"
#include "generated/brushed_graph_scene_container.h"
#include "generated/brushed_graph_shadow_container.h"
#include "generated/coated_graph_scene_container.h"
#include "generated/coated_graph_shadow_container.h"
#include "generated/frosted_graph_scene_container.h"
#include "generated/frosted_graph_shadow_container.h"
#include "generated/glass_graph_scene_container.h"
#include "generated/glass_graph_shadow_container.h"
#include "programs.h"
#include "rawframe/material/material.h"
#include "rawframe/render/device.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/test/test.h"
#include "rawframe/texture/texture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <utility>
#include <vector>

using namespace rawframe;
using render_scene::SceneDraw;
using render_scene::SceneFrame;
using namespace rawframe::scene_fixture;

RAWFRAME_TEST(ACoatShinesOverARoughBase) {
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
    // A black ball under the default sun, rough (0.9), its highlight spread
    // thin; then coated.material as rawframe.material writes it
    // (coated_graph.slang, D716), the same base under a smooth coat, which
    // gathers the sun's highlight where the plain one spreads it.
    SceneFrame frame = looking();
    frame.shadows.count = 0;
    SceneDraw ball = box(4, 1.5F, {0, 0, 0, 1}, render_scene::kSphere);
    ball.material = 1;
    frame.draws = {ball};
    render_scene::MaterialBlob rough = render_scene::noMaterial();
    rough[7] = 0.9F;
    frame.materials = {render_scene::noMaterial(), rough};
    const auto kPlain = drawn(**framer, **made, frame, kMeshes);
    frame.programs = {nullptr, programOf(kCoatedGraphScene, kCoatedGraphShadow, material::Shading::Lit)};
    const auto kCoated =
        drawnWith(**framer, **made, frame, kMeshes, &render_scene_gpu::RendererStatistics::programModelsDrawn);
    RAWFRAME_EXPECT(kPlain.has_value() && kCoated.has_value());
    if (!kPlain.has_value() || !kCoated.has_value()) {
        return;
    }
    // The brightest red in each: the sun's highlight.
    const auto kBrightest = [](const std::vector<std::byte>& pixels) {
        int most = 0;
        for (std::uint32_t y = 0; y < kSide; ++y) {
            for (std::uint32_t x = 0; x < kSide; ++x) {
                most = std::max(most, at(pixels, x, y)[0]);
            }
        }
        return most;
    };
    const int kRough = kBrightest(*kPlain);
    const int kShining = kBrightest(*kCoated);
    std::printf("highlight rough %d, coated %d\n", kRough, kShining);
    RAWFRAME_EXPECT(kShining > kRough + 30);
}

RAWFRAME_TEST(AnAnisotropyStretchesTheHighlightAlongItsTurn) {
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
    // brushed.material as rawframe.material writes it (brushed_graph.slang,
    // D717) on a black ball under the default sun, no sky or ground: its
    // roughness stretched
    // along the mesh's tangent turned by its turn parameter. The sun's
    // highlight, the pixels at least half as red as the brightest, is
    // drawn out one way unturned and the other way turned a quarter.
    SceneFrame frame = looking();
    frame.shadows.count = 0;
    frame.lights.sky = {0, 0, 0};
    frame.lights.ground = {0, 0, 0};
    SceneDraw ball = box(4, 1.5F, {0, 0, 0, 1}, render_scene::kSphere);
    ball.material = 1;
    frame.draws = {ball};
    frame.programs = {nullptr, programOf(kBrushedGraphScene, kBrushedGraphShadow, material::Shading::Lit)};
    const auto kSpread = [&](float turn) -> std::array<int, 2> {
        render_scene::MaterialBlob states = render_scene::noMaterial();
        states[material::kBlobFloats] = turn;
        frame.materials = {render_scene::noMaterial(), states};
        const auto kPixels =
            drawnWith(**framer, **made, frame, kMeshes, &render_scene_gpu::RendererStatistics::programModelsDrawn);
        if (!kPixels.has_value()) {
            return {};
        }
        int most = 0;
        for (std::uint32_t y = 0; y < kSide; ++y) {
            for (std::uint32_t x = 0; x < kSide; ++x) {
                most = std::max(most, at(*kPixels, x, y)[0]);
            }
        }
        std::array<int, 4> bounds = {static_cast<int>(kSide), static_cast<int>(kSide), -1, -1};
        for (std::uint32_t y = 0; y < kSide; ++y) {
            for (std::uint32_t x = 0; x < kSide; ++x) {
                if (at(*kPixels, x, y)[0] * 2 >= most && most > 0) {
                    bounds = {std::min(bounds[0], static_cast<int>(x)),
                              std::min(bounds[1], static_cast<int>(y)),
                              std::max(bounds[2], static_cast<int>(x)),
                              std::max(bounds[3], static_cast<int>(y))};
                }
            }
        }
        return {bounds[2] - bounds[0] + 1, bounds[3] - bounds[1] + 1};
    };
    const std::array<int, 2> kAlong = kSpread(0);
    const std::array<int, 2> kTurned = kSpread(1.5707964F);
    std::printf("highlight unturned %d by %d, turned %d by %d\n", kAlong[0], kAlong[1], kTurned[0], kTurned[1]);
    RAWFRAME_EXPECT((kAlong[0] > kAlong[1]) != (kTurned[0] > kTurned[1]));
    RAWFRAME_EXPECT(std::max(kAlong[0], kAlong[1]) >= 2 * std::min(kAlong[0], kAlong[1]));
}

RAWFRAME_TEST(AGlassShowsWhatLiesBehindIt) {
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
    // A red box six meters ahead and a ball between, drawn after it as the
    // translucent are: white and plain, it hides the box; of glass.material
    // as rawframe.material writes it (glass_graph.slang, D720), it shows
    // the box through itself, a copy of what the opaque models made.
    SceneFrame frame = looking();
    frame.shadows.count = 0;
    SceneDraw back = box(6, 2, {1, 0, 0, 1});
    SceneDraw ball = box(3, 0.8F, {1, 1, 1, 1}, render_scene::kSphere);
    ball.material = 1;
    frame.draws = {back, ball};
    frame.translucent = 1;
    frame.materials = {render_scene::noMaterial(), render_scene::noMaterial()};
    const auto kPlain = drawn(**framer, **made, frame, kMeshes);
    // Its program transmits, as the cook says of it.
    auto glass = std::make_shared<material::ProgramMaterial>(
        *programOf(kGlassGraphScene, kGlassGraphShadow, material::Shading::Lit));
    glass->transmits = true;
    frame.programs = {nullptr, glass};
    const auto kGlass =
        drawnWith(**framer, **made, frame, kMeshes, &render_scene_gpu::RendererStatistics::programModelsDrawn);
    RAWFRAME_EXPECT(kPlain.has_value() && kGlass.has_value());
    if (!kPlain.has_value() || !kGlass.has_value()) {
        return;
    }
    const std::array<int, 3> kHidden = at(*kPlain, 32, 32);
    const std::array<int, 3> kSeen = at(*kGlass, 32, 32);
    std::printf("through the ball: plain %d %d %d, glass %d %d %d\n",
                kHidden[0],
                kHidden[1],
                kHidden[2],
                kSeen[0],
                kSeen[1],
                kSeen[2]);
    RAWFRAME_EXPECT(std::abs(kHidden[0] - kHidden[1]) < 30);
    RAWFRAME_EXPECT(kSeen[0] > kSeen[1] + 60 && kSeen[0] > kSeen[2] + 60);
}

RAWFRAME_TEST(AFrostedGlassBlursWhatLiesBehindIt) {
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
    // A red box and a blue one six meters ahead, meeting in the middle,
    // and a ball between: through clear glass (glass_graph.slang) the two
    // stand apart on either side of the middle; through frosted glass,
    // rough (frosted_graph.slang, D723), the copy is read at a level its
    // roughness picks, and the two run into each other there.
    SceneFrame frame = looking();
    frame.shadows.count = 0;
    SceneDraw red = box(6, 2, {1, 0, 0, 1});
    red.model[12] = -2;
    SceneDraw blue = box(6, 2, {0, 0, 1, 1});
    blue.model[12] = 2;
    SceneDraw ball = box(3, 0.8F, {1, 1, 1, 1}, render_scene::kSphere);
    ball.material = 1;
    frame.draws = {red, blue, ball};
    frame.translucent = 1;
    frame.materials = {render_scene::noMaterial(), render_scene::noMaterial()};
    const auto kThrough = [&](std::span<const std::uint8_t> scene, std::span<const std::uint8_t> shadow) {
        auto glass = std::make_shared<material::ProgramMaterial>(*programOf(scene, shadow, material::Shading::Lit));
        glass->transmits = true;
        frame.programs = {nullptr, glass};
        return drawnWith(**framer, **made, frame, kMeshes, &render_scene_gpu::RendererStatistics::programModelsDrawn);
    };
    const auto kClear = kThrough(kGlassGraphScene, kGlassGraphShadow);
    const auto kFrosted = kThrough(kFrostedGraphScene, kFrostedGraphShadow);
    RAWFRAME_EXPECT(kClear.has_value() && kFrosted.has_value());
    if (!kClear.has_value() || !kFrosted.has_value()) {
        return;
    }
    // How much redder than blue the left of the middle is than its right.
    const auto kApart = [](const std::vector<std::byte>& pixels) {
        const std::array<int, 3> kLeft = at(pixels, 28, 32);
        const std::array<int, 3> kRight = at(pixels, 36, 32);
        std::printf("left %d %d %d, right %d %d %d\n", kLeft[0], kLeft[1], kLeft[2], kRight[0], kRight[1], kRight[2]);
        return (kLeft[0] - kLeft[2]) - (kRight[0] - kRight[2]);
    };
    const int kClearApart = kApart(*kClear);
    const int kFrostedApart = kApart(*kFrosted);
    std::printf("through clear glass the sides are %d apart, through frosted %d\n", kClearApart, kFrostedApart);
    RAWFRAME_EXPECT(kClearApart > 150);
    RAWFRAME_EXPECT(kFrostedApart * 2 < kClearApart);
}

RAWFRAME_TEST(AGlassLetsTheSunThroughFromBehindIt) {
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
    // A glass ball alone, in the dark but for the sun straight behind it:
    // its light passes through the glass toward the eye (D724), brightest
    // where the way to the eye and the way to the sun meet, the ball's
    // middle, and dark toward its edge; with no sun, the ball shows only
    // the dark behind it.
    SceneFrame frame = looking();
    frame.shadows.count = 0;
    frame.lights.sky = {0, 0, 0};
    frame.lights.ground = {0, 0, 0};
    frame.lights.toSun = {0, 0, -1};
    SceneDraw ball = box(3, 0.8F, {1, 1, 1, 1}, render_scene::kSphere);
    ball.material = 1;
    frame.draws = {ball};
    frame.translucent = 1;
    frame.materials = {render_scene::noMaterial()};
    auto glass = std::make_shared<material::ProgramMaterial>(
        *programOf(kGlassGraphScene, kGlassGraphShadow, material::Shading::Lit));
    glass->transmits = true;
    frame.programs = {glass};
    frame.lights.sun = {0, 0, 0};
    const auto kDark =
        drawnWith(**framer, **made, frame, kMeshes, &render_scene_gpu::RendererStatistics::programModelsDrawn);
    frame.lights.sun = {100000, 100000, 100000};
    const auto kLit =
        drawnWith(**framer, **made, frame, kMeshes, &render_scene_gpu::RendererStatistics::programModelsDrawn);
    RAWFRAME_EXPECT(kDark.has_value() && kLit.has_value());
    if (!kDark.has_value() || !kLit.has_value()) {
        return;
    }
    // The brightest pixel about the middle: a lobe this smooth is narrower
    // than a pixel.
    std::array<int, 3> kThrough{};
    for (std::uint32_t y = 30; y < 35; ++y) {
        for (std::uint32_t x = 30; x < 35; ++x) {
            const std::array<int, 3> kAt = at(*kLit, x, y);
            if (kAt[0] + kAt[1] + kAt[2] > kThrough[0] + kThrough[1] + kThrough[2]) {
                kThrough = kAt;
            }
        }
    }
    const std::array<int, 3> kUnlit = at(*kDark, 32, 32);
    const std::array<int, 3> kEdge = at(*kLit, 32, 26);
    std::printf("the ball's middle: no sun %d %d %d, sun behind %d %d %d; toward its edge %d %d %d\n",
                kUnlit[0],
                kUnlit[1],
                kUnlit[2],
                kThrough[0],
                kThrough[1],
                kThrough[2],
                kEdge[0],
                kEdge[1],
                kEdge[2]);
    RAWFRAME_EXPECT(kUnlit[0] + kUnlit[1] + kUnlit[2] < 30);
    RAWFRAME_EXPECT(kThrough[0] > 100 && kThrough[1] > 100 && kThrough[2] > 100);
    RAWFRAME_EXPECT(kEdge[0] + kEdge[1] + kEdge[2] + 150 < kThrough[0] + kThrough[1] + kThrough[2]);
}

RAWFRAME_TEST(ACoatReflectsTheProbesThatHoldIt) {
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
    // A sky's picture red all round and a room's green all round, each a
    // cube of one level.
    constexpr std::uint64_t kSky = 0x51c7e0a2b9d34f68ULL;
    constexpr std::uint64_t kRoom = 0x7c2d5e18a94b03f1ULL;
    const auto kCubeOf = [](float red, float green) {
        texture::Texture cube{.format = texture::Format::Rgba16Float, .faces = 6};
        texture::Level level{.width = 4, .height = 4};
        for (std::uint32_t texel = 0; texel < 6 * 16; ++texel) {
            for (const float kChannel : {red, green, 0.0F, 1.0F}) {
                const std::uint16_t kHalf = texture::halfOf(kChannel);
                level.bytes.push_back(static_cast<std::byte>(kHalf & 0xFFU));
                level.bytes.push_back(static_cast<std::byte>(kHalf >> 8U));
            }
        }
        cube.levels.push_back(std::move(level));
        return std::make_shared<const texture::Texture>(std::move(cube));
    };
    const std::shared_ptr<const texture::Texture> kRed = kCubeOf(1, 0);
    const std::shared_ptr<const texture::Texture> kGreen = kCubeOf(0, 1);
    const render_scene_gpu::TextureSource kTextures = [&](std::uint64_t id) -> std::shared_ptr<const texture::Texture> {
        return id == kSky ? kRed : id == kRoom ? kGreen : nullptr;
    };
    // A black ball under coated.material's smooth coat (coated_graph.slang,
    // D716) in a green room, under a red sky: its coat reflects the room
    // that holds it (D725), as its base does, and not the sky beyond it.
    SceneFrame frame = looking();
    frame.shadows.count = 0;
    frame.lights.sun = {0, 0, 0};
    frame.lights.ground = {0, 0, 0};
    frame.lights.sky = {20000, 20000, 20000};
    frame.lights.environment = kSky;
    SceneDraw ball = box(4, 1.5F, {0, 0, 0, 1}, render_scene::kSphere);
    ball.material = 1;
    frame.draws = {ball};
    frame.materials = {render_scene::noMaterial(), render_scene::noMaterial()};
    frame.probes = {
        render_scene::SceneProbe{.position = {0, 0, -4}, .half = {3, 3, 3}, .environment = kRoom, .intensity = 20000}};
    frame.clusters = {.tilesX = 1, .tilesY = 1, .slices = 1};
    frame.clusters.ranges = {0, 0, 0, 1};
    frame.clusters.indices = {0};
    frame.programs = {nullptr, programOf(kCoatedGraphScene, kCoatedGraphShadow, material::Shading::Lit)};
    // Its probe's pipeline, then its program's, each made as the device
    // answers (D337, D485).
    static_cast<void>(
        drawnWith(**framer, **made, frame, kMeshes, &render_scene_gpu::RendererStatistics::probesDrawn, kTextures));
    const auto kCoated = drawnWith(
        **framer, **made, frame, kMeshes, &render_scene_gpu::RendererStatistics::programModelsDrawn, kTextures);
    RAWFRAME_EXPECT(kCoated.has_value());
    if (!kCoated.has_value()) {
        return;
    }
    const std::array<int, 3> kMiddle = at(*kCoated, kSide / 2, kSide / 2);
    std::printf("a coat in a green room under a red sky: %d %d %d\n", kMiddle[0], kMiddle[1], kMiddle[2]);
    RAWFRAME_EXPECT(kMiddle[1] > kMiddle[0] + 30 && kMiddle[0] < 20);
}

RAWFRAME_TEST(ASeeThroughCasterCastsAsMuchShadowAsItCovers) {
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
    // The sun's shadows' scene again: a white floor and a box above it. The
    // box opaque casts all its shadow; drawn translucent at half its
    // opacity, about half (D726); of glass.material, which transmits all
    // but its reflection, almost none. Each blends, as the scene marks it.
    SceneFrame frame = looking();
    SceneDraw floor = box(8, 1, {1, 1, 1, 1});
    floor.model[0] = 10;
    floor.model[5] = 0.1F;
    floor.model[10] = 10;
    floor.normal[0] = 0.1F;
    floor.normal[5] = 10;
    floor.normal[10] = 0.1F;
    floor.model[13] = -2;
    SceneDraw cube = box(8, 0.5F, {1, 1, 1, 1});
    cube.material = 1;
    frame.draws = {floor, cube};
    frame.shadows.casters = frame.draws;
    for (std::size_t at = 0; at < frame.shadows.count; ++at) {
        frame.shadows.cascades[at].firstCaster = 0;
        frame.shadows.cascades[at].casterCount = 2;
    }
    const auto kShade = [](const std::vector<std::byte>& pixels) {
        int sum = 0;
        for (std::uint32_t y = 38; y < 41; ++y) {
            for (std::uint32_t x = 28; x < 32; ++x) {
                sum += at(pixels, x, y)[0];
            }
        }
        return sum;
    };
    frame.materials = {render_scene::noMaterial(), render_scene::noMaterial()};
    frame.programs = {nullptr, nullptr};
    const auto kOpaque = drawn(**framer, **made, frame, kMeshes);
    render_scene::MaterialBlob half = render_scene::noMaterial();
    half[12] = 0.5F;
    half[render_scene::kFlagsAt] =
        static_cast<float>(static_cast<std::uint32_t>(half[render_scene::kFlagsAt]) | render_scene::kBlendsFlag);
    frame.materials = {render_scene::noMaterial(), half};
    frame.translucent = 1;
    const auto kHalf = drawn(**framer, **made, frame, kMeshes);
    render_scene::MaterialBlob blends = render_scene::noMaterial();
    blends[render_scene::kFlagsAt] = static_cast<float>(render_scene::kBlendsFlag);
    frame.materials = {render_scene::noMaterial(), blends};
    auto glass = std::make_shared<material::ProgramMaterial>(
        *programOf(kGlassGraphScene, kGlassGraphShadow, material::Shading::Lit));
    glass->transmits = true;
    frame.programs = {nullptr, glass};
    const auto kGlass =
        drawnWith(**framer, **made, frame, kMeshes, &render_scene_gpu::RendererStatistics::programModelsDrawn);
    frame.shadows.count = 0;
    const auto kUnshaded = drawn(**framer, **made, frame, kMeshes);
    RAWFRAME_EXPECT(kOpaque.has_value() && kHalf.has_value() && kGlass.has_value() && kUnshaded.has_value());
    if (!kOpaque.has_value() || !kHalf.has_value() || !kGlass.has_value() || !kUnshaded.has_value()) {
        return;
    }
    const int kOpaqueShade = kShade(*kOpaque);
    const int kHalfShade = kShade(*kHalf);
    const int kGlassShade = kShade(*kGlass);
    const int kNoShade = kShade(*kUnshaded);
    std::printf("the shadow's window: opaque %d, half translucent %d, glass %d, no shadow %d\n",
                kOpaqueShade,
                kHalfShade,
                kGlassShade,
                kNoShade);
    // The half's shadow lies between; the noise and the filter make it
    // lighter than half.
    RAWFRAME_EXPECT(kOpaqueShade + 300 < kHalfShade && kHalfShade + 100 < kNoShade);
    RAWFRAME_EXPECT(kGlassShade + 50 > kNoShade && kHalfShade + 100 < kGlassShade);
}
