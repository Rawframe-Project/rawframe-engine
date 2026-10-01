// The canvas's GPU half (D278) on lavapipe: a frame the queue stage could
// have built drawn into an offscreen target and read back. A 2 by 2 texture
// lands the right way up, each texel whole (exact textures are sampled
// nearest); a half-clear sprite drawn after it blends over it in linear
// light; a draw whose texture is not ready is left out; and a texture is
// uploaded once, and again only when a reload replaces it; and draws blend
// by their materials (D356): added, multiplied, a sprite with no texture
// drawn white, and an emission added where nothing covers; and particles
// and ribbons drawn over the sprites through the scene's device half, each
// by its material's blend (D357). Skips where no
// adapter answers, unless RAWFRAME_REQUIRE_GPU is set. Frames are made by
// `render`'s framer (D285), as the frame participant makes them.

#include "rawframe/particles/particles.h"
#include "rawframe/render/device.h"
#include "rawframe/render/errors.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_canvas_gpu/renderer.h"
#include "rawframe/test/test.h"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace rawframe;
using render_canvas::CanvasDraw;
using render_canvas::CanvasFrame;
using render_canvas::CanvasVertex;

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
constexpr std::uint64_t kQuarters = 7;
constexpr std::uint64_t kWhite = 8;
constexpr std::uint64_t kMissing = 9;

/// A quad from (`left`, `top`) to (`right`, `bottom`) in the canvas's clip
/// space, y up, the whole texture across it.
void quad(CanvasFrame& frame,
          std::uint64_t texture,
          float left,
          float top,
          float right,
          float bottom,
          std::uint32_t color,
          std::uint32_t material = 0) {
    const auto kFirst = static_cast<std::uint32_t>(frame.vertices.size());
    frame.vertices.push_back(CanvasVertex{.x = left, .y = top, .u = 0, .v = 0, .color = color});
    frame.vertices.push_back(CanvasVertex{.x = right, .y = top, .u = 1, .v = 0, .color = color});
    frame.vertices.push_back(CanvasVertex{.x = right, .y = bottom, .u = 1, .v = 1, .color = color});
    frame.vertices.push_back(CanvasVertex{.x = left, .y = bottom, .u = 0, .v = 1, .color = color});
    const auto kIndex = static_cast<std::uint32_t>(frame.indices.size());
    for (const std::uint32_t kCorner : {0U, 1U, 2U, 0U, 2U, 3U}) {
        frame.indices.push_back(kFirst + kCorner);
    }
    frame.draws.push_back(CanvasDraw{.texture = texture, .material = material, .firstIndex = kIndex, .indexCount = 6});
}

std::shared_ptr<const texture::Texture> image(std::uint32_t side, std::initializer_list<std::uint8_t> texels) {
    auto made = std::make_shared<texture::Texture>();
    made->format = texture::Format::Rgba8Srgb;
    made->levels.push_back(texture::Level{.width = side, .height = side, .bytes = {}});
    for (const std::uint8_t kByte : texels) {
        made->levels[0].bytes.push_back(static_cast<std::byte>(kByte));
    }
    return made;
}

bool near(const std::vector<std::byte>& pixels, std::uint32_t x, std::uint32_t y, std::array<int, 4> expected) {
    for (std::size_t channel = 0; channel < 4; ++channel) {
        const int kValue = std::to_integer<int>(pixels[((std::size_t{y} * kSide + x) * 4) + channel]);
        if (kValue < expected[channel] - 3 || kValue > expected[channel] + 3) {
            std::printf("pixel (%u, %u) channel %zu: %d, not %d\n", x, y, channel, kValue, expected[channel]);
            return false;
        }
    }
    return true;
}

} // namespace

