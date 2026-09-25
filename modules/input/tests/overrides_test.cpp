// Rebinding: overrides written, read back to the same bytes, applied
// without touching the defaults, orphans kept and reported, and conflicts
// found by what would activate together.

#include "rawframe/document/errors.h"
#include "rawframe/input/conflicts.h"
#include "rawframe/input/overrides.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <string>

using namespace rawframe;
using namespace rawframe::input;

namespace {

constexpr std::string_view kSet = R"({
  "kind": "input.actions",
  "formatVersion": 1,
  "actions": [
    {
      "actionId": "00000000000000a1",
      "name": "jump",
      "valueType": "bool",
      "bindings": [
        {
          "device": "keyboard",
          "physicalKey": "space"
        },
        {
          "device": "gamepad",
          "control": "face_south"
        }
      ]
    },
    {
      "actionId": "00000000000000a2",
      "name": "interact",
      "valueType": "bool",
      "bindings": [
        {
          "device": "keyboard",
          "physicalKey": "key_e"
        }
      ]
    },
    {
      "actionId": "00000000000000a3",
      "name": "confirm",
      "valueType": "bool",
      "bindings": [
        {
          "device": "keyboard",
          "physicalKey": "enter"
        }
      ]
    },
    {
      "actionId": "00000000000000a4",
      "name": "pause",
      "valueType": "bool",
      "bindings": [
        {
          "device": "keyboard",
          "physicalKey": "escape"
        }
      ]
    }
  ],
  "contexts": [
    {
      "contextId": "00000000000000c1",
      "name": "game",
      "actions": [
        "jump",
        "interact",
        "pause"
      ]
    },
    {
      "contextId": "00000000000000c2",
      "name": "menu",
      "priority": 10,
      "actions": [
        "confirm"
      ]
    }
  ],
  "reserved": [
    {
      "device": "keyboard",
      "physicalKey": "f12"
    }
  ]
}
)";

constexpr std::uint64_t kJump = 0xa1;
constexpr std::uint64_t kInteract = 0xa2;

Binding keyBinding(std::string_view name, std::uint32_t slot = 0) {
    Binding binding;
    binding.slot = slot;
    binding.device = DeviceClass::Keyboard;
    binding.controls[0] = *controlNamed(DeviceClass::Keyboard, name);
    return binding;
}

std::size_t count(const std::vector<Conflict>& found, Conflict::Kind kind) {
    return static_cast<std::size_t>(std::ranges::count(found, kind, &Conflict::kind));
}

} // namespace

RAWFRAME_TEST(OverridesRoundTripAndApplyAsADelta) {
    const ActionSet kDefaults = *readActionSet(kSet);
    Overrides overrides{.target = "game.actions"};
    // Interact moves from E to F, and gains a second key; the gamepad's jump
    // is turned off.
    RAWFRAME_EXPECT(overrides.rebind(kDefaults, kInteract, keyBinding("key_f")).has_value());
    Binding modified = keyBinding("key_g", 1);
    modified.modifiers = static_cast<std::uint8_t>(Modifier::Ctrl);
    RAWFRAME_EXPECT(overrides.rebind(kDefaults, kInteract, modified).has_value());
    RAWFRAME_EXPECT(overrides.disable(kDefaults, kJump, DeviceClass::Gamepad, 0).has_value());
    const std::string kText = writeOverrides(overrides);
    RAWFRAME_EXPECT(kText.find("\"actionId\": \"00000000000000a1\"") <
                    kText.find("\"actionId\": \"00000000000000a2\""));
    const auto kRead = readOverrides(kText, kDefaults, "game.actions");
    RAWFRAME_EXPECT(kRead.has_value() && writeOverrides(*kRead) == kText);

    const ActionSet kApplied = applyOverrides(kDefaults, overrides);
    const Action& interact = kApplied.actions[1];
    RAWFRAME_EXPECT(interact.bindings.size() == 2 &&
                    interact.bindings[0].controls[0] == *controlNamed(DeviceClass::Keyboard, "key_f") &&
                    interact.bindings[1].slot == 1 && interact.bindings[1].modifiers != 0);
    RAWFRAME_EXPECT(kApplied.actions[0].bindings.size() == 1);
    // The defaults are untouched, and resetting is deleting.
    RAWFRAME_EXPECT(kDefaults.actions[1].bindings.size() == 1 && kDefaults.actions[0].bindings.size() == 2);
    overrides.reset(kInteract, DeviceClass::Keyboard, 1);
    RAWFRAME_EXPECT(overrides.entries.size() == 2);
    overrides.resetAll();
    RAWFRAME_EXPECT(overrides.entries.empty() && writeOverrides(overrides).find("entries") == std::string::npos);
}

