#include "rawframe/input/navigation.h"

#include "rawframe/input/controls.h"
#include "rawframe/input/errors.h"

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string_view>
#include <utility>

namespace rawframe::input {
namespace {

constexpr std::uint64_t kContextId = 0xb7bfecf881947dc1;

/// One navigation action: its identity, name, and controls, a keyboard key
/// with the modifiers it needs exactly, and a gamepad control.
struct Declared {
    std::uint64_t id = 0;
    std::string_view name;
    std::string_view displayName;
    std::initializer_list<std::pair<std::string_view, std::uint8_t>> keys;
    std::string_view pad;
};

constexpr auto kShift = static_cast<std::uint8_t>(Modifier::Shift);

} // namespace

result::Result<Navigation> addNavigation(ActionSet& set) {
    const std::array<Declared, kNavigationActions> kDeclared = {{
        {0x678de22b09198f93, "rawframe.ui.up", "Up", {{"arrow_up", 0}}, "dpad_up"},
        {0x0a55f17e48cfd8f0, "rawframe.ui.down", "Down", {{"arrow_down", 0}}, "dpad_down"},
        {0x639dcb1801855271, "rawframe.ui.left", "Left", {{"arrow_left", 0}}, "dpad_left"},
        {0x5d6a280ebf6561b9, "rawframe.ui.right", "Right", {{"arrow_right", 0}}, "dpad_right"},
        {0x4eb95317956c605b, "rawframe.ui.next", "Next", {{"tab", 0}}, "shoulder_right"},
        {0x27a39c4f29464d8a, "rawframe.ui.previous", "Previous", {{"tab", kShift}}, "shoulder_left"},
        {0xdd48bf7cc1ff68ba,
         "rawframe.ui.activate",
         "Activate",
         {{"enter", 0}, {"numpad_enter", 0}, {"space", 0}},
         "face_south"},
        {0xf889516c9f07521c, "rawframe.ui.dismiss", "Dismiss", {{"escape", 0}}, "face_east"},
    }};
    const auto kTaken = [&set](std::uint64_t id, std::string_view name) {
        return set.actionWithId(id).has_value() || set.actionNamed(name).has_value();
    };
    if (set.contextNamed("rawframe.ui.navigation").has_value()) {
        return result::fail(result::ErrorClass::AlreadyExists,
                            kInputDomain,
                            code(InputError::Taken),
                            "the set already has the navigation context");
    }
    for (const Declared& kAction : kDeclared) {
        if (kTaken(kAction.id, kAction.name)) {
            return result::fail(result::ErrorClass::AlreadyExists,
                                kInputDomain,
                                code(InputError::Taken),
                                "the set already has a navigation action's name or identity");
        }
    }
    Navigation navigation;
    Context context{.id = kContextId,
                    .name = "rawframe.ui.navigation",
                    .priority = std::numeric_limits<std::int64_t>::max(),
                    .actions = {},
                    .textEditGated = true};
    for (std::size_t each = 0; each < kDeclared.size(); ++each) {
        const Declared& kAction = kDeclared[each];
        Action action{.id = kAction.id,
                      .name = std::string{kAction.name},
                      .type = ValueType::Bool,
                      .displayName = std::string{kAction.displayName},
                      .group = "rawframe.ui",
                      .bindings = {}};
        for (const auto& [kKey, kModifiers] : kAction.keys) {
            action.bindings.push_back(Binding{.device = DeviceClass::Keyboard,
                                              .controls = {*controlNamed(DeviceClass::Keyboard, kKey)},
                                              .modifiers = kModifiers,
                                              .exactModifiers = true});
        }
        action.bindings.push_back(
            Binding{.device = DeviceClass::Gamepad, .controls = {*controlNamed(DeviceClass::Gamepad, kAction.pad)}});
        navigation.actions[each] = set.actions.size();
        context.actions.push_back(set.actions.size());
        set.actions.push_back(std::move(action));
    }
    navigation.context = set.contexts.size();
    set.contexts.push_back(std::move(context));
    return navigation;
}

} // namespace rawframe::input
