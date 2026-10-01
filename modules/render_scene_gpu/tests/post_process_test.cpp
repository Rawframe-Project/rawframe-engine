// Post processes on lavapipe (D350): a fade after the tonemapper darkens
// the picture by its weight; the picture without red before the
// tonemapper, or after the temporal slot, comes out without red through
// the linear tonemapper; a red wash over the scene's output comes after
// FXAA; a texture laid over the picture shows its color; and one over the
// composed picture is drawn by the scene's second recorder, and left out
// and counted without it.

#include "fixture.h"
#include "rawframe/material/post_process.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_scene_gpu/renderer.h"
#include "rawframe/test/test.h"
#include "rawframe/texture/texture.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>

using namespace rawframe;
using namespace rawframe::scene_fixture;
using render_scene::SceneFrame;

namespace {

constexpr std::uint64_t kGreen = 0x9e3779b97f4a7c15ULL;

/// A post process of `made` at `weight`.
render_scene::ScenePostProcess processOf(const material::PostProcess& made, float weight = 1) {
    return {.insertion = made.insertion,
            .blob = material::blobOf(made),
            .texture = {.id = made.sampled.id, .filter = made.sampled.filter, .address = made.sampled.address},
            .weight = weight};
}

/// The view of a grey box before a sky, mapped linearly.
SceneFrame seen() {
    SceneFrame made = looking();
    made.draws = {box(4, 1, {0.5F, 0.5F, 0.5F, 1})};
    made.tonemapper = render_scene::Tonemapper::Linear;
    return made;
}

} // namespace

