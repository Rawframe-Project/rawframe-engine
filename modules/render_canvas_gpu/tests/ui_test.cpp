// The UI's draw list on the device (SPEC-0032, D375) on lavapipe: a tree's
// rounded box drawn over the cleared picture, its fill inside its border,
// nothing past its rounded corner, its edges smoothed; a box half clear
// blends over what is behind it; a clipping parent keeps its child
// inside it; and a child is kept inside every clip above it (D377).
// Skips where no adapter answers, unless RAWFRAME_REQUIRE_GPU is set.

#include "rawframe/render/device.h"
#include "rawframe/render/frame.h"
#include "rawframe/render_canvas_gpu/ui.h"
#include "rawframe/test/test.h"
#include "rawframe/ui/tree.h"

#include <array>
#include <cstdio>
#include <cstdlib>
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
std::optional<std::vector<std::byte>> drawn(render::Device& device, const ui::DrawList& list) {
    auto framer = render::Framer::create(device);
    auto boxes = render_canvas_gpu::UiRenderer::create(device);
    RAWFRAME_EXPECT(framer.has_value() && boxes.has_value());
    if (!framer.has_value() || !boxes.has_value()) {
        return std::nullopt;
    }
    const std::array<render::FrameRecorder*, 1> kRecorders = {boxes->get()};
    for (int attempt = 0; attempt < 1000 && (**boxes).statistics().frames == 0; ++attempt) {
        (**boxes).prepare(&list);
        RAWFRAME_EXPECT(
            (*framer)->make(kRecorders, {.width = kSide, .height = kSide, .readBack = true}).value_or(false));
        RAWFRAME_EXPECT((*framer)->finish(5'000'000'000).has_value());
        if ((**boxes).statistics().frames != 0) {
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