RAWFRAME_TEST(TheCanvasDrawsItsFrameInOrder) {
    const auto kDevice = opened();
    if (kDevice == nullptr) {
        return;
    }
    auto made = render_canvas_gpu::CanvasRenderer::create(*kDevice);
    RAWFRAME_EXPECT(made.has_value());
    if (!made.has_value()) {
        return;
    }
    render_canvas_gpu::CanvasRenderer& renderer = **made;
    // Red, green; blue, white: rows top first.
    auto quarters = image(2, {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255});
    const auto kWhiteTexel = image(1, {255, 255, 255, 255});
    const render_canvas_gpu::TextureSource kTextures = [&](std::uint64_t id) {
        return id == kQuarters ? quarters : id == kWhite ? kWhiteTexel : nullptr;
    };
    CanvasFrame frame;
    // The top-left quarter of the view, then a half-clear blue square over
    // its middle, then a quad whose texture never comes.
    quad(frame, kQuarters, -1, 1, 0, 0, 0xFFFFFFFF);
    quad(frame, kWhite, -0.5F, 0.5F, 0.5F, -0.5F, 0x0000FF80);
    quad(frame, kMissing, 0.5F, 1, 1, 0.5F, 0xFFFFFFFF);
    auto framer = render::Framer::create(*kDevice);
    RAWFRAME_EXPECT(framer.has_value());
    if (!framer.has_value()) {
        return;
    }
    const std::array<render::FrameRecorder*, 1> kRecorders = {&renderer};
    const auto kDraw = [&] {
        // The pipeline is made as the device answers: a few frames at most.
        const std::uint64_t kBefore = renderer.statistics().frames;
        for (int attempt = 0; attempt < 1000 && renderer.statistics().frames == kBefore; ++attempt) {
            renderer.prepare(&frame, kTextures);
            RAWFRAME_EXPECT((*framer)->finish(10'000'000'000ULL).has_value());
            const auto kMade = (*framer)->make(kRecorders, {.width = kSide, .height = kSide, .readBack = true});
            RAWFRAME_EXPECT(kMade.has_value());
            if (!kMade.has_value()) {
                break;
            }
        }
        RAWFRAME_EXPECT((*framer)->finish(10'000'000'000ULL).has_value());
        auto pixels = (*framer)->pixels();
        RAWFRAME_EXPECT(pixels.has_value() && pixels->size() == std::size_t{kSide} * kSide * 4);
        return pixels.value_or(std::vector<std::byte>(std::size_t{kSide} * kSide * 4));
    };
    const std::vector<std::byte> kPixels = kDraw();
    // Each texel whole, the right way up, where the half-clear square is
    // not.
    RAWFRAME_EXPECT(near(kPixels, 4, 4, {255, 0, 0, 255}) && near(kPixels, 28, 4, {0, 255, 0, 255}) &&
                    near(kPixels, 4, 28, {0, 0, 255, 255}) && near(kPixels, 12, 12, {255, 0, 0, 255}));
    // Half blue over white, and over the black behind, blended in linear
    // light: sRGB 0.498 is 187, 0.502 is 188.
    RAWFRAME_EXPECT(near(kPixels, 28, 28, {187, 187, 255, 255}) && near(kPixels, 40, 40, {0, 0, 188, 255}));
    // The missing texture drew nothing; the rest is the clear color.
    RAWFRAME_EXPECT(near(kPixels, 56, 4, {0, 0, 0, 255}) && near(kPixels, 60, 60, {0, 0, 0, 255}));
    // Two textures uploaded, and the white a sprite with no texture
    // samples (D356).
    RAWFRAME_EXPECT(renderer.statistics().draws == 2 && renderer.statistics().drawsLeftOut == 1 &&
                    renderer.statistics().texturesUploaded == 3);

    // The same textures again are not uploaded again; a reload's new one is.
    static_cast<void>(kDraw());
    RAWFRAME_EXPECT(renderer.statistics().texturesUploaded == 3 && renderer.statistics().texturesReplaced == 0);
    quarters = image(2, {0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255});
    const std::vector<std::byte> kReloaded = kDraw();
    RAWFRAME_EXPECT(renderer.statistics().texturesUploaded == 4 && renderer.statistics().texturesReplaced == 1);
    RAWFRAME_EXPECT(near(kReloaded, 4, 4, {0, 255, 0, 255}));
}

RAWFRAME_TEST(ACanvasWithNothingQueuedDrawsNothing) {
    const auto kDevice = opened();
    if (kDevice == nullptr) {
        return;
    }
    auto made = render_canvas_gpu::CanvasRenderer::create(*kDevice);
    auto framer = render::Framer::create(*kDevice);
    RAWFRAME_EXPECT(made.has_value() && framer.has_value());
    if (!made.has_value() || !framer.has_value()) {
        return;
    }
    (*made)->prepare(nullptr, {});
    const std::array<render::FrameRecorder*, 1> kRecorders = {made->get()};
    RAWFRAME_EXPECT((*framer)->make(kRecorders, {.width = 8, .height = 8, .readBack = true}).value_or(false));
    RAWFRAME_EXPECT((*framer)->finish(10'000'000'000ULL).has_value());
    const auto kPixels = (*framer)->pixels();
    RAWFRAME_EXPECT(kPixels.has_value() && (*made)->statistics().frames == 0);
}

RAWFRAME_TEST(TheCanvasDrawsByItsMaterials) {
    const auto kDevice = opened();
    if (kDevice == nullptr) {
        return;
    }
    auto made = render_canvas_gpu::CanvasRenderer::create(*kDevice);
    auto framer = render::Framer::create(*kDevice);
    RAWFRAME_EXPECT(made.has_value() && framer.has_value());
    if (!made.has_value() || !framer.has_value()) {
        return;
    }
    const auto kWhiteTexel = image(1, {255, 255, 255, 255});
    const render_canvas_gpu::TextureSource kTextures = [&](std::uint64_t id) {
        return id == kWhite ? kWhiteTexel : nullptr;
    };
    // None; blue added; red multiplied; green; and a red glow over nothing.
    const std::array<material::CanvasMaterial, 5> kMaterials = {
        material::CanvasMaterial{.shading = material::Shading::Unlit},
        material::CanvasMaterial{.blend = material::CanvasBlend::Additive, .color = {0, 0, 1, 1}},
        material::CanvasMaterial{.blend = material::CanvasBlend::Multiply, .color = {1, 0, 0, 1}},
        material::CanvasMaterial{.color = {0, 1, 0, 1}},
        material::CanvasMaterial{.color = {0, 0, 0, 0}, .emission = {0.5F, 0, 0}}};
    CanvasFrame frame;
    frame.materials = kMaterials;
    // Grey over the view, then a quadrant of each.
    quad(frame, kWhite, -1, 1, 1, -1, 0x808080FF);
    quad(frame, kWhite, -1, 1, 0, 0, 0xFFFFFFFF, 1);
    quad(frame, kWhite, 0, 1, 1, 0, 0xFFFFFFFF, 2);
    quad(frame, 0, -1, 0, 0, -1, 0xFFFFFFFF, 3);
    quad(frame, kWhite, 0, 0, 1, -1, 0xFFFFFFFF, 4);
    const std::array<render::FrameRecorder*, 1> kRecorders = {&**made};
    for (int attempt = 0; attempt < 1000 && (*made)->statistics().frames == 0; ++attempt) {
        (*made)->prepare(&frame, kTextures);
        RAWFRAME_EXPECT((*framer)->finish(10'000'000'000ULL).has_value());
        RAWFRAME_EXPECT((*framer)->make(kRecorders, {.width = kSide, .height = kSide, .readBack = true}).has_value());
    }
    RAWFRAME_EXPECT((*framer)->finish(10'000'000'000ULL).has_value());
    const auto kPixels = (*framer)->pixels();
    RAWFRAME_EXPECT(kPixels.has_value() && (*made)->statistics().draws == 5);
    if (kPixels.has_value()) {
        // Grey is 0.216 in linear light: 0.716 with the glow is 218.
        RAWFRAME_EXPECT(near(*kPixels, 16, 16, {128, 128, 255, 255}) && near(*kPixels, 48, 16, {128, 0, 0, 255}) &&
                        near(*kPixels, 16, 48, {0, 255, 0, 255}) && near(*kPixels, 48, 48, {218, 128, 128, 255}));
    }
}

RAWFRAME_TEST(TheCanvasDrawsParticlesAndRibbons) {
    const auto kDevice = opened();
    if (kDevice == nullptr) {
        return;
    }
    auto made = render_canvas_gpu::CanvasRenderer::create(*kDevice);
    auto framer = render::Framer::create(*kDevice);
    RAWFRAME_EXPECT(made.has_value() && framer.has_value());
    if (!made.has_value() || !framer.has_value()) {
        return;
    }
    // None; and blue, added.
    const std::array<material::CanvasMaterial, 2> kMaterials = {
        material::CanvasMaterial{.shading = material::Shading::Unlit},
        material::CanvasMaterial{.blend = material::CanvasBlend::Additive, .color = {0, 0, 1, 1}}};
    CanvasFrame frame;
    frame.materials = kMaterials;
    // Sixteen meters across: four pixels a meter.
    frame.extent = {8, 8};
    // White particles four meters wide, standing still at the top left
    // quarter's middle; and a blue ribbon two meters wide along the bottom.
    frame.particles.clock = 1;
    frame.particles.emitters.push_back(particles::EmitterDraw{.key = 1,
                                                              .anchor = {-4, 4, 0},
                                                              .lifetime = 10,
                                                              .sizeStart = 4,
                                                              .sizeEnd = 4,
                                                              .capacity = 4,
                                                              .spawned = 4,
                                                              .seed = 7,
                                                              .ring = 1});
    frame.particles.ribbonPoints = {particles::RibbonPoint{.place = {-4, -4, 0}, .width = 2},
                                    particles::RibbonPoint{.place = {4, -4, 0}, .width = 2, .along = 1}};
    frame.particles.ribbons = {particles::Ribbon{.material = 1, .first = 0, .count = 2}};
    const std::array<render::FrameRecorder*, 1> kRecorders = {&**made};
    // The particles' pipelines are asked for by the first frame with any.
    for (int attempt = 0; attempt < 1000 && (*made)->statistics().ribbonsDrawn == 0; ++attempt) {
        (*made)->prepare(&frame, {});
        RAWFRAME_EXPECT((*framer)->finish(10'000'000'000ULL).has_value());
        RAWFRAME_EXPECT((*framer)->make(kRecorders, {.width = kSide, .height = kSide, .readBack = true}).has_value());
    }
    RAWFRAME_EXPECT((*framer)->finish(10'000'000'000ULL).has_value());
    const auto kPixels = (*framer)->pixels();
    const render_canvas_gpu::RendererStatistics& kCounted = (*made)->statistics();
    RAWFRAME_EXPECT(kPixels.has_value() && kCounted.emittersDrawn >= 1 && kCounted.ribbonsDrawn >= 1 &&
                    kCounted.particlesSpawned >= 4 && kCounted.emittersLeftOut == 0);
    if (kPixels.has_value()) {
        // White at the particles' middle, soft toward their edge; blue
        // along the ribbon; the clear color elsewhere.
        RAWFRAME_EXPECT(near(*kPixels, 16, 16, {255, 255, 255, 255}) && near(*kPixels, 32, 48, {0, 0, 255, 255}));
        RAWFRAME_EXPECT(near(*kPixels, 48, 16, {0, 0, 0, 255}) && near(*kPixels, 32, 32, {0, 0, 0, 255}));
    }
}
