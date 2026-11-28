// Style classes over Maul UI's: a class's base look, the variants a
// node's states pick, the later class winning, the parts a node sets
// itself winning over every class, and a timed transition moving a fill
// between two frames' times, its spec gone with its class; and opacity
// multiplying down a subtree, moved by a class's transition.

#include "rawframe/test/test.h"
#include "rawframe/ui/tree.h"

#include <array>
#include <cmath>
#include <span>

using namespace rawframe;
using namespace rawframe::ui;

namespace {

/// The fill of `node`'s box as the tree draws it at `seconds`: red, then
/// green, nought to one.
std::array<float, 2> fillAt(Tree& ui, Node node, double seconds) {
    DrawList list;
    if (!ui.layOut(node, 100, 100, seconds).has_value() || !ui.draw(node, 1, list).has_value() || list.boxes.empty()) {
        return {-1, -1};
    }
    return {list.boxes[0].fill[0], list.boxes[0].fill[1]};
}

bool near(std::array<float, 2> fill, float red, float green) {
    return std::abs(fill[0] - red) < 0.02F && std::abs(fill[1] - green) < 0.02F;
}

} // namespace

RAWFRAME_TEST(AClassLooksAsItsNodesStatesSay) {
    auto tree = Tree::create(16);
    if (!tree.has_value()) {
        return;
    }
    Tree& ui = **tree;
    const Node kButton = *ui.add(1);
    RAWFRAME_EXPECT(ui.setLayout(kButton, {.width = pixels(40), .height = pixels(20)}).has_value());
    const Style kButtons = *ui.addStyle();
    const Style kDanger = *ui.addStyle();
    RAWFRAME_EXPECT(ui.setStyleLook(kButtons, Variant::Base, {.fill = 0xFF0000FF}, kEveryLookPart).has_value());
    RAWFRAME_EXPECT(ui.setStyleLook(kButtons, Variant::Hovered, {.fill = 0x00FF00FF}, kEveryLookPart).has_value());
    // The node's own look leaves its fill to its classes.
    RAWFRAME_EXPECT(ui.setLook(kButton, {.radius = 4}, static_cast<LookParts>(LookPart::Radius)).has_value());
    const std::array<Style, 1> kOne = {kButtons};
    RAWFRAME_EXPECT(ui.setClasses(kButton, kOne).has_value());
    RAWFRAME_EXPECT(near(fillAt(ui, kButton, 0), 1, 0));
    RAWFRAME_EXPECT(ui.setStates(kButton, {.hovered = true}).has_value());
    RAWFRAME_EXPECT(near(fillAt(ui, kButton, 0), 0, 1));
    // A later class wins where it sets the part; its own hovered look
    // unset, the earlier class's still shows.
    RAWFRAME_EXPECT(ui.setStyleLook(kDanger, Variant::Base, {.fill = 0x0000FFFF}, kEveryLookPart).has_value());
    const std::array<Style, 2> kTwo = {kButtons, kDanger};
    RAWFRAME_EXPECT(ui.setClasses(kButton, kTwo).has_value());
    RAWFRAME_EXPECT(ui.setStates(kButton, {}).has_value());
    RAWFRAME_EXPECT(near(fillAt(ui, kButton, 0), 0, 0));
    // The node's own fill wins over every class.
    RAWFRAME_EXPECT(ui.setLook(kButton, {.fill = 0xFFFF00FF}, static_cast<LookParts>(LookPart::Fill)).has_value());
    RAWFRAME_EXPECT(near(fillAt(ui, kButton, 0), 1, 1));
    // More than eight classes, refused.
    const std::array<Style, 9> kNine{};
    RAWFRAME_EXPECT(!ui.setClasses(kButton, kNine).has_value());
}

RAWFRAME_TEST(ATransitionMovesAPartOverTime) {
    auto tree = Tree::create(16);
    if (!tree.has_value()) {
        return;
    }
    Tree& ui = **tree;
    const Node kButton = *ui.add(1);
    RAWFRAME_EXPECT(ui.setLayout(kButton, {.width = pixels(40), .height = pixels(20)}).has_value());
    RAWFRAME_EXPECT(ui.setLook(kButton, {}, 0).has_value());
    const Style kButtons = *ui.addStyle();
    RAWFRAME_EXPECT(ui.setStyleLook(kButtons, Variant::Base, {.fill = 0xFF0000FF}, kEveryLookPart).has_value());
    RAWFRAME_EXPECT(ui.setStyleLook(kButtons, Variant::Hovered, {.fill = 0x00FF00FF}, kEveryLookPart).has_value());
    const Transition kSecond{.seconds = 1, .easing = Transition::Easing::Linear};
    RAWFRAME_EXPECT(
        ui.setStyleTransition(kButtons, Variant::Hovered, static_cast<LookParts>(LookPart::Fill), kSecond).has_value());
    RAWFRAME_EXPECT(
        ui.setStyleTransition(kButtons, Variant::Base, static_cast<LookParts>(LookPart::Fill), kSecond).has_value());
    const std::array<Style, 1> kOne = {kButtons};
    RAWFRAME_EXPECT(ui.setClasses(kButton, kOne).has_value());
    RAWFRAME_EXPECT(near(fillAt(ui, kButton, 10), 1, 0) && !ui.transitioning(kButton));
    RAWFRAME_EXPECT(ui.setStates(kButton, {.hovered = true}).has_value());
    RAWFRAME_EXPECT(near(fillAt(ui, kButton, 10), 1, 0));
    const std::array<float, 2> kHalfway = fillAt(ui, kButton, 10.5);
    RAWFRAME_EXPECT(kHalfway[0] > 0.05F && kHalfway[0] < 0.95F && kHalfway[1] > 0.05F && ui.transitioning(kButton));
    RAWFRAME_EXPECT(near(fillAt(ui, kButton, 11.5), 0, 1) && !ui.transitioning(kButton));
    // A time below nought, refused.
    RAWFRAME_EXPECT(!ui.setStyleTransition(kButtons, Variant::Base, kEveryLookPart, {.seconds = -1}).has_value());
    // A class's specs go with it: made and removed more times than the
    // context holds specs at once, as a sheet reloaded is.
    for (int again = 0; again < 100; ++again) {
        const Style kMade = *ui.addStyle();
        RAWFRAME_EXPECT(ui.setStyleTransition(kMade, Variant::Base, kEveryLookPart, kSecond).has_value());
        RAWFRAME_EXPECT(ui.removeStyle(kMade).has_value());
    }
}

