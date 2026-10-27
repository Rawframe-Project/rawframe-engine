#include "common.h"
#include "surface_graph.h"

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <utility>

namespace rawframe::material::surface_graph {

using document::Value;

namespace {

/// An inline node's pins: an object of names, each its type's name.
result::Result<std::vector<Pin>> pinsOf(const Value* pins, std::size_t most, bool none) {
    constexpr std::array<std::pair<std::string_view, Carried>, 8> kTypes = {{{"bool", Carried::Bool},
                                                                             {"color3", Carried::Color3},
                                                                             {"color4", Carried::Color4},
                                                                             {"float", Carried::Float},
                                                                             {"int", Carried::Int},
                                                                             {"vec2", Carried::Vec2},
                                                                             {"vec3", Carried::Vec3},
                                                                             {"vec4", Carried::Vec4}}};
    if (pins == nullptr || pins->kind() != Value::Kind::Object || pins->names().size() > most ||
        (!none && pins->names().empty())) {
        return invalid("an inline Slang node's inputs are at most eight and its outputs one to four");
    }
    std::vector<Pin> made;
    for (std::size_t at = 0; at < pins->names().size(); ++at) {
        const Value& kType = pins->items()[at];
        const auto kFound = kType.text() != nullptr ? std::ranges::find(kTypes,
                                                                        std::string_view{*kType.text()},
                                                                        &std::pair<std::string_view, Carried>::first)
                                                    : kTypes.end();
        if (!kestNamed(pins->names()[at]) || kFound == kTypes.end()) {
            return invalid("an inline Slang node's pin is named in lower case and typed from the graph's types");
        }
        made.push_back(Pin{.name = pins->names()[at], .carried = kFound->second});
    }
    return made;
}

/// An inline node's body, held to one function's.
result::Status bodyChecked(std::string_view body) {
    if (body.empty() || body.size() > 4096 || !std::ranges::all_of(body, [](char letter) {
            return (letter >= ' ' && letter <= '~' && letter != '#' && letter != '"' && letter != '\\' &&
                    letter != '`') ||
                   letter == '\n' || letter == '\t';
        })) {
        return invalid("an inline Slang node's body is at most 4096 bytes of printable ASCII, no preprocessor line "
                       "or string");
    }
    for (const std::string_view kWord : {"import", "include", "module", "extern", "export", "implementing", "__"}) {
        if (body.contains(kWord)) {
            return invalid("an inline Slang node's body imports, includes, and exports nothing");
        }
    }
    std::int64_t depth = 0;
    for (const char kLetter : body) {
        depth += kLetter == '{' ? 1 : kLetter == '}' ? -1 : 0;
        if (depth < 0) {
            return invalid("an inline Slang node's body closes no brace it did not open");
        }
    }
    if (depth != 0) {
        return invalid("an inline Slang node's body closes every brace it opens");
    }
    return {};
}

/// Which of `among` an array of names gives, each once, in order.
result::Result<std::vector<std::string_view>> namesOf(const Value* names, std::span<const std::string_view> among) {
    if (names == nullptr || names->kind() != Value::Kind::Array) {
        return invalid("an inline Slang node declares its capabilities, domains, and stages, each a list");
    }
    std::vector<std::string_view> made;
    for (const Value& kName : names->items()) {
        const auto kFound =
            kName.text() != nullptr ? std::ranges::find(among, std::string_view{*kName.text()}) : among.end();
        if (kFound == among.end() || (!made.empty() && !(made.back() < *kFound))) {
            return invalid("an inline Slang node's capabilities, domains, and stages are known names, in order");
        }
        made.push_back(*kFound);
    }
    return made;
}

} // namespace

result::Result<Inline> inlineIn(const surface_graph::Graph& surface, const graph::Node& node) {
    constexpr std::array<std::string_view, 3> kDomains = {"canvas", "post_process", "surface"};
    constexpr std::array<std::string_view, 2> kStages = {"fragment", "vertex"};
    // Its declared inputs name its node's inputs.
    const Value* kParamsFound = node.record.find("params");
    const Value* kDeclared =
        kParamsFound != nullptr && kParamsFound->kind() == Value::Kind::Object ? kParamsFound->find("inputs") : nullptr;
    Inline made;
    RAWFRAME_TRY_ASSIGN(made.inputs, pinsOf(kDeclared, 8, true));
    std::vector<std::string_view> names;
    for (const Pin& kPin : made.inputs) {
        names.push_back(kPin.name);
    }
    std::ranges::sort(names);
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, ownMembersOf(kInlineSlangType)->params, names));
    RAWFRAME_TRY_ASSIGN(made.outputs, pinsOf(kParts.first->find("outputs"), 4, false));
    const Value* kBody = kParts.first->find("body");
    if (kBody == nullptr || kBody->text() == nullptr) {
        return invalid("an inline Slang node's body is its one function's");
    }
    made.body = *kBody->text();
    RAWFRAME_TRY(bodyChecked(made.body));
    // Generation 1 defines no capability: any asked waits.
    const Value* kCapabilities = kParts.first->find("capabilities");
    if (kCapabilities == nullptr || kCapabilities->kind() != Value::Kind::Array) {
        return invalid("an inline Slang node declares its capabilities, domains, and stages, each a list");
    }
    if (!kCapabilities->items().empty()) {
        return unsupported("an inline Slang node's capabilities wait: generation 1 defines none");
    }
    RAWFRAME_TRY_ASSIGN(const auto kDomainsGiven, namesOf(kParts.first->find("domains"), kDomains));
    if (std::ranges::find(kDomainsGiven, std::string_view{"surface"}) == kDomainsGiven.end()) {
        return invalid("an inline Slang node in a surface declares the surface's domain");
    }
    RAWFRAME_TRY_ASSIGN(const auto kStagesGiven, namesOf(kParts.first->find("stages"), kStages));
    made.fragment = std::ranges::find(kStagesGiven, std::string_view{"fragment"}) != kStagesGiven.end();
    made.vertex = std::ranges::find(kStagesGiven, std::string_view{"vertex"}) != kStagesGiven.end();
    if (!made.fragment && !made.vertex) {
        return invalid("an inline Slang node declares the stages it is written in");
    }
    for (const Pin& kPin : made.outputs) {
        if (std::ranges::find(made.inputs, kPin.name, &Pin::name) != made.inputs.end()) {
            return invalid("an inline Slang node's inputs and outputs are named apart");
        }
    }
    // Each declared input given, of its type: three numbers a vec3's as a
    // color3's, a whole number an int's.
    for (const Pin& kPin : made.inputs) {
        const Value* kGiven = kParts.second->find(kPin.name);
        if (kGiven == nullptr) {
            return invalid("an inline Slang node's every input is given");
        }
        RAWFRAME_TRY_ASSIGN(Operand operand, operandOf(surface, *kGiven));
        if (!operand.from.has_value() &&
            ((operand.carried == Carried::Color3 && kPin.carried == Carried::Vec3) ||
             (operand.carried == Carried::Color4 && kPin.carried == Carried::Vec4) ||
             (operand.carried == Carried::Float && kPin.carried == Carried::Int && wholeNumber(operand.literal[0])))) {
            operand.carried = kPin.carried;
        }
        if (operand.carried.has_value() && *operand.carried != kPin.carried) {
            return invalid("an inline Slang node's input is given its declared type");
        }
        made.given.push_back(operand);
    }
    return made;
}

} // namespace rawframe::material::surface_graph
