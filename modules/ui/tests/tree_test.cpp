// A UI tree over Maul UI (D374): a row of two fixed boxes and a growing one
// laid out in their parent's padding box, a column's share of its parent,
// an automatic root fitting its content, an absolute node placed by its
// anchor point and a detached one a root again, keys kept, values out of
// range refused, a removed subtree's nodes stale, a tree's looks drawn as
// SPEC-0032's boxes in paint order, and points hitting the topmost node
// that takes part (D421): through containers, pass-through panels, and
// past a modal layer.

#include "rawframe/test/test.h"
#include "rawframe/ui/errors.h"
#include "rawframe/ui/tree.h"

#include <limits>

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

RAWFRAME_TEST(AShadowIsDrawnOutsideOrInsideItsBox) {
    auto tree = Tree::create(4);
    if (!tree.has_value()) {
        return;
    }
    Tree& ui = **tree;
    const Node kCard = *ui.add(1);
    RAWFRAME_EXPECT(ui.setLayout(kCard, {.width = pixels(100), .height = pixels(50)}).has_value());
    RAWFRAME_EXPECT(ui.setLook(kCard,
                               {.fill = 0xFFFFFFFF,
                                .radius = 4,
                                .outerShadow = {.color = 0x00000080, .x = 2, .y = 3, .blur = 6, .spread = 1},
                                .innerShadow = {.color = 0xFF0000FF, .blur = 2}})
                        .has_value());
    // A blur below nought is refused.
    RAWFRAME_EXPECT(!ui.setLook(kCard, {.outerShadow = {.color = 0x000000FF, .blur = -1}}).has_value());
    RAWFRAME_EXPECT(ui.layOut(kCard, 640, 360).has_value());
    DrawList list;
    RAWFRAME_EXPECT(ui.draw(kCard, 1, list).has_value());
    // The outer shadow under the box, the inner one over its fill.
    RAWFRAME_EXPECT(list.commands.size() == 3 && list.shadows.size() == 2 && list.boxes.size() == 1);
    if (list.commands.size() != 3 || list.shadows.size() != 2) {
        return;
    }
    RAWFRAME_EXPECT(list.commands[0].kind == DrawCommand::Kind::Shadow &&
                    list.commands[1].kind == DrawCommand::Kind::Box &&
                    list.commands[2].kind == DrawCommand::Kind::Shadow);
    const Shadow& kOuter = list.shadows[0];
    RAWFRAME_EXPECT(!kOuter.inset && placed(kOuter.rect, 0, 0, 100, 50) && kOuter.x == 2 && kOuter.y == 3 &&
                    kOuter.blur == 6 && kOuter.spread == 1 && kOuter.radii[0] == 4 &&
                    near(kOuter.color[3], 128.0F / 255.0F));
    RAWFRAME_EXPECT(list.shadows[1].inset && list.shadows[1].color == (std::array<float, 4>{1, 0, 0, 1}));
}

RAWFRAME_TEST(AGradientIsPaintedOverItsBoxsFill) {
    auto tree = Tree::create(4);
    if (!tree.has_value()) {
        return;
    }
    Tree& ui = **tree;
    const Node kBar = *ui.add(1);
    RAWFRAME_EXPECT(ui.setLayout(kBar, {.width = pixels(100), .height = pixels(20)}).has_value());
    RAWFRAME_EXPECT(ui.setLook(kBar,
                               {.fill = 0x000000FF,
                                .gradient = {.kind = GradientLook::Kind::Linear,
                                             .angle = 90,
                                             .colors = {0xFF0000FF, 0x0000FFFF},
                                             .positions = {0, 1},
                                             .stops = 2}})
                        .has_value());
    // One stop, or stops out of order, are no gradient.
    RAWFRAME_EXPECT(
        !ui.setLook(kBar, {.gradient = {.kind = GradientLook::Kind::Radial, .colors = {0xFF0000FF}, .stops = 1}})
             .has_value());
    RAWFRAME_EXPECT(ui.layOut(kBar, 640, 360).has_value());
    DrawList list;
    RAWFRAME_EXPECT(ui.draw(kBar, 1, list).has_value());
    RAWFRAME_EXPECT(list.boxes.size() == 1 && list.gradients.size() == 2 && list.skipped == 0);
    if (list.boxes.size() != 1 || list.gradients.size() != 2) {
        return;
    }
    const Gradient& kGradient = list.gradients[list.boxes[0].gradient];
    RAWFRAME_EXPECT(list.boxes[0].gradient == 1 && kGradient.kind == GradientLook::Kind::Linear &&
                    kGradient.angle == 90 && kGradient.stops == 2 && kGradient.positions[1] == 1 &&
                    kGradient.colors[0] == (std::array<float, 4>{1, 0, 0, 1}) &&
                    kGradient.colors[1] == (std::array<float, 4>{0, 0, 1, 1}));
}

