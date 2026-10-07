// A material's own program lights its model on the device (D485) on
// lavapipe: the scene's containers linked with a material written by hand
// (tests/materials/checker.slang), built by tools/gen_shaders.py as the
// cook builds a game's (D484). Skips where no adapter answers, unless
// RAWFRAME_REQUIRE_GPU is set.

#include "fixture.h"
#include "generated/checker_container.h"
#include "rawframe/material/material.h"
#include "rawframe/render/device.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/test/test.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

using namespace rawframe;
using render_scene::SceneDraw;
using render_scene::SceneFrame;
using namespace rawframe::scene_fixture;

namespace {

/// The checkerboard's program material: unlit, the container the build's
/// driver reads in each place, which is the one the renderer takes.
std::shared_ptr<const material::ProgramMaterial> checker() {
    material::ProgramMaterial made{.shading = material::Shading::Unlit};
    for (std::vector<std::byte>& container : made.containers) {
        container.resize(kCheckerContainer.size());
        for (std::size_t at = 0; at < kCheckerContainer.size(); ++at) {
            container[at] = static_cast<std::byte>(kCheckerContainer[at]);
        }
    }
    return std::make_shared<const material::ProgramMaterial>(std::move(made));
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
        "plain red %d green %d, program red %d green %d, center %d %d %d; program models drawn %llu, waiting %llu\n",
        kWhite[0],
        kWhite[1],
        kChecked[0],
        kChecked[1],
        at(*kOwn, 32, 32)[0],
        at(*kOwn, 32, 32)[1],
        at(*kOwn, 32, 32)[2],
        static_cast<unsigned long long>(kStatistics.programModelsDrawn),
        static_cast<unsigned long long>(kStatistics.programModelsWaiting));
    RAWFRAME_EXPECT(kWhite[0] == 0 && kWhite[1] == 0 && kChecked[0] > 100 && kChecked[1] > 100 &&
                    kStatistics.programModelsDrawn > 0);
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
