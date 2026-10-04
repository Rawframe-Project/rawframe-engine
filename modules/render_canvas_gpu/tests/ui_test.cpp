// The UI's draw list on the device (SPEC-0032, D375) on lavapipe: a tree's
// rounded box drawn over the cleared picture, its fill inside its border,
// nothing past its rounded corner, its edges smoothed; a box half clear
// blends over what is behind it; a clipping parent keeps its child
// inside it; a child is kept inside every clip above it (D377); and an
// image is drawn stretched, or in nine slices, over its node's fill (D378);
// a run of glyphs with no atlas is counted and left out between boxes it
// does not join (D384), and with one, each glyph's coverage is drawn texel
// for pixel in its run's color, inside its clip (D398).
// Skips where no adapter answers, unless RAWFRAME_REQUIRE_GPU is set.

#include "rawframe/render/device.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_canvas_gpu/ui.h"
#include "rawframe/test/test.h"
#include "rawframe/ui/tree.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

using namespace rawframe;

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

std::array<int, 4> at(const std::vector<std::byte>& pixels, std::uint32_t x, std::uint32_t y) {
    std::array<int, 4> color{};
    for (std::size_t channel = 0; channel < 4; ++channel) {
        color[channel] = std::to_integer<int>(pixels[((std::size_t{y} * kSide + x) * 4) + channel]);
    }
    return color;
}

bool near(const std::array<int, 4>& color, std::array<int, 3> expected, int within = 3) {
    for (std::size_t channel = 0; channel < 3; ++channel) {
        if (color[channel] < expected[channel] - within || color[channel] > expected[channel] + within) {
            std::printf("(%d, %d, %d), not (%d, %d, %d)\n",
                        color[0],
                        color[1],
                        color[2],
                        expected[0],
                        expected[1],
                        expected[2]);
            return false;
        }
    }
    return true;
}