RAWFRAME_TEST(OpacityMultipliesDownASubtree) {
    auto tree = Tree::create(16);
    if (!tree.has_value()) {
        return;
    }
    Tree& ui = **tree;
    const Node kPanel = *ui.add(1);
    const Node kCard = *ui.add(2);
    RAWFRAME_EXPECT(ui.attach(kPanel, kCard).has_value());
    RAWFRAME_EXPECT(ui.setLayout(kPanel, {.width = pixels(80), .height = pixels(40)}).has_value());
    RAWFRAME_EXPECT(ui.setLayout(kCard, {.width = pixels(40), .height = pixels(20)}).has_value());
    RAWFRAME_EXPECT(ui.setLook(kPanel, {.fill = 0xFFFFFFFF, .opacity = 0.5F}).has_value());
    RAWFRAME_EXPECT(ui.setLook(kCard, {.fill = 0xFFFFFFFF, .opacity = 0.5F}).has_value());
    // Each box's alpha, the panel's then the card's.
    const auto kAlphas = [&](double seconds) {
        DrawList list;
        if (!ui.layOut(kPanel, 100, 100, seconds).has_value() || !ui.draw(kPanel, 1, list).has_value()) {
            return std::array<float, 2>{-1, -1};
        }
        return std::array<float, 2>{list.boxes.empty() ? 0 : list.boxes[0].fill[3],
                                    list.boxes.size() < 2 ? 0 : list.boxes[1].fill[3]};
    };
    const std::array<float, 2> kHalved = kAlphas(0);
    RAWFRAME_EXPECT(std::abs(kHalved[0] - 0.5F) < 0.01F && std::abs(kHalved[1] - 0.25F) < 0.01F);
    // A subtree at nought draws nothing.
    RAWFRAME_EXPECT(ui.setLook(kPanel, {.fill = 0xFFFFFFFF, .opacity = 0}).has_value());
    DrawList hidden;
    RAWFRAME_EXPECT(ui.layOut(kPanel, 100, 100, 0).has_value() && ui.draw(kPanel, 1, hidden).has_value() &&
                    hidden.boxes.empty());
    // A class fades the panel out as it is hovered, over a second.
    RAWFRAME_EXPECT(ui.setLook(kPanel, {.fill = 0xFFFFFFFF}, static_cast<LookParts>(LookPart::Fill)).has_value());
    const Style kFades = *ui.addStyle();
    RAWFRAME_EXPECT(ui.setStyleLook(kFades, Variant::Hovered, {.opacity = 0}, static_cast<LookParts>(LookPart::Opacity))
                        .has_value());
    const Transition kSecond{.seconds = 1, .easing = Transition::Easing::Linear};
    for (const Variant kVariant : {Variant::Base, Variant::Hovered}) {
        RAWFRAME_EXPECT(
            ui.setStyleTransition(kFades, kVariant, static_cast<LookParts>(LookPart::Opacity), kSecond).has_value());
    }
    const std::array<Style, 1> kOne = {kFades};
    RAWFRAME_EXPECT(ui.setClasses(kPanel, kOne).has_value());
    // Given back to its class, the panel moves from nought to the base's
    // one, as any part does.
    const float kRising = kAlphas(5)[0];
    RAWFRAME_EXPECT(kRising < 0.01F && ui.transitioning(kPanel));
    RAWFRAME_EXPECT(std::abs(kAlphas(10)[0] - 1) < 0.01F && !ui.transitioning(kPanel));
    RAWFRAME_EXPECT(ui.setStates(kPanel, {.hovered = true}).has_value());
    RAWFRAME_EXPECT(std::abs(kAlphas(10)[0] - 1) < 0.01F);
    const std::array<float, 2> kHalfway = kAlphas(10.5);
    RAWFRAME_EXPECT(kHalfway[0] > 0.3F && kHalfway[0] < 0.7F && std::abs(kHalfway[1] - kHalfway[0] * 0.5F) < 0.01F &&
                    ui.transitioning(kPanel));
    RAWFRAME_EXPECT(kAlphas(11.5)[0] == 0 && !ui.transitioning(kPanel));
    // An opacity past nought to one, refused.
    RAWFRAME_EXPECT(!ui.setLook(kCard, {.opacity = 2}).has_value());
}
