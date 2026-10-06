// The engine's navigation actions (D430): added once to a game's set, a
// context above the game's that, enabled, claims its controls from the
// game, and gated with the keyboard while a field takes text.

#include "rawframe/input/mapper.h"
#include "rawframe/input/navigation.h"
#include "rawframe/test/test.h"

#include <string_view>

using namespace rawframe;
using namespace rawframe::input;

namespace {

constexpr DeviceId kKeyboard{1};
constexpr DeviceId kPad{2};
constexpr PlayerSlot kFirst{0};

Control key(std::string_view name) {
    return *controlNamed(DeviceClass::Keyboard, name);
}

Control pad(std::string_view name) {
    return *controlNamed(DeviceClass::Gamepad, name);
}

/// A game that builds with space and the south face button.
ActionSet game() {
    ActionSet set;
    Action build{.id = 1, .name = "build", .type = ValueType::Bool};
    for (const Control kControl : {key("space"), pad("face_south")}) {
        Binding binding;
        binding.device = kControl.device;
        binding.controls[0] = kControl;
        build.bindings.push_back(binding);
    }
    set.actions.push_back(build);
    set.contexts.push_back(Context{.id = 1, .name = "play", .priority = 0, .actions = {0}});
    return set;
}

} // namespace

RAWFRAME_TEST(NavigationIsAddedOnceAboveTheGame) {
    ActionSet set = game();
    const auto kNavigation = addNavigation(set);
    RAWFRAME_EXPECT(kNavigation.has_value() && set.actions.size() == 1 + kNavigationActions &&
                    set.contexts.size() == 2);
    if (!kNavigation.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(set.contexts[kNavigation->context].priority > set.contexts[0].priority &&
                    set.contexts[kNavigation->context].textEditGated);
    RAWFRAME_EXPECT(set.actions[kNavigation->actions[static_cast<std::size_t>(NavigationAction::Activate)]].name ==
                    "rawframe.ui.activate");
    // A second time, refused.
    RAWFRAME_EXPECT(!addNavigation(set).has_value() && set.actions.size() == 1 + kNavigationActions);
}

RAWFRAME_TEST(NavigationClaimsItsControlsWhileEnabled) {
    ActionSet set = game();
    const Navigation kNavigation = *addNavigation(set);
    auto mapper = *Mapper::create(set, {.players = 1});
    RAWFRAME_EXPECT(mapper->pair(kKeyboard, DeviceClass::Keyboard, kFirst).has_value());
    RAWFRAME_EXPECT(mapper->pair(kPad, DeviceClass::Gamepad, kFirst).has_value());
    RAWFRAME_EXPECT(mapper->activate(kFirst, 0).has_value());
    RAWFRAME_EXPECT(mapper->activate(kFirst, kNavigation.context).has_value());
    mapper->setEnabled(kFirst, kNavigation.context, false);
    const auto kAction = [&kNavigation](NavigationAction action) {
        return kNavigation.actions[static_cast<std::size_t>(action)];
    };
    std::uint64_t tick = 0;
    // One control pressed and let go over a tick: what was pressed.
    const auto kTap = [&](DeviceId device, Control control, std::size_t action) {
        mapper->submit(ControlEvent{.device = device, .control = control, .x = 1});
        mapper->commit(++tick);
        const bool kPressed = mapper->pressedThisTick(kFirst, action);
        mapper->submit(ControlEvent{.device = device, .control = control, .x = 0});
        mapper->commit(++tick);
        return kPressed;
    };
    // Disabled, space and the south button build.
    RAWFRAME_EXPECT(kTap(kKeyboard, key("space"), 0) && kTap(kPad, pad("face_south"), 0));
    RAWFRAME_EXPECT(!kTap(kKeyboard, key("arrow_up"), kAction(NavigationAction::Up)));
    // Enabled, they activate and build nothing.
    mapper->setEnabled(kFirst, kNavigation.context, true);
    RAWFRAME_EXPECT(kTap(kKeyboard, key("space"), kAction(NavigationAction::Activate)));
    RAWFRAME_EXPECT(!kTap(kPad, pad("face_south"), 0));
    RAWFRAME_EXPECT(kTap(kKeyboard, key("arrow_up"), kAction(NavigationAction::Up)) &&
                    kTap(kPad, pad("dpad_right"), kAction(NavigationAction::Right)) &&
                    kTap(kPad, pad("face_east"), kAction(NavigationAction::Dismiss)) &&
                    kTap(kKeyboard, key("tab"), kAction(NavigationAction::Next)));
    // Shift and Tab go back, not on.
    mapper->submit(ControlEvent{.device = kKeyboard, .control = key("shift_left"), .x = 1});
    mapper->submit(ControlEvent{.device = kKeyboard, .control = key("tab"), .x = 1});
    mapper->commit(++tick);
    RAWFRAME_EXPECT(mapper->pressedThisTick(kFirst, kAction(NavigationAction::Previous)) &&
                    !mapper->pressedThisTick(kFirst, kAction(NavigationAction::Next)));
    mapper->submit(ControlEvent{.device = kKeyboard, .control = key("tab"), .x = 0});
    mapper->submit(ControlEvent{.device = kKeyboard, .control = key("shift_left"), .x = 0});
    mapper->commit(++tick);
    // While a field takes text the keys are its; the gamepad still moves.
    mapper->setTextEditing(true);
    RAWFRAME_EXPECT(!kTap(kKeyboard, key("arrow_up"), kAction(NavigationAction::Up)) &&
                    kTap(kPad, pad("dpad_up"), kAction(NavigationAction::Up)));
}
