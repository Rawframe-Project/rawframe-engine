// A UI tree over Maul UI (D374): a row of two fixed boxes and a growing one
// laid out in their parent's padding box, a column's share of its parent,
// an automatic root fitting its content, keys kept, values out of range
// refused, and a removed subtree's nodes stale.

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