RAWFRAME_TEST(OverridesAreCheckedLikeBindings) {
    const ActionSet kDefaults = *readActionSet(kSet);
    Overrides overrides{.target = "game.actions"};
    // A reserved key, a slot past the limit, and an action that is not.
    RAWFRAME_EXPECT(!overrides.rebind(kDefaults, kJump, keyBinding("f12")).has_value());
    RAWFRAME_EXPECT(!overrides.rebind(kDefaults, kJump, keyBinding("key_j", 4)).has_value());
    RAWFRAME_EXPECT(!overrides.rebind(kDefaults, 0xff, keyBinding("key_j")).has_value());
    // A pair makes no bool.
    Binding pair;
    pair.device = DeviceClass::Keyboard;
    pair.composite = Composite::Pair;
    pair.controls = {*controlNamed(DeviceClass::Keyboard, "key_q"), *controlNamed(DeviceClass::Keyboard, "key_e")};
    RAWFRAME_EXPECT(!overrides.rebind(kDefaults, kJump, pair).has_value());
    RAWFRAME_EXPECT(overrides.entries.empty());

    const auto kRefusal = [&kDefaults](std::string_view text, std::string_view target = "game.actions") {
        const auto kRead = readOverrides(text, kDefaults, target);
        return kRead.has_value() ? document::DocumentError{}
                                 : static_cast<document::DocumentError>(kRead.error().code().value);
    };
    constexpr std::string_view kHead = "{\n  \"kind\": \"input.overrides\",\n  \"formatVersion\": 1,\n  \"target\": "
                                       "\"game.actions\",\n  \"entries\": [\n";
    const auto kEntry = [](std::string_view id, std::string_view binding) {
        return std::string{"    {\n      \"actionId\": \""} + std::string{id} +
               "\",\n      \"device\": \"keyboard\",\n      \"binding\": " + std::string{binding} + "\n    }";
    };
    const auto kDocument = [&](std::string entries) {
        return std::string{kHead} + entries + "\n  ]\n}\n";
    };
    const std::string kF12 = "{\n        \"physicalKey\": \"f12\"\n      }";
    const std::string kJ = "{\n        \"physicalKey\": \"key_j\"\n      }";
    RAWFRAME_EXPECT(kRefusal(kDocument(kEntry("00000000000000a1", kJ))) == document::DocumentError{});
    RAWFRAME_EXPECT(kRefusal(kDocument(kEntry("00000000000000a1", kJ)), "other.actions") ==
                    document::DocumentError::Invalid);
    RAWFRAME_EXPECT(kRefusal(kDocument(kEntry("00000000000000a1", kF12))) == document::DocumentError::Invalid);
    RAWFRAME_EXPECT(kRefusal(kDocument(kEntry("00000000000000a1", "\"off\""))) == document::DocumentError::Invalid);
    RAWFRAME_EXPECT(kRefusal(kDocument(kEntry("00000000000000a2", kJ) + ",\n" + kEntry("00000000000000a1", kJ))) ==
                    document::DocumentError::NotCanonical);
    RAWFRAME_EXPECT(kRefusal(kDocument(kEntry("00000000000000a1", kJ) + ",\n" + kEntry("00000000000000a1", kJ))) ==
                    document::DocumentError::NotCanonical);

    // An entry for an action the set no longer has is kept, written back,
    // not applied, and reported; resetting everything keeps it.
    const std::string kOrphaned = kDocument(kEntry("00000000000000a1", kJ) + ",\n" + kEntry("00000000000000ff", kJ));
    auto kRead = readOverrides(kOrphaned, kDefaults, "game.actions");
    RAWFRAME_EXPECT(kRead.has_value() && kRead->entries.size() == 2 &&
                    kRead->entries[1].kind == OverrideEntry::Kind::Orphaned && writeOverrides(*kRead) == kOrphaned);
    if (kRead.has_value()) {
        const auto kFound = conflicts(applyOverrides(kDefaults, *kRead), &*kRead);
        RAWFRAME_EXPECT(count(kFound, Conflict::Kind::Orphaned) == 1);
        kRead->resetAll();
        RAWFRAME_EXPECT(kRead->entries.size() == 1);
    }
}

RAWFRAME_TEST(ConflictsAreWhatWouldActivateTogether) {
    const ActionSet kDefaults = *readActionSet(kSet);
    // As authored, confirm's context is above the game's but they share no
    // control, and nothing is reserved.
    RAWFRAME_EXPECT(conflicts(kDefaults).empty());

    Overrides overrides{.target = "game.actions"};
    // Interact onto space: a collision with jump in the game context.
    RAWFRAME_EXPECT(overrides.rebind(kDefaults, kInteract, keyBinding("space")).has_value());
    auto found = conflicts(applyOverrides(kDefaults, overrides));
    RAWFRAME_EXPECT(count(found, Conflict::Kind::Collision) == 1 && found[0].action == 0 && found[0].other == 1 &&
                    found[0].control == *controlNamed(DeviceClass::Keyboard, "space"));

    // Shift and space, exactly, does not meet space alone held exactly...
    Binding exactShift = keyBinding("space");
    exactShift.modifiers = static_cast<std::uint8_t>(Modifier::Shift);
    exactShift.exactModifiers = true;
    RAWFRAME_EXPECT(overrides.rebind(kDefaults, kInteract, exactShift).has_value());
    Binding exactPlain = keyBinding("space");
    exactPlain.exactModifiers = true;
    RAWFRAME_EXPECT(overrides.rebind(kDefaults, kJump, exactPlain).has_value());
    RAWFRAME_EXPECT(count(conflicts(applyOverrides(kDefaults, overrides)), Conflict::Kind::Collision) == 0);
    // ...but space alone, not exactly, is met by shift and space.
    overrides.reset(kJump, DeviceClass::Keyboard, 0);
    RAWFRAME_EXPECT(count(conflicts(applyOverrides(kDefaults, overrides)), Conflict::Kind::Collision) == 1);
    overrides.resetAll();

    // Jump onto enter: the menu above claims it, so the jump is shadowed.
    RAWFRAME_EXPECT(overrides.rebind(kDefaults, kJump, keyBinding("enter")).has_value());
    found = conflicts(applyOverrides(kDefaults, overrides));
    RAWFRAME_EXPECT(count(found, Conflict::Kind::Shadowed) == 1 && found[0].action == 0 && found[0].other == 2);

    // An authored binding of a reserved key is reported.
    ActionSet reserved = kDefaults;
    reserved.reserved.push_back(*controlNamed(DeviceClass::Keyboard, "escape"));
    found = conflicts(reserved);
    RAWFRAME_EXPECT(count(found, Conflict::Kind::Reserved) == 1 && found[0].action == 3);
}
