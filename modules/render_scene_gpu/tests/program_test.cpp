// A material's own program lights its model on the device (D485) on
// lavapipe: the scene's containers linked with a material written by hand
// (tests/materials/checker.slang), built by tools/gen_shaders.py as the
// cook builds a game's (D484). Skips where no adapter answers, unless
// RAWFRAME_REQUIRE_GPU is set.

#include "fixture.h"
#include "generated/checker_container.h"
#include "generated/checker_shadow_container.h"
#include "generated/faded_graph_scene_container.h"
#include "generated/faded_graph_shadow_container.h"
#include "generated/faded_scene_container.h"
#include "generated/faded_shadow_container.h"
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

namespace {

/// A program material of `shading`, the scene's and the shadow's container
/// the build's driver reads in each place, which is the one the renderer
/// takes.
std::shared_ptr<const material::ProgramMaterial>
programOf(std::span<const std::uint8_t> scene, std::span<const std::uint8_t> shadow, material::Shading shading) {
    material::ProgramMaterial made{.shading = shading};
    const auto kBytes = [](std::span<const std::uint8_t> embedded) {
        std::vector<std::byte> copied(embedded.size());
        for (std::size_t at = 0; at < embedded.size(); ++at) {
            copied[at] = static_cast<std::byte>(embedded[at]);
        }
        return copied;
    };
    made.containers.fill(kBytes(scene));
    made.shadows.fill(kBytes(shadow));
    return std::make_shared<const material::ProgramMaterial>(std::move(made));
}

/// The checkerboard's program material, unlit.
std::shared_ptr<const material::ProgramMaterial> checker() {
    return programOf(kCheckerContainer, kCheckerShadowContainer, material::Shading::Unlit);
}

} // namespace

RAWFRAME_TEST(AMaterialsOwnProgramLightsItsModel) {
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
    // A white box four meters ahead in the dark, exposed so fifty nits are
    // middle grey, its material second in the frame's.
    SceneFrame frame = looking();
    frame.shadows.count = 0;
    frame.lights.sun = {0, 0, 0};
    frame.lights.ground = {0, 0, 0};
    frame.lights.sky = {0, 0, 0};
    frame.exposure = std::log2(50.0F / (1.2F * 0.18F));
    SceneDraw shown = box(4, 1.5F, {1, 1, 1, 1});
    shown.material = 1;
    frame.draws = {shown};
    render_scene::MaterialBlob states = render_scene::noMaterial();
    states[15] = 1;
    frame.materials = {render_scene::noMaterial(), states};
    // The pixels of the box's face strongly red, and strongly green.
    const auto kCounted = [](const std::vector<std::byte>& pixels) {
        std::array<int, 2> counted{};
        for (std::uint32_t y = 16; y < 48; ++y) {
            for (std::uint32_t x = 16; x < 48; ++x) {
                const std::array<int, 3> kAt = at(pixels, x, y);
                counted[0] += kAt[0] > kAt[1] + 100 && kAt[0] > kAt[2] + 100 ? 1 : 0;
                counted[1] += kAt[1] > kAt[0] + 100 && kAt[1] > kAt[2] + 100 ? 1 : 0;
            }
        }
        return counted;
    };
    // Without its program, the blob's white; with it, the checkerboard,
    // once its pipelines are made.
    frame.programs = {nullptr, nullptr};
    const auto kPlain = drawn(**framer, **made, frame, kMeshes);
    frame.programs = {nullptr, checker()};
    const auto kOwn =
        drawnWith(**framer, **made, frame, kMeshes, &render_scene_gpu::RendererStatistics::programModelsDrawn);
    RAWFRAME_EXPECT(kPlain.has_value() && kOwn.has_value());
    if (!kPlain.has_value() || !kOwn.has_value()) {
        return;
    }
    const std::array<int, 2> kWhite = kCounted(*kPlain);
    const std::array<int, 2> kChecked = kCounted(*kOwn);
    const render_scene_gpu::RendererStatistics kStatistics = (*made)->statistics();
    std::printf(
        "plain red %d green %d, program red %d green %d, center %d %d %d; program models drawn %llu, waiting %llu; "
        "programs refused %llu%s%s\n",
        kWhite[0],
        kWhite[1],
        kChecked[0],
        kChecked[1],
        at(*kOwn, 32, 32)[0],
        at(*kOwn, 32, 32)[1],
        at(*kOwn, 32, 32)[2],
        static_cast<unsigned long long>(kStatistics.programModelsDrawn),
        static_cast<unsigned long long>(kStatistics.programModelsWaiting),
        static_cast<unsigned long long>(kStatistics.programsRefused),
        kStatistics.programRefusal.empty() ? "" : ", the first by ",
        kStatistics.programRefusal.c_str());
    RAWFRAME_EXPECT(kStatistics.programsRefused == 0 && kWhite[0] == 0 && kWhite[1] == 0 && kChecked[0] > 100 &&
                    kChecked[1] > 100 && kStatistics.programModelsDrawn > 0);
}