RAWFRAME_TEST(APointHitsTheTopmostNodeThatTakesPart) {
    auto tree = Tree::create(16);
    if (!tree.has_value()) {
        return;
    }
    Tree& ui = **tree;
    // A screen, a panel on it, a button in the panel, and a dialog above
    // them all.
    const Node kScreen = *ui.add(1);
    const Node kPanel = *ui.add(2);
    const Node kButton = *ui.add(3);
    const Node kDialog = *ui.add(4);
    RAWFRAME_EXPECT(ui.attach(kScreen, kPanel).has_value());
    RAWFRAME_EXPECT(ui.attach(kPanel, kButton).has_value());
    RAWFRAME_EXPECT(ui.attach(kScreen, kDialog).has_value());
    RAWFRAME_EXPECT(
        ui.setLayout(kScreen, {.width = share(1), .height = share(1), .alignItems = Align::Start}).has_value());
    RAWFRAME_EXPECT(
        ui.setLayout(kPanel, {.width = pixels(200), .height = pixels(100), .padding = {10, 10, 10, 10}}).has_value());
    RAWFRAME_EXPECT(ui.setLayout(kButton, {.width = pixels(50), .height = pixels(20)}).has_value());
    RAWFRAME_EXPECT(ui.setLayout(kDialog,
                                 {.width = pixels(40),
                                  .height = pixels(40),
                                  .placement = {.absolute = true, .x = pixels(300), .y = pixels(0)}})
                        .has_value());
    // The screen lets points through to the world where it has nothing.
    RAWFRAME_EXPECT(ui.setInteraction(kScreen, {.hits = Interaction::Hits::Children, .passThrough = true}).has_value());
    RAWFRAME_EXPECT(ui.layOut(kScreen, 640, 480).has_value());

    const auto kKeyAt = [&](float x, float y) -> std::uint64_t {
        const auto kHit = ui.hit(kScreen, x, y);
        return kHit.has_value() && kHit->node.has_value() ? ui.keyOf(*kHit->node) : 0;
    };
    RAWFRAME_EXPECT(kKeyAt(15, 15) == 3);
    const auto kOnButton = ui.hit(kScreen, 15, 15);
    RAWFRAME_EXPECT(kOnButton.has_value() && kOnButton->x == 5 && kOnButton->y == 5 && !kOnButton->passThrough);
    RAWFRAME_EXPECT(kKeyAt(150, 50) == 2);
    RAWFRAME_EXPECT(kKeyAt(310, 10) == 4);
    // Nothing there: the point passes through.
    const auto kNowhere = ui.hit(kScreen, 600, 400);
    RAWFRAME_EXPECT(kNowhere.has_value() && !kNowhere->node.has_value() && kNowhere->passThrough);
    // A panel that passes what it leaves unused through.
    RAWFRAME_EXPECT(ui.setInteraction(kPanel, {.passThrough = true}).has_value());
    RAWFRAME_EXPECT(ui.layOut(kScreen, 640, 480).has_value());
    const auto kThrough = ui.hit(kScreen, 150, 50);
    RAWFRAME_EXPECT(kThrough.has_value() && kThrough->passThrough && kKeyAt(150, 50) == 2);
    // A button that takes no part: the panel under it is hit.
    RAWFRAME_EXPECT(ui.setInteraction(kButton, {.hits = Interaction::Hits::Nothing}).has_value());
    RAWFRAME_EXPECT(ui.layOut(kScreen, 640, 480).has_value());
    RAWFRAME_EXPECT(kKeyAt(15, 15) == 2);
    // A modal dialog: points that miss it reach nothing below.
    RAWFRAME_EXPECT(ui.setInteraction(kDialog, {.layer = Interaction::Layer::Modal}).has_value());
    RAWFRAME_EXPECT(ui.layOut(kScreen, 640, 480).has_value());
    const auto kBlocked = ui.hit(kScreen, 15, 15);
    RAWFRAME_EXPECT(kBlocked.has_value() && kKeyAt(15, 15) == 4 && !kBlocked->passThrough);
    RAWFRAME_EXPECT(!ui.hit(kScreen, std::numeric_limits<float>::quiet_NaN(), 0).has_value());
}
