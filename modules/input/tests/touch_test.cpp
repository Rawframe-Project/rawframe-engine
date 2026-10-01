// A touch screen's virtual controls (D387): a touch that begins in a half
// holds its button and tilts its stick by its way from where it began, a
// full tilt at the radius; a second touch in a held half takes nothing; the
// stick rests and the button lets go when the touch ends; `pointer` follows
// the newest touch; and until the window's width is told, every touch is
// the left half's.

#include "rawframe/input/feed.h"
#include "rawframe/input/mapper.h"
#include "rawframe/input/touch.h"
#include "rawframe/test/test.h"

#include <cmath>
#include <string_view>

using namespace rawframe;
using namespace rawframe::input;

namespace {

constexpr DeviceId kScreen{7};
constexpr PlayerSlot kPlayer{0};
constexpr std::size_t kMove = 0;
constexpr std::size_t kFire = 1;
constexpr std::size_t kAim = 2;
constexpr std::size_t kJump = 3;

Binding single(std::string_view name) {
    Binding binding;
    binding.device = DeviceClass::Touch;
    binding.controls[0] = *controlNamed(DeviceClass::Touch, name);
    return binding;
}

ActionSet actionSet() {
    ActionSet set;
    Binding stick = single("stick_left");
    stick.deadzoneLower = 0;
    set.actions.push_back(Action{.id = 1, .name = "move", .type = ValueType::Axis2D, .bindings = {stick}});
    set.actions.push_back(Action{.id = 2, .name = "fire", .type = ValueType::Bool, .bindings = {single("right")}});
    set.actions.push_back(Action{.id = 3, .name = "aim", .type = ValueType::Axis2D, .bindings = {single("pointer")}});
    set.actions.push_back(Action{.id = 4, .name = "jump", .type = ValueType::Bool, .bindings = {single("left")}});
    set.contexts.push_back(Context{.id = 10, .name = "play", .actions = {kMove, kFire, kAim, kJump}});
    return set;
}

struct Rig {
    Feed feed;
    TouchControls touch{feed, kScreen};
    std::unique_ptr<Mapper> mapper = *Mapper::create(actionSet(), {.players = 1});

    Rig() {
        RAWFRAME_EXPECT(mapper->activate(kPlayer, 0).has_value());
    }

    const ActionState& now(std::size_t action) {
        feed.deliver(*mapper, kPlayer);
        mapper->update();
        return mapper->current(kPlayer, action);
    }
};

bool near(float value, float expected) {
    return std::abs(value - expected) < 1e-5F;
}

} // namespace

RAWFRAME_TEST(ATouchHoldsItsHalfAndTiltsItsStick) {
    Rig rig;
    // Nothing is connected until a touch begins.
    rig.touch.move(1, 10, 10);
    RAWFRAME_EXPECT(rig.feed.waiting() == 0);
    rig.touch.resize(800);
    rig.touch.down(1, 100, 300);
    RAWFRAME_EXPECT(rig.now(kJump).on && near(rig.now(kMove).x, 0));
    // Half the radius right, then far past it up and right: a full tilt.
    rig.touch.move(1, 132, 300);
    RAWFRAME_EXPECT(near(rig.now(kMove).x, 0.5F) && near(rig.now(kMove).y, 0));
    rig.touch.move(1, 100 + 300, 300 - 400);
    const ActionState kFull = rig.now(kMove);
    RAWFRAME_EXPECT(near(kFull.x, 0.6F) && near(kFull.y, 0.8F));

    // The right half is its own; the pointer follows the newest touch.
    rig.touch.down(2, 600, 200);
    RAWFRAME_EXPECT(rig.now(kFire).on && near(rig.now(kAim).x, 600) && near(rig.now(kAim).y, 200));
    // A second touch in the held left half takes nothing from the first.
    rig.touch.down(3, 150, 100);
    RAWFRAME_EXPECT(near(rig.now(kMove).x, 0.6F) && near(rig.now(kAim).x, 150));

    // Ended, the stick rests and the button lets go; the touch that began
    // while it was held does not take it over.
    rig.touch.up(1);
    RAWFRAME_EXPECT(!rig.now(kJump).on && near(rig.now(kMove).x, 0) && near(rig.now(kMove).y, 0));
    rig.touch.move(3, 200, 100);
    RAWFRAME_EXPECT(near(rig.now(kMove).x, 0) && rig.now(kFire).on);
    rig.touch.up(2);
    RAWFRAME_EXPECT(!rig.now(kFire).on);
    // Forgotten touches end nothing more.
    rig.touch.down(4, 700, 100);
    rig.touch.forget();
    rig.touch.up(4);
    RAWFRAME_EXPECT(rig.now(kFire).on);
}

RAWFRAME_TEST(UntilTheWidthIsToldEveryTouchIsTheLeftHalfs) {
    Rig rig;
    rig.touch.down(1, 700, 100);
    RAWFRAME_EXPECT(rig.now(kJump).on && !rig.now(kFire).on);
    // A width that is not one is not taken.
    rig.touch.resize(-1);
    rig.touch.resize(std::nanf(""));
    rig.touch.down(2, 900, 100);
    RAWFRAME_EXPECT(!rig.now(kFire).on);
    RAWFRAME_EXPECT(nameOf(DeviceClass::Touch) == "touch" && deviceClassNamed("touch") == DeviceClass::Touch);
    RAWFRAME_EXPECT(positional(*controlNamed(DeviceClass::Touch, "pointer")) &&
                    shapeOf(*controlNamed(DeviceClass::Touch, "stick_right")) == ControlShape::Axis2 &&
                    shapeOf(*controlNamed(DeviceClass::Touch, "right")) == ControlShape::Digital &&
                    !relative(*controlNamed(DeviceClass::Touch, "stick_left")));
}
