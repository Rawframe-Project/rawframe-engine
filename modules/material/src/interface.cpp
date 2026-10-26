#include "common.h"
#include "surface_graph.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace rawframe::material::surface_graph {

using document::Value;

bool kestNamed(std::string_view name) {
    return !name.empty() && name.size() <= 64 && name[0] >= 'a' && name[0] <= 'z' &&
           std::ranges::all_of(name, [](char letter) {
               return (letter >= 'a' && letter <= 'z') || (letter >= '0' && letter <= '9') || letter == '_';
           });
}

result::Result<std::array<double, 4>> interfaceValueOf(Carried carried, const Value& value) {
    const graph::Document kNone;
    RAWFRAME_TRY_ASSIGN(const Operand kValue, operandOf(Graph{kNone}, value));
    if (carried == Carried::Int) {
        if (kValue.carried != Carried::Float || !wholeNumber(kValue.literal[0]) ||
            std::abs(kValue.literal[0]) > 16777216.0) {
            return invalid("an int input's value is a whole number within 2^24 either way of nought");
        }
        return kValue.literal;
    }
    // Three or four numbers read as a color either way.
    const Carried kRead = carried == Carried::Vec3   ? Carried::Color3
                          : carried == Carried::Vec4 ? Carried::Color4
                                                     : carried;
    if (kValue.carried != kRead) {
        return invalid("an interface input's value is of its type");
    }
    return kValue.literal;
}

result::Result<std::vector<InterfaceInput>> interfaceOf(const surface_graph::Graph& surface) {
    constexpr std::array<std::pair<std::string_view, Carried>, 8> kTypes = {{{"float", Carried::Float},
                                                                             {"int", Carried::Int},
                                                                             {"bool", Carried::Bool},
                                                                             {"vec2", Carried::Vec2},
                                                                             {"vec3", Carried::Vec3},
                                                                             {"color3", Carried::Color3},
                                                                             {"vec4", Carried::Vec4},
                                                                             {"color4", Carried::Color4}}};
    const Value& kInterface = surface.document().interface;
    std::vector<InterfaceInput> made;
    // A function's interface has its outputs after its inputs.
    const bool kFunction = surface.document().kind == "function";
    const Value* kInputsFound = kInterface.kind() == Value::Kind::Object ? kInterface.find("inputs") : nullptr;
    const std::size_t kMembers = kInterface.kind() == Value::Kind::Object ? kInterface.names().size() : 0;
    const bool kShaped =
        kInterface.kind() == Value::Kind::Object &&
        (kFunction ? kInterface.find("outputs") != nullptr && kMembers == (kInputsFound != nullptr ? 2U : 1U) &&
                         (kInputsFound == nullptr || kInterface.names()[0] == "inputs")
                   : std::ranges::all_of(kInterface.kind() == Value::Kind::Object ? kInterface.names()
                                                                                  : std::vector<std::string>{},
                                         [&kInterface](const std::string& name) {
                                             // Its inputs, then its static switches.
                                             return (name == "inputs" && kInterface.names()[0] == "inputs") ||
                                                    (name == "switches" && kInterface.names().back() == "switches");
                                         }));
    if (!kShaped) {
        return invalid("a surface's interface is its inputs and its switches, or nothing; a function's, its inputs "
                       "and outputs");
    }
    if (kInputsFound == nullptr) {
        return made;
    }
    const Value& kInputs = *kInputsFound;
    if (kInputs.kind() != Value::Kind::Object || kInputs.names().empty() || kInputs.names().size() > 8) {
        return invalid("a surface's interface inputs are one to eight");
    }
    for (std::size_t at = 0; at < kInputs.names().size(); ++at) {
        const std::string& kName = kInputs.names()[at];
        const bool kNamed =
            !kName.empty() && kName.size() <= 64 && (kName[0] < '0' || kName[0] > '9') &&
            std::ranges::all_of(kName, [](char letter) {
                return (letter >= 'a' && letter <= 'z') || (letter >= '0' && letter <= '9') || letter == '_';
            });
        const Value& kInput = kInputs.items()[at];
        const Value* kDefault = kInput.kind() == Value::Kind::Object ? kInput.find("default") : nullptr;
        const Value* kType = kInput.kind() == Value::Kind::Object ? kInput.find("type") : nullptr;
        // A function's input may default to a stream, as a mesh's own
        // coordinates.
        const Value* kStream = kInput.kind() == Value::Kind::Object ? kInput.find("stream") : nullptr;
        if (kFunction && kNamed && kStream != nullptr && kType != nullptr && kInput.names().size() == 2 &&
            kInput.names()[0] == "stream") {
            constexpr std::array<std::pair<std::string_view, std::string_view>, 4> kStreams = {
                {{"uv", "vec2"}, {"world_position", "vec3"}, {"world_normal", "vec3"}, {"view_direction", "vec3"}}};
            const auto kOf = kStream->text() != nullptr
                                 ? std::ranges::find(kStreams,
                                                     std::string_view{*kStream->text()},
                                                     &std::pair<std::string_view, std::string_view>::first)
                                 : kStreams.end();
            if (kOf == kStreams.end() || kType->text() == nullptr || *kType->text() != kOf->second) {
                return invalid("a function's input defaults to the uv, world_position, world_normal, or view_direction "
                               "stream, of its type");
            }
            made.push_back(InterfaceInput{.name = kName,
                                          .carried = kOf->second == "vec2" ? Carried::Vec2 : Carried::Vec3,
                                          .slot = at,
                                          .stream = kOf->first});
            continue;
        }
        // A function's texture input has no default: a function knows no
        // game's texture, and each call gives it one.
        if (kFunction && kType != nullptr && kType->text() != nullptr && *kType->text() == "texture_2d") {
            if (!kNamed || kInput.names().size() != 1) {
                return invalid("a function's texture input is named in lower case, its type alone given");
            }
            made.push_back(InterfaceInput{.name = kName, .carried = Carried::Texture2d, .slot = at});
            continue;
        }
        if (!kNamed || kDefault == nullptr || kType == nullptr || kType->text() == nullptr ||
            kInput.names().size() != 2 || kInput.names()[0] != "default") {
            return invalid("an interface input is named in lower case, its default and its type given");
        }
        if (*kType->text() == "texture_2d") {
            const std::optional<std::uint64_t> kId =
                kDefault->text() != nullptr ? graph::nodeIdOf(*kDefault->text()) : std::nullopt;
            if (!kId.has_value() || *kId == 0) {
                return invalid("a texture input's default names the game's texture by 16 lowercase hex digits");
            }
            made.push_back(InterfaceInput{.name = kName, .carried = Carried::Texture2d, .slot = at, .texture = *kId});
            continue;
        }
        const auto kFound =
            std::ranges::find(kTypes, std::string_view{*kType->text()}, &std::pair<std::string_view, Carried>::first);
        if (kFound == kTypes.end()) {
            return invalid("an interface input's type is one of the graph's");
        }
        RAWFRAME_TRY_ASSIGN(const auto kInitial, interfaceValueOf(kFound->second, *kDefault));
        made.push_back(InterfaceInput{.name = kName, .carried = kFound->second, .initial = kInitial, .slot = at});
    }
    return made;
}

