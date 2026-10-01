// A UI tree over Maul UI (D374): a row of two fixed boxes and a growing one
// laid out in their parent's padding box, a column's share of its parent,
// an automatic root fitting its content, an absolute node placed by its
// anchor point and a detached one a root again, keys kept, values out of
// range refused, a removed subtree's nodes stale, and a tree's looks drawn as
// SPEC-0032's boxes in paint order.

#include "rawframe/test/test.h"
#include "rawframe/ui/errors.h"
#include "rawframe/ui/tree.h"

using namespace rawframe;
using namespace rawframe::ui;

namespace {

bool placed(const Rect& rect, float x, float y, float width, float height) {
    return rect.x == x && rect.y == y && rect.width == width && rect.height == height;
}

} // namespace

RAWFRAME_TEST(ARowLaysItsChildrenOutInItsPadding) {
    auto tree = Tree::create(16);
    RAWFRAME_EXPECT(tree.has_value());
    if (!tree.has_value()) {
        return;
    }
    Tree& ui = **tree;
    const Node kBar = *ui.add(1);
    const Node kLeft = *ui.add(2);
    const Node kFill = *ui.add(3);
    const Node kRight = *ui.add(4);
    for (const Node kChild : {kLeft, kFill, kRight}) {
        RAWFRAME_EXPECT(ui.attach(kBar, kChild).has_value());
    }
    RAWFRAME_EXPECT(
        ui.setLayout(kBar, {.width = share(1), .height = share(1), .gap = 10, .padding = {20, 20, 5, 5}}).has_value());
    RAWFRAME_EXPECT(ui.setLayout(kLeft, {.width = pixels(100)}).has_value());
    RAWFRAME_EXPECT(ui.setLayout(kFill, {.grow = 1}).has_value());
    RAWFRAME_EXPECT(
        ui.setLayout(kRight, {.width = pixels(50), .height = pixels(10), .alignSelf = Align::Center}).has_value());
    RAWFRAME_EXPECT(ui.layOut(kBar, 640, 60).has_value());
    // The root takes the whole space, its share of it; its children its
    // padding box less the gaps, stretched on the cross axis unless they
    // align themselves.
    RAWFRAME_EXPECT(placed(ui.rectOf(kBar), 0, 0, 640, 60));
    RAWFRAME_EXPECT(placed(ui.rectOf(kLeft), 20, 5, 100, 50));
    RAWFRAME_EXPECT(placed(ui.rectOf(kFill), 130, 5, 430, 50));
    RAWFRAME_EXPECT(placed(ui.rectOf(kRight), 570, 25, 50, 10));
    RAWFRAME_EXPECT(ui.keyOf(kFill) == 3);
}

RAWFRAME_TEST(AShareIsOfTheParentsContent) {
    auto tree = Tree::create(16);
    if (!tree.has_value()) {
        return;
    }
    Tree& ui = **tree;
    const Node kPanel = *ui.add(1);
    const Node kHalf = *ui.add(2);
    RAWFRAME_EXPECT(ui.attach(kPanel, kHalf).has_value());
    RAWFRAME_EXPECT(
        ui.setLayout(
              kPanel,
              {.width = share(1), .height = share(1), .direction = Direction::Column, .alignItems = Align::Start})
            .has_value());
    RAWFRAME_EXPECT(
        ui.setLayout(kHalf, {.width = share(0.5F), .height = {.automatic = false, .scale = 0.25F, .offset = 8}})
            .has_value());
    RAWFRAME_EXPECT(ui.layOut(kPanel, 400, 200).has_value());
    RAWFRAME_EXPECT(placed(ui.rectOf(kHalf), 0, 0, 200, 58));
}