/// The list drawn over a cleared picture, read back once the box pipeline
/// is made.
std::optional<std::vector<std::byte>> drawn(render::Device& device,
                                            const ui::DrawList& list,
                                            render_canvas_gpu::ImageSource images = {},
                                            render_canvas_gpu::UiStatistics* statistics = nullptr) {
    auto framer = render::Framer::create(device);
    auto boxes = render_canvas_gpu::UiRenderer::create(device);
    RAWFRAME_EXPECT(framer.has_value() && boxes.has_value());
    if (!framer.has_value() || !boxes.has_value()) {
        return std::nullopt;
    }
    const std::array<render::FrameRecorder*, 1> kRecorders = {boxes->get()};
    for (int attempt = 0; attempt < 1000 && (**boxes).statistics().frames == 0; ++attempt) {
        (**boxes).prepare(&list, images);
        RAWFRAME_EXPECT(
            (*framer)->make(kRecorders, {.width = kSide, .height = kSide, .readBack = true}).value_or(false));
        RAWFRAME_EXPECT((*framer)->finish(5'000'000'000).has_value());
        if ((**boxes).statistics().frames != 0) {
            if (statistics != nullptr) {
                *statistics = (**boxes).statistics();
            }
            return (*framer)->pixels();
        }
        static_cast<void>((*framer)->pixels());
    }
    return std::nullopt;
}

} // namespace

RAWFRAME_TEST(ARoundedBoxIsDrawnWithItsBorder) {
    const auto kDevice = opened();
    if (kDevice == nullptr) {
        return;
    }
    auto tree = ui::Tree::create(4);
    RAWFRAME_EXPECT(tree.has_value());
    if (!tree.has_value()) {
        return;
    }
    ui::Tree& made = **tree;
    const ui::Node kPanel = *made.add(1);
    RAWFRAME_EXPECT(made.setLayout(kPanel, {.width = ui::pixels(48), .height = ui::pixels(32), .border = {4, 4, 4, 4}})
                        .has_value());
    RAWFRAME_EXPECT(made.setLook(kPanel, {.fill = 0xFF0000FF, .borderColor = 0xFFFFFFFF, .radius = 12}).has_value());
    RAWFRAME_EXPECT(made.layOut(kPanel, kSide, kSide).has_value());
    ui::DrawList list;
    RAWFRAME_EXPECT(made.draw(kPanel, 1, list).has_value() && list.boxes.size() == 1);
    const auto kPixels = drawn(*kDevice, list);
    RAWFRAME_EXPECT(kPixels.has_value());
    if (!kPixels.has_value()) {
        return;
    }
    // Red inside, white at its border, black past its rounded corner and
    // outside it; the corner's curve is smoothed between.
    RAWFRAME_EXPECT(near(at(*kPixels, 24, 16), {255, 0, 0}));
    RAWFRAME_EXPECT(near(at(*kPixels, 24, 1), {255, 255, 255}) && near(at(*kPixels, 1, 16), {255, 255, 255}));
    RAWFRAME_EXPECT(near(at(*kPixels, 0, 0), {0, 0, 0}) && near(at(*kPixels, 56, 40), {0, 0, 0}));
    const std::array<int, 4> kCurve = at(*kPixels, 3, 3);
    RAWFRAME_EXPECT(kCurve[0] > 20 && kCurve[0] < 250);
}

RAWFRAME_TEST(AClippingParentKeepsItsChildInside) {
    const auto kDevice = opened();
    if (kDevice == nullptr) {
        return;
    }
    auto tree = ui::Tree::create(4);
    if (!tree.has_value()) {
        return;
    }
    ui::Tree& made = **tree;
    const ui::Node kWindow = *made.add(1);
    const ui::Node kWide = *made.add(2);
    RAWFRAME_EXPECT(made.attach(kWindow, kWide).has_value());
    RAWFRAME_EXPECT(
        made.setLayout(kWindow, {.width = ui::pixels(32), .height = ui::pixels(32), .alignItems = ui::Align::Start})
            .has_value());
    // Wider than its parent, which shrinks it unless it may not.
    RAWFRAME_EXPECT(
        made.setLayout(kWide, {.width = ui::pixels(60), .height = ui::pixels(16), .shrink = 0}).has_value());
    RAWFRAME_EXPECT(made.setLook(kWindow, {.fill = 0x000080FF, .clip = true}).has_value());
    // Green at half alpha, over the blue.
    RAWFRAME_EXPECT(made.setLook(kWide, {.fill = 0x00FF0080}).has_value());
    RAWFRAME_EXPECT(made.layOut(kWindow, kSide, kSide).has_value());
    ui::DrawList list;
    RAWFRAME_EXPECT(made.draw(kWindow, 1, list).has_value() && list.boxes.size() == 2 && list.boxes[1].clip != 0);
    const auto kPixels = drawn(*kDevice, list);
    RAWFRAME_EXPECT(kPixels.has_value());
    if (!kPixels.has_value()) {
        return;
    }
    // Half green over the blue inside; past the window the child is kept
    // out, black; the window's lower half is its own blue.
    const std::array<int, 4> kInside = at(*kPixels, 16, 8);
    RAWFRAME_EXPECT(kInside[1] > 150 && kInside[2] > 40);
    RAWFRAME_EXPECT(near(at(*kPixels, 48, 8), {0, 0, 0}));
    RAWFRAME_EXPECT(near(at(*kPixels, 16, 24), {0, 0, 128}));
}

RAWFRAME_TEST(ANestedClipKeepsItsChildInsideEveryClipAbove) {
    const auto kDevice = opened();
    if (kDevice == nullptr) {
        return;
    }
    auto tree = ui::Tree::create(4);
    if (!tree.has_value()) {
        return;
    }
    ui::Tree& made = **tree;
    const ui::Node kOuter = *made.add(1);
    const ui::Node kInner = *made.add(2);
    const ui::Node kChild = *made.add(3);
    RAWFRAME_EXPECT(made.attach(kOuter, kInner).has_value());
    RAWFRAME_EXPECT(made.attach(kInner, kChild).has_value());
    // A blue window 40 square, in it a clear one 60 by 20 that clips too,
    // and in that a green one 60 by 30.
    RAWFRAME_EXPECT(
        made.setLayout(kOuter, {.width = ui::pixels(40), .height = ui::pixels(40), .alignItems = ui::Align::Start})
            .has_value());
    RAWFRAME_EXPECT(
        made.setLayout(kInner,
                       {.width = ui::pixels(60), .height = ui::pixels(20), .alignItems = ui::Align::Start, .shrink = 0})
            .has_value());
    RAWFRAME_EXPECT(
        made.setLayout(kChild, {.width = ui::pixels(60), .height = ui::pixels(30), .shrink = 0}).has_value());
    RAWFRAME_EXPECT(made.setLook(kOuter, {.fill = 0x000080FF, .clip = true}).has_value());
    RAWFRAME_EXPECT(made.setLook(kInner, {.clip = true}).has_value());
    RAWFRAME_EXPECT(made.setLook(kChild, {.fill = 0x00FF00FF}).has_value());
    RAWFRAME_EXPECT(made.layOut(kOuter, kSide, kSide).has_value());
    ui::DrawList list;
    RAWFRAME_EXPECT(made.draw(kOuter, 1, list).has_value() && list.boxes.size() == 2 && list.clips.size() == 3);
    const auto kPixels = drawn(*kDevice, list);
    RAWFRAME_EXPECT(kPixels.has_value());
    if (!kPixels.has_value()) {
        return;
    }
    // Green inside both; past the outer window though inside the inner
    // clip, black; below the inner clip though inside the green, blue.
    RAWFRAME_EXPECT(near(at(*kPixels, 20, 10), {0, 255, 0}));
    RAWFRAME_EXPECT(near(at(*kPixels, 50, 10), {0, 0, 0}));
    RAWFRAME_EXPECT(near(at(*kPixels, 20, 25), {0, 0, 128}));
}

namespace {

/// A texture `side` texels square, each texel `color(x, y)`, exact sRGB,
/// with a level below it at half its size, as a cooked texture has levels.
std::shared_ptr<const texture::Texture>
picture(std::uint32_t side, const std::function<std::array<std::uint8_t, 4>(std::uint32_t, std::uint32_t)>& color) {
    texture::Texture made{.format = texture::Format::Rgba8Srgb};
    texture::Level level{.width = side, .height = side, .bytes = {}};
    for (std::uint32_t y = 0; y < side; ++y) {
        for (std::uint32_t x = 0; x < side; ++x) {
            for (const std::uint8_t kChannel : color(x, y)) {
                level.bytes.push_back(std::byte{kChannel});
            }
        }
    }
    made.levels.push_back(std::move(level));
    made.levels.push_back(texture::Level{.width = side / 2, .height = side / 2, .bytes = {}});
    made.levels.back().bytes.resize(std::size_t{side / 2} * (side / 2) * 4, std::byte{0});
    return std::make_shared<const texture::Texture>(std::move(made));
}

} // namespace

RAWFRAME_TEST(AnImageIsDrawnStretchedOrInNineSlices) {
    const auto kDevice = opened();
    if (kDevice == nullptr) {
        return;
    }
    auto tree = ui::Tree::create(4);
    if (!tree.has_value()) {
        return;
    }
    ui::Tree& made = **tree;
    const ui::Node kRow = *made.add(1);
    const ui::Node kQuarters = *made.add(2);
    const ui::Node kFramed = *made.add(3);
    RAWFRAME_EXPECT(made.attach(kRow, kQuarters).has_value() && made.attach(kRow, kFramed).has_value());
    RAWFRAME_EXPECT(
        made.setLayout(kRow, {.width = ui::pixels(64), .height = ui::pixels(64), .alignItems = ui::Align::Start})
            .has_value());
    RAWFRAME_EXPECT(made.setLayout(kQuarters, {.width = ui::pixels(24), .height = ui::pixels(24)}).has_value());
    RAWFRAME_EXPECT(made.setLayout(kFramed, {.width = ui::pixels(40), .height = ui::pixels(40)}).has_value());
    // Four colored texels stretched over 24 pixels; a frame one texel wide
    // in nine slices over 40, its border kept a pixel wide.
    RAWFRAME_EXPECT(made.setLook(kQuarters, {.image = 1}).has_value());
    RAWFRAME_EXPECT(made.setLook(kFramed, {.fill = 0x000080FF, .image = 2, .imageSlice = {1, 1, 1, 1}}).has_value());
    RAWFRAME_EXPECT(made.layOut(kRow, kSide, kSide).has_value());
    ui::DrawList list;
    RAWFRAME_EXPECT(made.draw(kRow, 1, list).has_value() && list.images.size() == 2);
    const auto kQuartersPicture = picture(2, [](std::uint32_t x, std::uint32_t y) {
        return y == 0 ? (x == 0 ? std::array<std::uint8_t, 4>{255, 0, 0, 255}
                                : std::array<std::uint8_t, 4>{0, 255, 0, 255})
                      : (x == 0 ? std::array<std::uint8_t, 4>{0, 0, 255, 255}
                                : std::array<std::uint8_t, 4>{255, 255, 255, 255});
    });
    // A red frame around a clear middle: the fill shows through.
    const auto kFramePicture = picture(4, [](std::uint32_t x, std::uint32_t y) {
        const bool kEdge = x == 0 || y == 0 || x == 3 || y == 3;
        return kEdge ? std::array<std::uint8_t, 4>{255, 0, 0, 255} : std::array<std::uint8_t, 4>{0, 0, 0, 0};
    });
    const auto kPixels = drawn(*kDevice, list, [&](std::uint64_t id) {
        return id == 1 ? kQuartersPicture : (id == 2 ? kFramePicture : nullptr);
    });
    RAWFRAME_EXPECT(kPixels.has_value());
    if (!kPixels.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(near(at(*kPixels, 4, 4), {255, 0, 0}));
    RAWFRAME_EXPECT(near(at(*kPixels, 20, 4), {0, 255, 0}));
    RAWFRAME_EXPECT(near(at(*kPixels, 4, 20), {0, 0, 255}));
    RAWFRAME_EXPECT(near(at(*kPixels, 20, 20), {255, 255, 255}));
    // The frame at x 24 to 64: its edge a pixel of red, its middle the
    // fill's blue, however wide.
    RAWFRAME_EXPECT(near(at(*kPixels, 24, 20), {255, 0, 0}));
    RAWFRAME_EXPECT(near(at(*kPixels, 26, 20), {0, 0, 128}));
    RAWFRAME_EXPECT(near(at(*kPixels, 44, 1), {0, 0, 128}));
    RAWFRAME_EXPECT(near(at(*kPixels, 44, 0), {255, 0, 0}));
}

RAWFRAME_TEST(AShadowFadesOutsideItsBoxAndInsideAnInsetOne) {
    const auto kDevice = opened();
    if (kDevice == nullptr) {
        return;
    }
    auto tree = ui::Tree::create(4);
    if (!tree.has_value()) {
        return;
    }
    ui::Tree& made = **tree;
    const ui::Node kWindow = *made.add(1);
    const ui::Node kCard = *made.add(2);
    const ui::Node kWell = *made.add(3);
    RAWFRAME_EXPECT(made.attach(kWindow, kCard).has_value() && made.attach(kWindow, kWell).has_value());
    RAWFRAME_EXPECT(made.setLayout(kWindow,
                                   {.width = ui::pixels(64),
                                    .height = ui::pixels(64),
                                    .direction = ui::Direction::Column,
                                    .alignItems = ui::Align::Start,
                                    .gap = 12,
                                    .padding = {12, 12, 4, 4}})
                        .has_value());
    RAWFRAME_EXPECT(made.setLayout(kCard, {.width = ui::pixels(20), .height = ui::pixels(20)}).has_value());
    RAWFRAME_EXPECT(made.setLayout(kWell, {.width = ui::pixels(40), .height = ui::pixels(20)}).has_value());
    // A white card casting red over 8 pixels; a blue well darkened green
    // inside its edge over 4.
    RAWFRAME_EXPECT(
        made.setLook(kCard, {.fill = 0xFFFFFFFF, .outerShadow = {.color = 0xFF0000FF, .blur = 8}}).has_value());
    RAWFRAME_EXPECT(
        made.setLook(kWell, {.fill = 0x0000FFFF, .innerShadow = {.color = 0x00FF00FF, .blur = 4}}).has_value());
    RAWFRAME_EXPECT(made.layOut(kWindow, kSide, kSide).has_value());
    ui::DrawList list;
    RAWFRAME_EXPECT(made.draw(kWindow, 1, list).has_value() && list.shadows.size() == 2);
    const auto kPixels = drawn(*kDevice, list);
    RAWFRAME_EXPECT(kPixels.has_value());
    if (!kPixels.has_value()) {
        return;
    }
    // The card from (12, 4) to (32, 24): white inside, untouched by its
    // shadow; red just past its edge, fading with distance, none far off.
    RAWFRAME_EXPECT(near(at(*kPixels, 22, 14), {255, 255, 255}));
    const int kNear = at(*kPixels, 33, 14)[0];
    const int kFar = at(*kPixels, 39, 14)[0];
    RAWFRAME_EXPECT(kNear > 120 && kFar > 10 && kFar < kNear && at(*kPixels, 33, 14)[1] < 10);
    RAWFRAME_EXPECT(at(*kPixels, 52, 14)[0] < 8);
    // The well from (12, 36) to (52, 56): green over its blue at its edge,
    // its own blue at its middle.
    const std::array<int, 4> kEdge = at(*kPixels, 32, 36);
    RAWFRAME_EXPECT(kEdge[1] > 120 && kEdge[2] < 230);
    RAWFRAME_EXPECT(near(at(*kPixels, 32, 46), {0, 0, 255}, 12));
}

RAWFRAME_TEST(AGradientMovesThroughOklabOverItsFill) {
    const auto kDevice = opened();
    if (kDevice == nullptr) {
        return;
    }
    auto tree = ui::Tree::create(4);
    if (!tree.has_value()) {
        return;
    }
    ui::Tree& made = **tree;
    const ui::Node kWindow = *made.add(1);
    const ui::Node kBar = *made.add(2);
    const ui::Node kSpot = *made.add(3);
    RAWFRAME_EXPECT(made.attach(kWindow, kBar).has_value() && made.attach(kWindow, kSpot).has_value());
    RAWFRAME_EXPECT(made.setLayout(kWindow,
                                   {.width = ui::pixels(64),
                                    .height = ui::pixels(64),
                                    .direction = ui::Direction::Column,
                                    .alignItems = ui::Align::Start})
                        .has_value());
    RAWFRAME_EXPECT(made.setLayout(kBar, {.width = ui::pixels(64), .height = ui::pixels(16)}).has_value());
    RAWFRAME_EXPECT(made.setLayout(kSpot, {.width = ui::pixels(32), .height = ui::pixels(32)}).has_value());
    // Red to blue left to right; a radial white to clear over green.
    RAWFRAME_EXPECT(made.setLook(kBar,
                                 {.fill = 0x000000FF,
                                  .gradient = {.kind = ui::GradientLook::Kind::Linear,
                                               .angle = 90,
                                               .colors = {0xFF0000FF, 0x0000FFFF},
                                               .positions = {0, 1},
                                               .stops = 2}})
                        .has_value());
    RAWFRAME_EXPECT(made.setLook(kSpot,
                                 {.fill = 0x00FF00FF,
                                  .gradient = {.kind = ui::GradientLook::Kind::Radial,
                                               .colors = {0xFFFFFFFF, 0xFFFFFF00},
                                               .positions = {0, 1},
                                               .stops = 2}})
                        .has_value());
    RAWFRAME_EXPECT(made.layOut(kWindow, kSide, kSide).has_value());
    ui::DrawList list;
    RAWFRAME_EXPECT(made.draw(kWindow, 1, list).has_value() && list.gradients.size() == 3);
    const auto kPixels = drawn(*kDevice, list);
    RAWFRAME_EXPECT(kPixels.has_value());
    if (!kPixels.has_value()) {
        return;
    }
    // At each end, a pixel's center is a little way toward the other stop.
    RAWFRAME_EXPECT(near(at(*kPixels, 0, 8), {255, 0, 0}, 16));
    RAWFRAME_EXPECT(near(at(*kPixels, 63, 8), {0, 0, 255}, 16));
    // Halfway, Oklab's purple: green in it, as a mix in linear light has
    // none (that would be 188, 0, 186).
    RAWFRAME_EXPECT(near(at(*kPixels, 32, 8), {140, 83, 162}, 8));
    // The spot white at its center, its fill's green at its corner.
    RAWFRAME_EXPECT(near(at(*kPixels, 16, 32), {255, 255, 255}, 8));
    const std::array<int, 4> kCorner = at(*kPixels, 1, 17);
    RAWFRAME_EXPECT(kCorner[1] > 200 && kCorner[0] < 120);
}

RAWFRAME_TEST(AGlyphRunWaitsBetweenBoxesItDoesNotJoin) {
    const auto kDevice = opened();
    if (kDevice == nullptr) {
        return;
    }
    ui::DrawList list;
    list.clips.push_back({});
    list.gradients.push_back({});
    list.boxes.push_back({.rect = {.x = 0, .y = 0, .width = 32, .height = 64}, .fill = {1, 0, 0, 1}});
    list.boxes.push_back({.rect = {.x = 32, .y = 0, .width = 32, .height = 64}, .fill = {0, 0, 1, 1}});
    list.glyphRuns.push_back({.size = 16, .color = {1, 1, 1, 1}, .x = 8, .y = 40, .first = 0, .count = 1});
    list.glyphs.push_back({.id = 1});
    list.commands = {{.kind = ui::DrawCommand::Kind::Box, .index = 0},
                     {.kind = ui::DrawCommand::Kind::Glyphs, .index = 0},
                     {.kind = ui::DrawCommand::Kind::Box, .index = 1}};
    render_canvas_gpu::UiStatistics statistics;
    const auto kPixels = drawn(*kDevice, list, {}, &statistics);
    RAWFRAME_EXPECT(kPixels.has_value());
    if (!kPixels.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(near(at(*kPixels, 8, 36), {255, 0, 0}) && near(at(*kPixels, 48, 36), {0, 0, 255}));
    RAWFRAME_EXPECT(statistics.boxes == 2 && statistics.shadows == 0 && statistics.glyphRunsWaiting == 1);
}

RAWFRAME_TEST(AGlyphsCoverageIsDrawnFromTheAtlasInItsRunsColor) {
    const auto kDevice = opened();
    if (kDevice == nullptr) {
        return;
    }
    // An atlas of two glyphs: a block four texels square covered whole but
    // for one texel half covered, and a bar a texel wide.
    ui::GlyphAtlas atlas{.side = 16, .coverage = std::vector<std::uint8_t>(16 * 16, 0), .revision = 1};
    for (std::uint32_t y = 2; y < 6; ++y) {
        for (std::uint32_t x = 2; x < 6; ++x) {
            atlas.coverage[(y * 16) + x] = 255;
        }
    }
    atlas.coverage[(5 * 16) + 5] = 128;
    for (std::uint32_t y = 8; y < 16; ++y) {
        atlas.coverage[(y * 16) + 10] = 255;
    }
    ui::DrawList list;
    list.clips.push_back({});
    // The second run is clipped to the picture's left half.
    list.clips.push_back({.rect = {.x = 0, .y = 0, .width = 32, .height = 64}});
    list.gradients.push_back({});
    list.atlas = &atlas;
    list.glyphRuns.push_back({.size = 4, .color = {1, 1, 1, 1}, .x = 10, .y = 24, .first = 0, .count = 2});
    list.glyphRuns.push_back(
        {.size = 8, .color = {0, 0.5F, 0, 0.5F}, .x = 28, .y = 48, .first = 2, .count = 2, .clip = 1});
    list.glyphs.push_back({.id = 1,
                           .image = {.x = 10, .y = 20, .width = 4, .height = 4},
                           .atlas = {.x = 2, .y = 2, .width = 4, .height = 4}});
    // A space: no image.
    list.glyphs.push_back({.id = 2, .x = 4});
    list.glyphs.push_back({.id = 3,
                           .image = {.x = 30, .y = 40, .width = 1, .height = 8},
                           .atlas = {.x = 10, .y = 8, .width = 1, .height = 8}});
    list.glyphs.push_back({.id = 3,
                           .x = 6,
                           .image = {.x = 34, .y = 40, .width = 1, .height = 8},
                           .atlas = {.x = 10, .y = 8, .width = 1, .height = 8}});
    list.commands = {{.kind = ui::DrawCommand::Kind::Glyphs, .index = 0},
                     {.kind = ui::DrawCommand::Kind::Glyphs, .index = 1}};
    render_canvas_gpu::UiStatistics statistics;
    const auto kPixels = drawn(*kDevice, list, {}, &statistics);
    RAWFRAME_EXPECT(kPixels.has_value());
    if (!kPixels.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(near(at(*kPixels, 10, 20), {255, 255, 255}) && near(at(*kPixels, 12, 22), {255, 255, 255}));
    // Half covered, half the light: 188 in sRGB.
    RAWFRAME_EXPECT(near(at(*kPixels, 13, 23), {188, 188, 188}, 4));
    RAWFRAME_EXPECT(near(at(*kPixels, 9, 20), {0, 0, 0}) && near(at(*kPixels, 14, 22), {0, 0, 0}));
    // Half-transparent green inside the clip, nothing past it.
    RAWFRAME_EXPECT(near(at(*kPixels, 30, 44), {0, 188, 0}, 4) && near(at(*kPixels, 31, 44), {0, 0, 0}));
    RAWFRAME_EXPECT(near(at(*kPixels, 34, 44), {0, 0, 0}));
    RAWFRAME_EXPECT(statistics.glyphRuns == 2 && statistics.glyphs == 4 && statistics.glyphRunsWaiting == 0);
}