RAWFRAME_TEST(PostProcessesRunWhereTheyAreInserted) {
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
    texture::Texture green{.format = texture::Format::Rgba8Srgb};
    green.levels.push_back(
        {.width = 1, .height = 1, .bytes = {std::byte{0}, std::byte{0xFF}, std::byte{0}, std::byte{0xFF}}});
    const auto kGreenTexture = std::make_shared<const texture::Texture>(std::move(green));
    const render_scene_gpu::TextureSource kTextures = [&](std::uint64_t id) -> std::shared_ptr<const texture::Texture> {
        return id == kGreen ? kGreenTexture : nullptr;
    };
    const auto kDraw = [&](const SceneFrame& frame) {
        return drawnWith(
            **framer, **made, frame, kMeshes, &render_scene_gpu::RendererStatistics::postProcessesRun, kTextures);
    };
    const auto kPlain = drawn(**framer, **made, seen(), kMeshes);
    RAWFRAME_EXPECT(kPlain.has_value());
    if (!kPlain.has_value()) {
        return;
    }
    const auto kBox = at(*kPlain, 32, 32);
    const auto kSky = at(*kPlain, 32, 2);
    std::printf("plain: box %d %d %d, sky %d %d %d\n", kBox[0], kBox[1], kBox[2], kSky[0], kSky[1], kSky[2]);
    RAWFRAME_EXPECT(kBox[0] > 40 && kSky[0] > 40);

    // A fade to black after the tonemapper: whole, then halfway.
    const material::PostProcess kFade{.constant = {0, 0, 0, 1}};
    SceneFrame faded = seen();
    faded.postProcesses = {processOf(kFade)};
    const auto kBlack = kDraw(faded);
    faded.postProcesses = {processOf(kFade, 0.5F)};
    const auto kHalf = kDraw(faded);
    RAWFRAME_EXPECT(kBlack.has_value() && kHalf.has_value());
    if (kBlack.has_value() && kHalf.has_value()) {
        const auto kDark = at(*kBlack, 32, 32);
        const auto kDim = at(*kHalf, 32, 32);
        std::printf("faded: whole %d, half %d\n", kDark[0], kDim[0]);
        RAWFRAME_EXPECT(kDark[0] == 0 && kDark[1] == 0 && kDark[2] == 0);
        RAWFRAME_EXPECT(kDim[0] > 10 && kDim[0] < kBox[0] - 10);
    }

    // The picture without its red, before the tonemapper and after the
    // temporal slot: no red comes out.
    for (const material::Insertion kAt : {material::Insertion::BeforeTonemap, material::Insertion::AfterTemporal}) {
        material::PostProcess cyan{.insertion = kAt, .constant = {0, 0, 0, 1}, .scene = {0, 1, 1}};
        SceneFrame tinted = seen();
        tinted.postProcesses = {processOf(cyan)};
        const auto kTinted = kDraw(tinted);
        RAWFRAME_EXPECT(kTinted.has_value());
        if (kTinted.has_value()) {
            const auto kPixel = at(*kTinted, 32, 32);
            std::printf("cyan at %d: %d %d %d\n", static_cast<int>(kAt), kPixel[0], kPixel[1], kPixel[2]);
            RAWFRAME_EXPECT(kPixel[0] <= 1 && kPixel[1] > 40 && std::abs(kPixel[1] - kBox[1]) < 6);
        }
    }

    // A red wash over the scene's output, after FXAA: every pixel red.
    const material::PostProcess kRed{.insertion = material::Insertion::SceneOutput, .constant = {1, 0, 0, 1}};
    SceneFrame washed = seen();
    washed.fxaa = true;
    washed.temporal.enabled = false;
    washed.postProcesses = {processOf(kRed)};
    const auto kWashed = kDraw(washed);
    RAWFRAME_EXPECT(kWashed.has_value());
    if (kWashed.has_value()) {
        bool red = true;
        for (std::uint32_t y = 0; y < kSide; ++y) {
            for (std::uint32_t x = 0; x < kSide; ++x) {
                const auto kPixel = at(*kWashed, x, y);
                red = red && kPixel[0] == 255 && kPixel[1] == 0 && kPixel[2] == 0;
            }
        }
        RAWFRAME_EXPECT(red);
    }

    // A texture laid over the picture by its own alpha: its green.
    material::PostProcess laid{.texture = {1, 1, 1, 1}};
    laid.sampled.id = kGreen;
    SceneFrame overlaid = seen();
    overlaid.postProcesses = {processOf(laid)};
    const auto kOverlaid = kDraw(overlaid);
    RAWFRAME_EXPECT(kOverlaid.has_value());
    if (kOverlaid.has_value()) {
        const auto kPixel = at(*kOverlaid, 32, 32);
        std::printf("overlaid: %d %d %d\n", kPixel[0], kPixel[1], kPixel[2]);
        RAWFRAME_EXPECT(kPixel[0] == 0 && kPixel[1] == 255 && kPixel[2] == 0);
    }

    // Over the composed picture: left out, and counted, where nothing
    // records it; drawn after the scene's other recorders by its own
    // (D351), here whole black.
    const std::uint64_t kLeftOut = (*made)->statistics().postProcessesLeftOut;
    const material::PostProcess kComposed{.insertion = material::Insertion::FinalOutput, .constant = {0, 0, 0, 1}};
    SceneFrame composed = seen();
    composed.postProcesses = {processOf(kComposed), processOf(kFade, 0.5F)};
    const auto kAlone = kDraw(composed);
    RAWFRAME_EXPECT(kAlone.has_value() && (*made)->statistics().postProcessesLeftOut > kLeftOut);
    if (kAlone.has_value() && kHalf.has_value()) {
        RAWFRAME_EXPECT(at(*kAlone, 32, 32) == at(*kHalf, 32, 32));
    }
    const std::array<render::FrameRecorder*, 2> kBoth = {&**made, &(*made)->composed()};
    const std::uint64_t kRun = (*made)->statistics().postProcessesRun;
    const std::uint64_t kStillOut = (*made)->statistics().postProcessesLeftOut;
    for (int attempt = 0; attempt < 1000 && (*made)->statistics().postProcessesRun < kRun + 2; ++attempt) {
        (*made)->prepare(&composed, kMeshes, kTextures);
        RAWFRAME_EXPECT((*framer)->finish(5'000'000'000).has_value());
        RAWFRAME_EXPECT((*framer)->make(kBoth, {.width = kSide, .height = kSide, .readBack = true}).has_value());
    }
    RAWFRAME_EXPECT((*framer)->finish(5'000'000'000).has_value());
    const auto kComposedPixels = (*framer)->pixels();
    RAWFRAME_EXPECT(kComposedPixels.has_value() && (*made)->statistics().postProcessesLeftOut == kStillOut);
    if (kComposedPixels.has_value()) {
        const auto kPixel = at(*kComposedPixels, 32, 32);
        std::printf("composed: %d %d %d\n", kPixel[0], kPixel[1], kPixel[2]);
        RAWFRAME_EXPECT(kPixel[0] == 0 && kPixel[1] == 0 && kPixel[2] == 0);
    }
}
