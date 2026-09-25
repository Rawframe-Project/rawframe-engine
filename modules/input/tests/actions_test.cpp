// Action sets: SPEC-0029's `input.actions` documents read into typed
// actions, bindings, and contexts, and every rule refused at its field.

#include "rawframe/document/errors.h"
#include "rawframe/input/actions.h"
#include "rawframe/test/test.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace rawframe;
using namespace rawframe::input;

namespace {

constexpr std::string_view kSet = R"({
  "kind": "input.actions",
  "formatVersion": 1,
  "actions": [
    {
      "actionId": "0f3a9c2e7b1d4e58",
      "name": "move",
      "valueType": "axis2d",
      "displayName": "Move",
      "group": "Movement",
      "bindings": [
        {
          "device": "keyboard",
          "quad": {
            "up": "key_w",
            "down": "key_s",
            "left": "key_a",
            "right": "key_d"
          }
        },
        {
          "device": "gamepad",
          "control": "stick_left",
          "deadzoneLower": 0.25,
          "invertY": true
        }
      ]
    },
    {
      "actionId": "91c4d2e87a3b5f60",
      "name": "jump",
      "valueType": "bool",
      "pressThreshold": 0.75,
      "releaseThreshold": 0.625,
      "bindings": [
        {
          "device": "keyboard",
          "physicalKey": "space"
        },
        {
          "slot": 1,
          "device": "keyboard",
          "physicalKey": "key_j",
          "modifiers": [
            "shift"
          ],
          "exactModifiers": true
        },
        {
          "device": "gamepad",
          "control": "face_south"
        }
      ]
    },
    {
      "actionId": "5d0e7f3a1c9b2846",
      "name": "throttle",
      "valueType": "axis1d",
      "consume": false,
      "bindings": [
        {
          "device": "gamepad",
          "control": "trigger_right",
          "scale": 2
        },
        {
          "device": "keyboard",
          "pair": {
            "negative": "arrow_down",
            "positive": "arrow_up"
          }
        }
      ]
    }
  ],
  "contexts": [
    {
      "contextId": "a7b8c9d0e1f20314",
      "name": "on_foot",
      "actions": [
        "move",
        "jump"
      ]
    },
    {
      "contextId": "b1c2d3e4f5061728",
      "name": "menu",
      "priority": 10,
      "actions": [],
      "textEditGated": false
    }
  ],
  "reserved": [
    {
      "device": "keyboard",
      "physicalKey": "escape"
    }
  ]
}
)";

/// `kSet` with one piece of text replaced.
std::string with(std::string_view from, std::string_view to) {
    std::string text{kSet};
    const std::size_t kAt = text.find(from);
    RAWFRAME_EXPECT(kAt != std::string::npos);
    if (kAt != std::string::npos) {
        text.replace(kAt, from.size(), to);
    }
    return text;
}

/// The refusal's code and its path, or line when it has no path.
std::pair<document::DocumentError, std::string> refusalOf(const std::string& text) {
    const auto kRead = readActionSet(text);
    if (kRead.has_value()) {
        return {document::DocumentError{}, ""};
    }
    std::string where;
    for (const auto& field : kRead.error().context()) {
        if (field.key == "path" || field.key == "line") {
            where = field.value;
        }
    }
    return {static_cast<document::DocumentError>(kRead.error().code().value), where};
}

} // namespace

RAWFRAME_TEST(AnActionSetReads) {
    const auto kRead = readActionSet(kSet);
    RAWFRAME_EXPECT(kRead.has_value());
    if (!kRead.has_value()) {
        return;
    }
    const ActionSet& set = *kRead;
    RAWFRAME_EXPECT(set.actions.size() == 3 && set.contexts.size() == 2 && set.reserved.size() == 1);
    const Action& move = set.actions[0];
    RAWFRAME_EXPECT(move.id == 0x0f3a9c2e7b1d4e58ULL && move.type == ValueType::Axis2D && move.displayName == "Move");
    RAWFRAME_EXPECT(move.bindings[0].composite == Composite::Quad &&
                    move.bindings[0].controls[3] == *controlNamed(DeviceClass::Keyboard, "key_d") &&
                    move.bindings[0].mode == CompositeMode::DigitalNormalized);
    RAWFRAME_EXPECT(move.bindings[1].deadzoneLower == 0.25F && move.bindings[1].deadzoneUpper == 1.0F &&
                    move.bindings[1].invertY && !move.bindings[1].invertX);
    const Action& jump = set.actions[1];
    RAWFRAME_EXPECT(jump.pressThreshold == 0.75F && jump.releaseThreshold == 0.625F && jump.consume);
    RAWFRAME_EXPECT(jump.bindings[1].slot == 1 && jump.bindings[1].exactModifiers &&
                    jump.bindings[1].modifiers == static_cast<std::uint8_t>(Modifier::Shift));
    const Action& throttle = set.actions[2];
    RAWFRAME_EXPECT(!throttle.consume && throttle.releaseThreshold == 0.5F && throttle.bindings[0].scale == 2.0F &&
                    throttle.bindings[1].composite == Composite::Pair);
    RAWFRAME_EXPECT(set.contexts[0].actions == std::vector<std::size_t>({0, 1}) && set.contexts[0].priority == 0 &&
                    set.contexts[0].textEditGated);
    RAWFRAME_EXPECT(set.contexts[1].priority == 10 && !set.contexts[1].textEditGated);
    RAWFRAME_EXPECT(set.reserved[0] == *controlNamed(DeviceClass::Keyboard, "escape"));
}