RAWFRAME_TEST(AnAbsoluteNodeIsPlacedByItsAnchorPoint) {
    auto tree = Tree::create(16);
    if (!tree.has_value()) {
        return;
    }
    Tree& ui = **tree;
    const Node kView = *ui.add(1);
    const Node kFirst = *ui.add(2);
    const Node kBar = *ui.add(3);
    RAWFRAME_EXPECT(ui.attach(kView, kFirst).has_value());
    RAWFRAME_EXPECT(ui.attach(kView, kBar).has_value());
    RAWFRAME_EXPECT(ui.setLayout(kView, {.width = share(1), .height = share(1)}).has_value());
    RAWFRAME_EXPECT(ui.setLayout(kFirst, {.width = pixels(40), .height = pixels(40)}).has_value());
    // A bar centered along the bottom, 16 pixels above it: out of the row,
    // so the first child keeps its place.
    RAWFRAME_EXPECT(ui.setLayout(kBar,
                                 {.width = pixels(200),
                                  .height = pixels(20),
                                  .placement = {.absolute = true,
                                                .x = share(0.5F),
                                                .y = {.automatic = false, .scale = 1, .offset = -16},
                                                .anchorX = 0.5F,
                                                .anchorY = 1}})
                        .has_value());
    RAWFRAME_EXPECT(ui.layOut(kView, 640, 360).has_value());
    RAWFRAME_EXPECT(placed(ui.rectOf(kFirst), 0, 0, 40, 40));
    RAWFRAME_EXPECT(placed(ui.rectOf(kBar), 220, 324, 200, 20));
    // Detached, the bar is a root with its own layout; the view lays out
    // without it.
    RAWFRAME_EXPECT(ui.detach(kBar).has_value());
    RAWFRAME_EXPECT(ui.attach(kFirst, kBar).has_value());
    RAWFRAME_EXPECT(ui.layOut(kView, 640, 360).has_value());
    RAWFRAME_EXPECT(placed(ui.rectOf(kBar), -80, 4, 200, 20));
}

RAWFRAME_TEST(WhatATreeCannotTakeIsRefused) {
    auto tree = Tree::create(2);
    if (!tree.has_value()) {
        return;
    }
    Tree& ui = **tree;
    const Node kRoot = *ui.add(1);
    const Node kChild = *ui.add(2);
    // Past the limit.
    const auto kThird = ui.add(3);
    RAWFRAME_EXPECT(!kThird.has_value() && kThird.error().code() == code(UiError::Capacity));
    // A negative gap; a container aligning its children automatically.
    RAWFRAME_EXPECT(ui.setLayout(kRoot, {.gap = -1}).error().code() == code(UiError::Invalid));
    RAWFRAME_EXPECT(ui.setLayout(kRoot, {.alignItems = Align::Auto}).error().code() == code(UiError::Invalid));
    // A child with a parent cannot be attached again; removing the root
    // removes its subtree.
    RAWFRAME_EXPECT(ui.attach(kRoot, kChild).has_value());
    RAWFRAME_EXPECT(ui.attach(kRoot, kChild).error().code() == code(UiError::Invalid));
    RAWFRAME_EXPECT(ui.remove(kRoot).has_value());
    RAWFRAME_EXPECT(!ui.contains(kRoot) && !ui.contains(kChild) && ui.keyOf(kChild) == 0);
    RAWFRAME_EXPECT(ui.remove(kChild).error().code() == code(UiError::Stale));
    RAWFRAME_EXPECT(placed(ui.rectOf(kChild), 0, 0, 0, 0));
}

RAWFRAME_TEST(AnAutomaticRootFitsItsContent) {
    auto tree = Tree::create(4);
    if (!tree.has_value()) {
        return;
    }
    Tree& ui = **tree;
    const Node kRoot = *ui.add(1);
    const Node kBox = *ui.add(2);
    RAWFRAME_EXPECT(ui.attach(kRoot, kBox).has_value());
    RAWFRAME_EXPECT(ui.setLayout(kBox, {.width = pixels(30), .height = pixels(20)}).has_value());
    RAWFRAME_EXPECT(ui.layOut(kRoot, 640, 360).has_value());
    RAWFRAME_EXPECT(placed(ui.rectOf(kRoot), 0, 0, 30, 20));
}

namespace {

bool near(float value, float expected) {
    return value > expected - 1e-3F && value < expected + 1e-3F;
}

} // namespace