RAWFRAME_TEST(AMaskedProgramMaterialIsCutWhereItsProgramSays) {
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
    // The checkerboard masked at a half (D310): its program clears the red
    // cells, so the prepass cuts them away (D487) and the dark behind shows
    // there; its blob, its states alone, would cut nothing.
    SceneFrame frame = looking();
    frame.shadows.count = 0;
    frame.lights.sun = {0, 0, 0};
    frame.lights.ground = {0, 0, 0};
    frame.lights.sky = {0, 0, 0};
    frame.exposure = std::log2(50.0F / (1.2F * 0.18F));
    SceneDraw shown = box(4, 1.5F, {1, 1, 1, 1});
    shown.material = 1;
    frame.draws = {shown};
    render_scene::MaterialBlob states = render_scene::noMaterial();
    states[14] = 0.5F;
    states[15] = 1;
    frame.materials = {render_scene::noMaterial(), states};
    frame.programs = {nullptr, checker()};
    const auto kCut =
        drawnWith(**framer, **made, frame, kMeshes, &render_scene_gpu::RendererStatistics::programModelsDrawn);
    RAWFRAME_EXPECT(kCut.has_value());
    if (!kCut.has_value()) {
        return;
    }
    std::array<int, 3> counted{};
    for (std::uint32_t y = 16; y < 48; ++y) {
        for (std::uint32_t x = 16; x < 48; ++x) {
            const std::array<int, 3> kAt = at(*kCut, x, y);
            counted[0] += kAt[0] > kAt[1] + 100 && kAt[0] > kAt[2] + 100 ? 1 : 0;
            counted[1] += kAt[1] > kAt[0] + 100 && kAt[1] > kAt[2] + 100 ? 1 : 0;
            counted[2] += kAt[0] + kAt[1] + kAt[2] < 30 ? 1 : 0;
        }
    }
    std::printf("masked: red %d, green %d, dark %d\n", counted[0], counted[1], counted[2]);
    // No face is culled, so through the front's holes the back's green
    // cells show too, and the dark where both are cut.
    RAWFRAME_EXPECT(counted[0] == 0 && counted[1] > 400 && counted[2] > 100);
}

RAWFRAME_TEST(AMaskedProgramMaterialCastsOnlyWhatItsProgramLeaves) {
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
    // The sun's shadows' scene (renderer_test.cpp): a white floor two meters
    // below the eye and a box above it eight meters ahead, its shadow on the
    // floor behind it and to its left. The box is the checkerboard masked
    // at a half: its states alone cut nothing, so it casts all its shadow;
    // its program clears its red cells, so light passes through it (D488).
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
    render_scene::MaterialBlob states = render_scene::noMaterial();
    states[14] = 0.5F;
    states[15] = 1;
    frame.materials = {render_scene::noMaterial(), states};
    // The floor's red in the shadow, summed over a window of it.
    const auto kShade = [](const std::vector<std::byte>& pixels) {
        int sum = 0;
        for (std::uint32_t y = 38; y < 41; ++y) {
            for (std::uint32_t x = 28; x < 32; ++x) {
                sum += at(pixels, x, y)[0];
            }
        }
        return sum;
    };
    frame.programs = {nullptr, nullptr};
    const auto kWhole = drawn(**framer, **made, frame, kMeshes);
    frame.programs = {nullptr, checker()};
    const auto kCut =
        drawnWith(**framer, **made, frame, kMeshes, &render_scene_gpu::RendererStatistics::programModelsDrawn);
    frame.shadows.count = 0;
    const auto kUnshaded = drawn(**framer, **made, frame, kMeshes);
    RAWFRAME_EXPECT(kWhole.has_value() && kCut.has_value() && kUnshaded.has_value());
    if (!kWhole.has_value() || !kCut.has_value() || !kUnshaded.has_value()) {
        return;
    }
    const int kWholeShade = kShade(*kWhole);
    const int kCutShade = kShade(*kCut);
    const int kNoShade = kShade(*kUnshaded);
    std::printf("the shadow's window: whole caster %d, cut by its program %d, no shadow %d\n",
                kWholeShade,
                kCutShade,
                kNoShade);
    RAWFRAME_EXPECT(kWholeShade + 200 < kCutShade && kCutShade + 200 < kNoShade);
}