RAWFRAME_TEST(EveryRuleIsRefusedAtItsField) {
    using document::DocumentError;
    struct Case {
        std::string_view from;
        std::string_view to;
        DocumentError error;
        std::string_view where;
    };
    const std::vector<Case> kCases = {
        // The profile: bytes, order, and defaults.
        {"\"name\": \"move\",", "\"name\":\"move\",", DocumentError::NotCanonical, "7"},
        {"\"formatVersion\": 1", "\"formatVersion\": 2", DocumentError::Invalid, "formatVersion"},
        {"\"input.actions\"", "\"input.overrides\"", DocumentError::Invalid, "$.kind"},
        {"\"consume\": false", "\"consume\": true", DocumentError::NotCanonical, "$.actions[2].consume"},
        {"\"releaseThreshold\": 0.625",
         "\"releaseThreshold\": 0.75",
         DocumentError::NotCanonical,
         "$.actions[1].releaseThreshold"},
        {"\"priority\": 10", "\"priority\": 0", DocumentError::NotCanonical, "$.contexts[1].priority"},
        {"\"displayName\": \"Move\",\n      \"group\": \"Movement\"",
         "\"group\": \"Movement\",\n      \"displayName\": \"Move\"",
         DocumentError::NotCanonical,
         "$.actions[0].displayName"},
        {"\"name\": \"jump\",",
         "\"name\": \"jump\",\n      \"hotkey\": 1,",
         DocumentError::Invalid,
         "$.actions[1].hotkey"},
        // Identities and names.
        {"\"91c4d2e87a3b5f60\"", "\"91C4D2E87A3B5F60\"", DocumentError::Invalid, "$.actions[1].actionId"},
        {"\"91c4d2e87a3b5f60\"", "\"0f3a9c2e7b1d4e58\"", DocumentError::Invalid, "$.actions[1].actionId"},
        {"\"b1c2d3e4f5061728\"", "\"0f3a9c2e7b1d4e58\"", DocumentError::Invalid, "$.contexts[1].contextId"},
        {"\"name\": \"jump\"", "\"name\": \"move\"", DocumentError::Invalid, "$.actions[1].name"},
        {"\"name\": \"jump\"", "\"name\": \"Jump\"", DocumentError::Invalid, "$.actions[1].name"},
        {"\"name\": \"menu\"", "\"name\": \"on_foot\"", DocumentError::Invalid, "$.contexts[1].name"},
        // Controls, composites, and types.
        {"\"physicalKey\": \"space\"",
         "\"physicalKey\": \"Space\"",
         DocumentError::Invalid,
         "$.actions[1].bindings[0].physicalKey"},
        {"\"physicalKey\": \"space\"",
         "\"control\": \"space\"",
         DocumentError::Invalid,
         "$.actions[1].bindings[0].control"},
        {"\"control\": \"face_south\"",
         "\"physicalKey\": \"face_south\"",
         DocumentError::Invalid,
         "$.actions[1].bindings[2].physicalKey"},
        {"\"physicalKey\": \"space\"",
         "\"logicalKey\": \" \"",
         DocumentError::Invalid,
         "$.actions[1].bindings[0].logicalKey"},
        {"\"control\": \"trigger_right\"",
         "\"control\": \"stick_left\"",
         DocumentError::Invalid,
         "$.actions[2].bindings[0]"},
        {"\"control\": \"stick_left\"",
         "\"control\": \"face_south\"",
         DocumentError::Invalid,
         "$.actions[0].bindings[1]"},
        {"\"right\": \"key_d\"",
         "\"right\": \"key_d\",\n            \"extra\": \"key_e\"",
         DocumentError::Invalid,
         "$.actions[0].bindings[0].quad.extra"},
        {"\"device\": \"gamepad\",\n          \"control\": \"trigger_right\"",
         "\"device\": \"gamepad\",\n          \"control\": \"trigger_right\",\n          \"invertY\": true",
         DocumentError::Invalid,
         "$.actions[2].bindings[0].invertY"},
        {"\"device\": \"keyboard\",\n          \"physicalKey\": \"space\"",
         "\"device\": \"keyboard\",\n          \"physicalKey\": \"space\",\n          \"deadzoneLower\": 0.1",
         DocumentError::Invalid,
         "$.actions[1].bindings[0]"},
        {"\"deadzoneLower\": 0.25", "\"deadzoneLower\": 1", DocumentError::Invalid, "$.actions[0].bindings[1]"},
        {"\"shift\"", "\"hyper\"", DocumentError::Invalid, "$.actions[1].bindings[1].modifiers[0]"},
        {"\"device\": \"gamepad\",\n          \"control\": \"face_south\"",
         "\"device\": \"gamepad\",\n          \"control\": \"face_south\",\n          \"exactModifiers\": true",
         DocumentError::Invalid,
         "$.actions[1].bindings[2]"},
        {"\"slot\": 1,\n          \"device\": \"keyboard\"",
         "\"device\": \"keyboard\"",
         DocumentError::Invalid,
         "$.actions[1].bindings[1]"},
        {"\"device\": \"gamepad\",\n          \"control\": \"face_south\"",
         "\"device\": \"joystick\"",
         DocumentError::Invalid,
         "$.actions[1].bindings[2].device"},
        // Thresholds, references, reserved.
        {"\"pressThreshold\": 0.75", "\"pressThreshold\": 0.5625", DocumentError::Invalid, "$.actions[1]"},
        {"\"move\",\n        \"jump\"",
         "\"move\",\n        \"fly\"",
         DocumentError::Invalid,
         "$.contexts[0].actions[1]"},
        {"\"move\",\n        \"jump\"",
         "\"move\",\n        \"move\"",
         DocumentError::Invalid,
         "$.contexts[0].actions[1]"},
        {"\"physicalKey\": \"escape\"", "\"control\": \"escape\"", DocumentError::Invalid, "$.reserved[0].control"},
    };
    RAWFRAME_EXPECT(refusalOf(std::string{kSet}) == std::pair(DocumentError{}, std::string{}));
    for (const Case& each : kCases) {
        const auto kRefusal = refusalOf(with(each.from, each.to));
        RAWFRAME_EXPECT(kRefusal.first == each.error && kRefusal.second == each.where);
        if (kRefusal.first != each.error || kRefusal.second != each.where) {
            std::fprintf(stderr,
                         "  replacing %.*s: code %u at %s\n",
                         static_cast<int>(each.from.size()),
                         each.from.data(),
                         static_cast<unsigned>(kRefusal.first),
                         kRefusal.second.c_str());
        }
    }
}

RAWFRAME_TEST(ControlsHaveOneNameAndOneShape) {
    for (const DeviceClass kDevice : {DeviceClass::Keyboard, DeviceClass::Mouse, DeviceClass::Gamepad}) {
        for (std::uint16_t code = 1;; ++code) {
            const Control kControl{.device = kDevice, .code = code};
            const std::string_view kName = nameOf(kControl);
            if (kName.empty()) {
                RAWFRAME_EXPECT(code > 5);
                break;
            }
            RAWFRAME_EXPECT(controlNamed(kDevice, kName) == kControl);
        }
    }
    RAWFRAME_EXPECT(shapeOf(*controlNamed(DeviceClass::Mouse, "delta")) == ControlShape::Axis2);
    RAWFRAME_EXPECT(relative(*controlNamed(DeviceClass::Mouse, "wheel_y")));
    RAWFRAME_EXPECT(!relative(*controlNamed(DeviceClass::Gamepad, "stick_left")));
    RAWFRAME_EXPECT(modifierOf(*controlNamed(DeviceClass::Keyboard, "shift_right")) == Modifier::Shift);
    RAWFRAME_EXPECT(!controlNamed(DeviceClass::Gamepad, "key_a").has_value());
}