RAWFRAME_TEST(ALaidOutTreeDrawsItsBoxesInPaintOrder) {
    auto tree = Tree::create(8);
    if (!tree.has_value()) {
        return;
    }
    Tree& ui = **tree;
    const Node kPanel = *ui.add(1);
    const Node kBare = *ui.add(2);
    const Node kChip = *ui.add(3);
    RAWFRAME_EXPECT(ui.attach(kPanel, kBare).has_value() && ui.attach(kPanel, kChip).has_value());
    RAWFRAME_EXPECT(ui.setLayout(kPanel,
                                 {.width = pixels(200),
                                  .height = pixels(100),
                                  .alignItems = Align::Start,
                                  .padding = {10, 10, 10, 10},
                                  .border = {2, 2, 2, 2}})
                        .has_value());
    RAWFRAME_EXPECT(ui.setLayout(kBare, {.width = pixels(30), .height = pixels(30)}).has_value());
    RAWFRAME_EXPECT(ui.setLayout(kChip, {.width = pixels(40), .height = pixels(20)}).has_value());
    RAWFRAME_EXPECT(ui.setLook(kPanel, {.fill = 0xFF0000FF, .borderColor = 0xFFFFFFFF, .radius = 8}).has_value());
    RAWFRAME_EXPECT(ui.setLook(kChip, {.fill = 0x80808080}).has_value());
    RAWFRAME_EXPECT(ui.setLook(kChip, {.radius = -1}).error().code() == code(UiError::Invalid));
    RAWFRAME_EXPECT(ui.layOut(kPanel, 640, 360).has_value());
    DrawList list;
    RAWFRAME_EXPECT(ui.draw(kPanel, 1, list).has_value());
    // The panel, then its one child with a look; the bare one draws
    // nothing. Rects are the root's, the chip past the panel's border,
    // padding, and the bare box.
    RAWFRAME_EXPECT(list.boxes.size() == 2 && list.skipped == 0);
    if (list.boxes.size() == 2) {
        const Box& kPanelBox = list.boxes[0];
        RAWFRAME_EXPECT(placed(kPanelBox.rect, 0, 0, 200, 100) && kPanelBox.radii[0] == 8 && kPanelBox.radii[2] == 8);
        RAWFRAME_EXPECT(kPanelBox.fill == (std::array<float, 4>{1, 0, 0, 1}));
        RAWFRAME_EXPECT(kPanelBox.borderWidths == (std::array<float, 4>{2, 2, 2, 2}) &&
                        kPanelBox.borderColors[3] == (std::array<float, 4>{1, 1, 1, 1}));
        const Box& kChipBox = list.boxes[1];
        RAWFRAME_EXPECT(placed(kChipBox.rect, 42, 12, 40, 20));
        // Grey 128 in linear light, at half alpha, premultiplied.
        const float kAlpha = 128.0F / 255.0F;
        RAWFRAME_EXPECT(near(kChipBox.fill[3], kAlpha) && near(kChipBox.fill[0], 0.2158605F * kAlpha));
    }
}

RAWFRAME_TEST(AnImageIsDrawnOverItsNodesFillInPaintOrder) {
    auto tree = Tree::create(8);
    if (!tree.has_value()) {
        return;
    }
    Tree& ui = **tree;
    const Node kPanel = *ui.add(1);
    const Node kIcon = *ui.add(2);
    const Node kAfter = *ui.add(3);
    RAWFRAME_EXPECT(ui.attach(kPanel, kIcon).has_value() && ui.attach(kPanel, kAfter).has_value());
    RAWFRAME_EXPECT(
        ui.setLayout(kPanel, {.width = pixels(100), .height = pixels(40), .alignItems = Align::Start}).has_value());
    RAWFRAME_EXPECT(ui.setLayout(kIcon, {.width = pixels(32), .height = pixels(32)}).has_value());
    RAWFRAME_EXPECT(ui.setLayout(kAfter, {.width = pixels(20), .height = pixels(20)}).has_value());
    RAWFRAME_EXPECT(ui.setLook(kPanel, {.fill = 0x000000FF}).has_value());
    RAWFRAME_EXPECT(
        ui.setLook(kIcon, {.fill = 0x202020FF, .image = 0xab, .imageSlice = {4, 5, 6, 7}, .imageTint = 0xFF000080})
            .has_value());
    RAWFRAME_EXPECT(ui.setLook(kAfter, {.fill = 0xFFFFFFFF}).has_value());
    RAWFRAME_EXPECT(ui.layOut(kPanel, 640, 360).has_value());
    DrawList list;
    RAWFRAME_EXPECT(ui.draw(kPanel, 1, list).has_value());
    // The panel, the icon's fill, its image over it, then the next child.
    RAWFRAME_EXPECT(list.boxes.size() == 3 && list.images.size() == 1 && list.commands.size() == 4);
    if (list.commands.size() != 4 || list.images.size() != 1) {
        return;
    }
    RAWFRAME_EXPECT(list.commands[2].kind == DrawCommand::Kind::Image && list.commands[3].index == 2);
    const Image& kImage = list.images[0];
    RAWFRAME_EXPECT(kImage.image == 0xab && placed(kImage.rect, 0, 0, 32, 32) && placed(kImage.uv, 0, 0, 1, 1));
    RAWFRAME_EXPECT(kImage.slice == (std::array<float, 4>{4, 5, 6, 7}));
    // Red at half alpha, premultiplied.
    RAWFRAME_EXPECT(near(kImage.tint[3], 128.0F / 255.0F) && near(kImage.tint[0], 128.0F / 255.0F) &&
                    kImage.tint[1] == 0);
}