result::Result<InterfaceInput> parameterIn(const surface_graph::Graph& surface, const graph::Node& node) {
    RAWFRAME_TRY_ASSIGN(const auto kParts, ownPartsOf(node));
    const Value* kName = kParts.first->find("name");
    if (kName == nullptr || kName->text() == nullptr) {
        return invalid("a parameter node names its interface input");
    }
    RAWFRAME_TRY_ASSIGN(const std::vector<InterfaceInput> kInputs, interfaceOf(surface));
    const auto kFound = std::ranges::find(kInputs, std::string_view{*kName->text()}, &InterfaceInput::name);
    if (kFound == kInputs.end()) {
        return invalid("a parameter node names one of the surface's interface inputs");
    }
    if ((kFound->carried == Carried::Texture2d) != (typeOf(node) == kParameterTexture2dType)) {
        return invalid("a texture input is a parameter_texture_2d node's, any other a parameter node's");
    }
    return *kFound;
}

result::Result<std::vector<StaticSwitch>> switchesOf(const surface_graph::Graph& surface) {
    RAWFRAME_TRY(interfaceOf(surface));
    std::vector<StaticSwitch> made;
    const Value* kSwitches = surface.document().interface.kind() == Value::Kind::Object
                                 ? surface.document().interface.find("switches")
                                 : nullptr;
    if (kSwitches == nullptr) {
        return made;
    }
    if (kSwitches->kind() != Value::Kind::Object || kSwitches->names().empty() || kSwitches->names().size() > 8) {
        return invalid("a surface's static switches are one to eight");
    }
    for (std::size_t at = 0; at < kSwitches->names().size(); ++at) {
        const Value& kSwitch = kSwitches->items()[at];
        const Value* kDefault =
            kSwitch.kind() == Value::Kind::Object && kSwitch.names().size() == 1 ? kSwitch.find("default") : nullptr;
        const std::optional<bool> kOn = kDefault != nullptr ? kDefault->truth() : std::nullopt;
        if (!kestNamed(kSwitches->names()[at]) || !kOn.has_value()) {
            return invalid("a static switch is named in lower case, its default a bool alone");
        }
        made.push_back(StaticSwitch{.name = kSwitches->names()[at], .on = *kOn});
    }
    return made;
}

result::Result<SwitchChoice> staticSwitchIn(const surface_graph::Graph& surface, const graph::Node& node) {
    RAWFRAME_TRY_ASSIGN(const auto kParts, ownPartsOf(node));
    RAWFRAME_TRY_ASSIGN(const std::vector<StaticSwitch> kSwitches, switchesOf(surface));
    const Value* kName = kParts.first->find("name");
    const auto kFound = kName != nullptr && kName->text() != nullptr
                            ? std::ranges::find(kSwitches, std::string_view{*kName->text()}, &StaticSwitch::name)
                            : kSwitches.end();
    const Value* kOff = kParts.second->find("off");
    const Value* kOnGiven = kParts.second->find("on");
    if (kFound == kSwitches.end() || kOff == nullptr || kOnGiven == nullptr) {
        return invalid("a static switch node names a declared switch, and its inputs off and on are given");
    }
    SwitchChoice made{.taken = kFound->on};
    RAWFRAME_TRY_ASSIGN(made.off, operandOf(surface, *kOff));
    RAWFRAME_TRY_ASSIGN(made.on, operandOf(surface, *kOnGiven));
    // Both of one type: three or four numbers a vector's beside one.
    for (auto [mine, other] : {std::pair{&made.off, &made.on}, std::pair{&made.on, &made.off}}) {
        if (!mine->from.has_value() && other->carried.has_value() &&
            ((mine->carried == Carried::Color3 && other->carried == Carried::Vec3) ||
             (mine->carried == Carried::Color4 && other->carried == Carried::Vec4))) {
            mine->carried = other->carried;
        }
    }
    if (made.off.carried == Carried::Texture2d || made.on.carried == Carried::Texture2d ||
        (made.off.carried.has_value() && made.on.carried.has_value() && *made.off.carried != *made.on.carried)) {
        return invalid("a static switch node's inputs are of one type, never a texture");
    }
    made.out = made.off.carried.has_value() ? made.off.carried : made.on.carried;
    return made;
}

} // namespace rawframe::material::surface_graph
