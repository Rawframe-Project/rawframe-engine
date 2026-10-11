// Ranges (D621): a node made a range holds its value within its limits
// and on a step; a press on its track pages the value toward the point,
// a drag past the threshold moves it with the pointer, from where its
// thumb was grabbed, and what the pointer did is taken; a press elsewhere
// is not; a range refused where its numbers are wrong, and cleared.

#include "rawframe/test/test.h"
#include "rawframe/ui/tree.h"

#include <limits>
#include <memory>

using namespace rawframe;
using namespace rawframe::ui;

namespace {

struct Slider {
    std::unique_ptr<Tree> tree;
    Node root;
    Node track;
    Node thumb;
};

/// A track 200 wide and 20 high at the top of a root 200 by 100, its
/// thumb 20 wide placed by its value as an owner places it.
Slider slider() {
    Slider made{.tree = *Tree::create(16), .root = {}, .track = {}, .thumb = {}};
    Tree& ui = *made.tree;
    made.root = *ui.add(1);
    made.track = *ui.add(2);
    made.thumb = *ui.add(3);
    static_cast<void>(
        ui.setLayout(made.root, Layout{.width = pixels(200), .height = pixels(100), .direction = Direction::Column}));
    static_cast<void>(ui.setLayout(made.track, Layout{.width = pixels(200), .height = pixels(20), .shrink = 0}));
    static_cast<void>(ui.setInteraction(made.track, Interaction{.drags = true}));
    static_cast<void>(ui.attach(made.root, made.track));
    static_cast<void>(ui.attach(made.track, made.thumb));
    return made;
}

/// The thumb placed for `value` of 0 to 100, as its owner would.
void place(Slider& slider, float value) {
    static_cast<void>(slider.tree->setLayout(
        slider.thumb,
        Layout{.width = pixels(20),
               .height = pixels(20),
               .placement = Placement{.absolute = true, .x = pixels(value / 100 * 180), .y = {}}}));
    static_cast<void>(slider.tree->layOut(slider.root, 200, 100, 1.0));
}

bool point(Slider& slider, PointerEvent::Action action, float x, bool held) {
    return slider.tree->point(slider.root, PointerEvent{.action = action, .x = x, .y = 10, .held = held, .seconds = 1})
        .value_or(false);
}

} // namespace

RAWFRAME_TEST(APressPagesARangeAndADragMovesItFromItsThumb) {
    Slider made = slider();
    Tree& ui = *made.tree;
    RAWFRAME_EXPECT(ui.setRange(made.track, Range{.value = 50, .thumb = made.thumb}).has_value());
    place(made, 50);
    RAWFRAME_EXPECT(ui.rangeOf(made.track).has_value() && ui.rangeOf(made.track)->value == 50 &&
                    ui.rangeOf(made.track)->thumb == made.thumb && !ui.rangeOf(made.root).has_value());
    // A press on the track before the thumb pages back by 10.
    RAWFRAME_EXPECT(point(made, PointerEvent::Action::Press, 10, true));
    RAWFRAME_EXPECT(!point(made, PointerEvent::Action::Release, 10, false));
    RAWFRAME_EXPECT(ui.rangeOf(made.track)->value == 40);
    place(made, 40);
    // The thumb, from 72 to 92, grabbed at its middle and dragged 36 to the
    // right: 36 of the 180 the thumb travels is 20 more.
    static_cast<void>(point(made, PointerEvent::Action::Move, 82, false));
    RAWFRAME_EXPECT(point(made, PointerEvent::Action::Press, 82, true));
    RAWFRAME_EXPECT(point(made, PointerEvent::Action::Move, 100, true));
    RAWFRAME_EXPECT(point(made, PointerEvent::Action::Move, 118, true));
    RAWFRAME_EXPECT(point(made, PointerEvent::Action::Release, 118, false));
    RAWFRAME_EXPECT(ui.rangeOf(made.track)->value == 60);
    // Past the end, no further than the maximum.
    place(made, 60);
    RAWFRAME_EXPECT(point(made, PointerEvent::Action::Press, 118, true));
    RAWFRAME_EXPECT(point(made, PointerEvent::Action::Move, 400, true));
    RAWFRAME_EXPECT(point(made, PointerEvent::Action::Release, 400, false));
    RAWFRAME_EXPECT(ui.rangeOf(made.track)->value == 100);
    RAWFRAME_EXPECT(!point(made, PointerEvent::Action::Leave, 400, false));
    // A press below the track is no range's.
    const auto kBelow = [&](PointerEvent::Action action, bool held) {
        return ui.point(made.root, PointerEvent{.action = action, .x = 20, .y = 60, .held = held, .seconds = 1})
            .value_or(true);
    };
    RAWFRAME_EXPECT(!kBelow(PointerEvent::Action::Press, true) && !kBelow(PointerEvent::Action::Release, false));
    RAWFRAME_EXPECT(ui.rangeOf(made.track)->value == 100);
}

RAWFRAME_TEST(ARangeKeepsItsValueOnAStepAndIsRefusedWhereItIsWrong) {
    Slider made = slider();
    Tree& ui = *made.tree;
    // Within its limits and on a step from the minimum.
    RAWFRAME_EXPECT(ui.setRange(made.track, Range{.minimum = 1, .maximum = 5, .value = 9, .step = 2}).has_value());
    RAWFRAME_EXPECT(ui.rangeOf(made.track)->value == 5);
    RAWFRAME_EXPECT(ui.setRange(made.track, Range{.minimum = 1, .maximum = 5, .value = 2.2F, .step = 2}).has_value());
    RAWFRAME_EXPECT(ui.rangeOf(made.track)->value == 3 || ui.rangeOf(made.track)->value == 1);
    RAWFRAME_EXPECT(!ui.setRange(made.track, Range{.minimum = 5, .maximum = 1}).has_value());
    RAWFRAME_EXPECT(!ui.setRange(made.track, Range{.step = -1}).has_value());
    RAWFRAME_EXPECT(!ui.setRange(made.track, Range{.value = std::numeric_limits<float>::quiet_NaN()}).has_value());
    RAWFRAME_EXPECT(ui.clearRange(made.track).has_value() && !ui.rangeOf(made.track).has_value());
    RAWFRAME_EXPECT(ui.clearRange(made.track).has_value());
}
