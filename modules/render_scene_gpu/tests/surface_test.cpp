// A material shapes its model's surface on the device (D299 to D313) on
// lavapipe: its parameters, translucency blending over what is behind, its
// textures coloring and cutting it, and its packed, emission, and normal
// textures. Skips where no adapter answers, unless RAWFRAME_REQUIRE_GPU is
// set.

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
    RAWFRAME_EXPECT(kDark[0] == 0 && kUnlit[1] > kUnlit[0] + 60 && std::abs(kGlowing[0] - 120) <= 3 &&
                    kSmooth > kRough + 30);
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
            RAWFRAME_EXPECT((**framer).finish(kFrameWait).has_value());
            RAWFRAME_EXPECT(
                (**framer).make(kRecorders, {.width = kSide, .height = kSide, .readBack = true}).has_value());
        }
        RAWFRAME_EXPECT((**framer).finish(kFrameWait).has_value());
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
    // The texture, white, the dark cube bound for no sky's picture (D322),
    // and the plain grading table for none (D344), each uploaded once.
    RAWFRAME_EXPECT(asked > 0 && (**made).statistics().texturesUploaded == 4);
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
        RAWFRAME_EXPECT((**framer).finish(kFrameWait).has_value());
        RAWFRAME_EXPECT((**framer).make(kRecorders, {.width = kSide, .height = kSide, .readBack = true}).has_value());
    }
    RAWFRAME_EXPECT((**framer).finish(kFrameWait).has_value());
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
    RAWFRAME_EXPECT(std::abs(kGlowAbove - 120) <= 3 && kGlowBelow == 0 && kSkyAbove > 60 && kSkyBelow < 5);
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
