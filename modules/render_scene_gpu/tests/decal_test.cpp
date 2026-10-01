// The decals on lavapipe (D339): a decal thrown onto an unlit white box's
// face shows its texture there the right way round, green on its left and
// blue on its right, and the face around it stays white. A decal's normal
// texture turns a lit face toward its right where it lies, to a sun there,
// and its roughness spreads a smooth metal's highlight there (D342).

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

RAWFRAME_TEST(ADecalBendsTheNormalsAndLaysTheRoughnessInItsBox) {
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
    // White, covering all; and a normal turned sixty degrees toward the
    // decal's right, (0.87, 0, 0.5) as glTF's normal textures hold it.
    const auto kTexel = [](std::array<std::uint8_t, 4> bytes, texture::Format format) {
        texture::Texture texel{.format = format};
        texel.levels.push_back(
            {.width = 1,
             .height = 1,
             .bytes = {std::byte{bytes[0]}, std::byte{bytes[1]}, std::byte{bytes[2]}, std::byte{bytes[3]}}});
        return std::make_shared<const texture::Texture>(std::move(texel));
    };
    const auto kWhite = kTexel({255, 255, 255, 255}, texture::Format::Rgba8Srgb);
    const auto kTurned = kTexel({238, 128, 191, 255}, texture::Format::Rgba8);
    const render_scene_gpu::TextureSource kTextures = [&](std::uint64_t id) -> std::shared_ptr<const texture::Texture> {
        if (id == 0xde) {
            return kWhite;
        }
        return id == 0xdf ? kTurned : nullptr;
    };
    // A white box's face 2.5 meters ahead, lit by the sun alone, sixty
    // degrees to the right; a decal a meter square on the face's left
    // half.
    SceneFrame frame = looking();
    frame.shadows.count = 0;
    frame.dither = false;
    frame.lights.sky = {0, 0, 0};
    frame.lights.ground = {0, 0, 0};
    frame.lights.sun = {10000, 10000, 10000};
    frame.lights.toSun = {0.866F, 0, 0.5F};
    frame.draws = {box(4, 1.5F, {1, 1, 1, 1})};
    frame.materials = {render_scene::noMaterial()};
    render_scene::SceneDecal decal{.color = {1, 1, 1, 1}, .texture = 0xde, .normal = 0xdf};
    decal.toBox = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 1, 0, 5, 1};
    frame.decals = {decal};
    frame.clusters = {.tilesX = 1, .tilesY = 1, .slices = 1, .ranges = {0, 0, 1, 0}, .indices = {0}};
    const auto kBent =
        drawnWith(**framer, **made, frame, kMeshes, &render_scene_gpu::RendererStatistics::decalsDrawn, kTextures);
    RAWFRAME_EXPECT(kBent.has_value());
    frame.decals[0].normal = 0;
    const auto kFlat =
        drawnWith(**framer, **made, frame, kMeshes, &render_scene_gpu::RendererStatistics::decalsDrawn, kTextures);
    RAWFRAME_EXPECT(kFlat.has_value());
    // A smooth white metal's face, the sun high behind the eye: its
    // highlight falls off the face, but where the decal lays a roughness
    // of one, it spreads there.
    frame.lights.toSun = {0, 0.7071F, 0.7071F};
    render_scene::MaterialBlob metal = render_scene::noMaterial();
    metal[3] = 1;
    metal[7] = 0.05F;
    frame.materials = {metal};
    frame.decals[0].roughness = 1;
    const auto kRough =
        drawnWith(**framer, **made, frame, kMeshes, &render_scene_gpu::RendererStatistics::decalsDrawn, kTextures);
    RAWFRAME_EXPECT(kRough.has_value());
    if (!kBent.has_value() || !kFlat.has_value() || !kRough.has_value()) {
        return;
    }
    // The same texel, under the bent normals and without them: turned
    // toward the sun, it takes twice the light.
    const auto kBentLit = at(*kBent, 25, kSide / 2);
    const auto kFlatLit = at(*kFlat, 25, kSide / 2);
    const auto kSpread = at(*kRough, 25, kSide / 2);
    const auto kSmooth = at(*kRough, 45, kSide / 2);
    std::printf("bent %d, flat %d; rough %d, smooth %d\n", kBentLit[1], kFlatLit[1], kSpread[1], kSmooth[1]);
    RAWFRAME_EXPECT(kFlatLit[1] + 20 < kBentLit[1] && kSmooth[1] + 20 < kSpread[1]);
}
