// The decals on lavapipe (D339): a decal thrown onto an unlit white box's
// face shows its texture there the right way round, green on its left and
// blue on its right, and the face around it stays white.

#include "fixture.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/test/test.h"

#include <array>
#include <cstdio>
#include <memory>

using namespace rawframe;
using namespace rawframe::scene_fixture;
using render_scene::SceneFrame;

RAWFRAME_TEST(ADecalLaysItsTextureOverTheSurfaceInItsBox) {
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
    // Two texels across: green, then blue.
    texture::Texture halves{.format = texture::Format::Rgba8Srgb};
    halves.levels.push_back({.width = 2,
                             .height = 1,
                             .bytes = {std::byte{0},
                                       std::byte{255},
                                       std::byte{0},
                                       std::byte{255},
                                       std::byte{0},
                                       std::byte{0},
                                       std::byte{255},
                                       std::byte{255}}});
    const auto kHalves = std::make_shared<const texture::Texture>(std::move(halves));
    const render_scene_gpu::TextureSource kTextures = [&](std::uint64_t id) {
        return id == 0xde ? kHalves : nullptr;
    };
    // An unlit white box four meters ahead, its face toward the eye at 2.5
    // meters; a decal a meter square on the face's left half, thrown along
    // -Z.
    SceneFrame frame = looking();
    frame.shadows.count = 0;
    frame.dither = false;
    render_scene::SceneDraw shown = box(4, 1.5F, {1, 1, 1, 1});
    shown.material = 1;
    frame.draws = {shown};
    render_scene::MaterialBlob unlit = render_scene::noMaterial();
    unlit[15] = 1;
    frame.materials = {render_scene::noMaterial(), unlit};
    render_scene::SceneDecal decal{.color = {1, 1, 1, 1}, .texture = 0xde};
    // Into its box: one over its half sides, about (-0.5, 0, -2.5).
    decal.toBox = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 1, 0, 5, 1};
    frame.decals = {decal};
    // One cluster holding the decal.
    frame.clusters = {.tilesX = 1, .tilesY = 1, .slices = 1, .ranges = {0, 0, 1, 0}, .indices = {0}};
    const auto kPixels =
        drawnWith(**framer, **made, frame, kMeshes, &render_scene_gpu::RendererStatistics::decalsDrawn, kTextures);
    RAWFRAME_EXPECT(kPixels.has_value());
    if (!kPixels.has_value()) {
        return;
    }
    const auto kLeft = at(*kPixels, 21, kSide / 2);
    const auto kRight = at(*kPixels, 30, kSide / 2);
    const auto kOutside = at(*kPixels, 15, kSide / 2);
    const auto kBeside = at(*kPixels, 45, kSide / 2);
    std::printf("the decal's left %d %d %d, right %d %d %d; the face outside %d %d %d and beside %d %d %d\n",
                kLeft[0],
                kLeft[1],
                kLeft[2],
                kRight[0],
                kRight[1],
                kRight[2],
                kOutside[0],
                kOutside[1],
                kOutside[2],
                kBeside[0],
                kBeside[1],
                kBeside[2]);
    RAWFRAME_EXPECT(kLeft[1] > kLeft[0] + 100 && kLeft[1] > kLeft[2] + 100);
    RAWFRAME_EXPECT(kRight[2] > kRight[0] + 100 && kRight[2] > kRight[1] + 100);
    RAWFRAME_EXPECT(kOutside[0] > 200 && kOutside[1] > 200 && kOutside[2] > 200 && kBeside[0] > 200 &&
                    kBeside[1] > 200 && kBeside[2] > 200);
}