RAWFRAME_TEST(AGraphMaterialDrawsAsItsHandwrittenTwinDoes) {
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
    // ADR-0026's proof (D490): faded.material as rawframe.material writes it
    // (faded_graph.slang, checked word for word by material's tests) and
    // the same written by hand (faded.slang), each built by gen_shaders.py
    // as the cook builds a game's, draw a box lit by the default sun, its
    // texture four texels of four colors and alphas, pixel for pixel alike.
    texture::Texture four{.format = texture::Format::Rgba8Srgb};
    four.levels.push_back({.width = 2,
                           .height = 2,
                           .bytes = {std::byte{230},
                                     std::byte{40},
                                     std::byte{40},
                                     std::byte{255},
                                     std::byte{40},
                                     std::byte{200},
                                     std::byte{60},
                                     std::byte{96},
                                     std::byte{50},
                                     std::byte{70},
                                     std::byte{220},
                                     std::byte{180},
                                     std::byte{240},
                                     std::byte{240},
                                     std::byte{200},
                                     std::byte{24}}});
    const auto kFour = std::make_shared<const texture::Texture>(std::move(four));
    const render_scene_gpu::TextureSource kTextures = [&kFour](std::uint64_t id) {
        return id == 0xa44ecb4a39ac5cc8ULL ? kFour : nullptr;
    };
    SceneFrame frame = looking();
    frame.shadows.count = 0;
    SceneDraw shown = box(4, 1.5F, {1, 1, 1, 1});
    shown.material = 1;
    frame.draws = {shown};
    frame.materials = {render_scene::noMaterial(), render_scene::noMaterial()};
    frame.textures = {{}, {.base = {.id = 0xa44ecb4a39ac5cc8ULL}}};
    frame.programs = {nullptr, programOf(kFadedGraphScene, kFadedGraphShadow, material::Shading::Lit)};
    const auto kGraph = drawnWith(
        **framer, **made, frame, kMeshes, &render_scene_gpu::RendererStatistics::programModelsDrawn, kTextures);
    frame.programs = {nullptr, programOf(kFadedScene, kFadedShadow, material::Shading::Lit)};
    const auto kHand = drawnWith(
        **framer, **made, frame, kMeshes, &render_scene_gpu::RendererStatistics::programModelsDrawn, kTextures);
    frame.programs = {nullptr, nullptr};
    const auto kBlob = drawn(**framer, **made, frame, kMeshes, kTextures);
    RAWFRAME_EXPECT(kGraph.has_value() && kHand.has_value() && kBlob.has_value());
    if (!kGraph.has_value() || !kHand.has_value() || !kBlob.has_value()) {
        return;
    }
    // Not a picture both would draw anyway: the blob's white material draws
    // another, and the box's face shows more than one texel's shade.
    std::size_t differing = 0;
    std::size_t fromBlob = 0;
    std::array<int, 3> least{255, 255, 255};
    std::array<int, 3> most{};
    for (std::uint32_t y = 16; y < 48; ++y) {
        for (std::uint32_t x = 16; x < 48; ++x) {
            const std::array<int, 3> kGraphAt = at(*kGraph, x, y);
            differing += kGraphAt != at(*kHand, x, y) ? 1 : 0;
            fromBlob += kGraphAt != at(*kBlob, x, y) ? 1 : 0;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                least.at(channel) = std::min(least.at(channel), kGraphAt.at(channel));
                most.at(channel) = std::max(most.at(channel), kGraphAt.at(channel));
            }
        }
    }
    std::printf("graph against hand: %zu pixels differ; against the blob %zu; red %d to %d, green %d to %d\n",
                differing,
                fromBlob,
                least[0],
                most[0],
                least[1],
                most[1]);
    RAWFRAME_EXPECT(*kGraph == *kHand && fromBlob > 500 && most[0] - least[0] > 40 && most[1] - least[1] > 40);
}
