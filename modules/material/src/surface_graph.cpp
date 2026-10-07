#include "surface_graph.h"

#include "common.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>

namespace rawframe::material::surface_graph {

using document::Value;

const Value* statesOf(const graph::Document& surface) {
    const auto kFound = std::ranges::find(surface.sections, std::string_view{"states"}, [](const auto& section) {
        return std::string_view{section.first};
    });
    return kFound == surface.sections.end() ? nullptr : &kFound->second;
}

result::Result<Material> statesIn(const Value* states) {
    Material made;
    if (states == nullptr) {
        return made;
    }
    constexpr std::array<std::string_view, 4> kOrder = {"shading", "blend", "alpha_cutoff", "double_sided"};
    if (states->kind() != Value::Kind::Object || states->names().empty()) {
        return invalid("a material's states are an object, left out when all are at their defaults");
    }
    std::size_t next = 0;
    for (std::size_t at = 0; at < states->names().size(); ++at) {
        const std::string& name = states->names()[at];
        const Value& value = states->items()[at];
        const auto kPlace = std::ranges::find(kOrder, name);
        if (kPlace == kOrder.end() || static_cast<std::size_t>(kPlace - kOrder.begin()) < next) {
            return invalid("a material's states are shading, blend, alpha_cutoff, and double_sided, in that order");
        }
        next = static_cast<std::size_t>(kPlace - kOrder.begin()) + 1;
        if (name == "shading" || name == "blend") {
            const bool kShading = name == "shading";
            const std::span<const std::string_view> kSet =
                kShading ? std::span<const std::string_view>{kShadings} : std::span<const std::string_view>{kBlends};
            const auto kFound =
                value.kind() == Value::Kind::String ? std::ranges::find(kSet, *value.text()) : kSet.end();
            if (kFound == kSet.end() || kFound == kSet.begin()) {
                return invalid("a material's shading is unlit, its blend masked or translucent, when not the default");
            }
            const auto kIndex = static_cast<std::uint8_t>(kFound - kSet.begin());
            if (kShading) {
                made.shading = static_cast<Shading>(kIndex);
            } else {
                made.blend = static_cast<Blend>(kIndex);
            }
        } else if (name == "alpha_cutoff") {
            const std::optional<double> kCutoff = value.kind() == Value::Kind::Number ? value.real() : std::nullopt;
            if (!kCutoff.has_value() || !(*kCutoff >= 0 && *kCutoff <= 1) || *kCutoff == 0.5) {
                return invalid("a material's alpha cutoff is from nought to one, when not 0.5");
            }
            made.alphaCutoff = static_cast<float>(*kCutoff);
        } else if (value.truth() != true) {
            return invalid("a material is double sided when it says so");
        }
        made.doubleSided = made.doubleSided || name == "double_sided";
    }
    if (states->find("alpha_cutoff") != nullptr && made.blend != Blend::Masked) {
        return invalid("a material's alpha cutoff is a masked one's");
    }
    return made;
}

const graph::Node* surfaceNode(const graph::Document& surface) {
    const graph::Node* found = nullptr;
    for (const graph::Node& node : surface.nodes) {
        const Value* kType = node.record.find("type");
        if (kType != nullptr && kType->text() != nullptr && *kType->text() == kSurfaceType) {
            if (found != nullptr) {
                return nullptr;
            }
            found = &node;
        }
    }
    return found;
}

bool known(const graph::Node& node) {
    const Value* kType = node.record.find("type");
    const std::string* kText = kType != nullptr ? kType->text() : nullptr;
    return kText != nullptr &&
           (*kText == kSurfaceType || *kText == kSampleTexture2dType || *kText == kUvType || *kText == kMultiplyType ||
            *kText == kAddType || *kText == kSeparate3Type || *kText == kNormalMapType || *kText == kQualitySwitchType);
}

bool math(std::string_view type) {
    return type == kMultiplyType || type == kAddType;
}

std::string_view typeOf(const graph::Node& node) {
    return *node.record.find("type")->text();
}

result::Result<std::uint32_t> uvIn(const graph::Node& node) {
    constexpr std::array<std::string_view, 1> kParams = {"channel"};
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, kParams, {}));
    const Value* kChannel = kParts.first->find("channel");
    if (kChannel == nullptr) {
        return 0U;
    }
    const std::optional<std::int64_t> kNumber =
        kChannel->kind() == Value::Kind::Number ? kChannel->integer() : std::nullopt;
    if (!kNumber.has_value() || *kNumber < 1 || *kNumber > 7) {
        return invalid("a uv node's channel is from one to seven, when not nought");
    }
    return static_cast<std::uint32_t>(*kNumber);
}

result::Result<Operand> operandOf(const graph::Document& surface, const Value& value) {
    Operand made;
    made.from = graph::connectionOf(value);
    if (made.from.has_value()) {
        RAWFRAME_TRY_ASSIGN(made.carried, carriedBy(surface, *made.from));
        return made;
    }
    const auto kFinite = [](const Value& each) -> std::optional<double> {
        const std::optional<double> kReal = each.kind() == Value::Kind::Number ? each.real() : std::nullopt;
        return kReal.has_value() && std::isfinite(*kReal) ? kReal : std::nullopt;
    };
    if (const std::optional<double> kOne = kFinite(value); kOne.has_value()) {
        made.literal[0] = *kOne;
        made.carried = Carried::Float;
        return made;
    }
    const std::size_t kCount = value.kind() == Value::Kind::Array ? value.items().size() : 0;
    if (kCount != 2 && kCount != 3) {
        return invalid("a math node's literal is a number, or two or three of them");
    }
    for (std::size_t at = 0; at < kCount; ++at) {
        const std::optional<double> kChannel = kFinite(value.items()[at]);
        if (!kChannel.has_value()) {
            return invalid("a math node's literal is a number, or two or three of them");
        }
        made.literal[at] = *kChannel;
    }
    made.carried = kCount == 2 ? Carried::Vec2 : Carried::Color3;
    return made;
}

result::Result<Math> mathIn(const graph::Document& surface, const graph::Node& node) {
    constexpr std::array<std::string_view, 2> kInputs = {"a", "b"};
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, {}, kInputs));
    const Value* kA = kParts.second->find("a");
    const Value* kB = kParts.second->find("b");
    if (kA == nullptr || kB == nullptr) {
        return invalid("a math node's inputs are a and b, both given");
    }
    Math made;
    RAWFRAME_TRY_ASSIGN(made.a, operandOf(surface, *kA));
    RAWFRAME_TRY_ASSIGN(made.b, operandOf(surface, *kB));
    if (made.a.carried.has_value() && made.b.carried.has_value()) {
        const Carried kA2 = *made.a.carried;
        const Carried kB2 = *made.b.carried;
        if (kA2 != kB2 && kA2 != Carried::Float && kB2 != Carried::Float) {
            return invalid("a math node's inputs are of one type, or one of them is a float");
        }
        made.out = kA2 == Carried::Float ? kB2 : kA2;
    }
    return made;
}

result::Result<Switch> switchIn(const graph::Document& surface, const graph::Node& node) {
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, {}, kSwitchInputs));
    if (kParts.second->find("default") == nullptr) {
        return invalid("a quality switch's default is given");
    }
    Switch made;
    for (std::size_t at = 0; at < kSwitchInputs.size(); ++at) {
        const Value* kInput = kParts.second->find(kSwitchInputs.at(at));
        if (kInput == nullptr) {
            continue;
        }
        RAWFRAME_TRY_ASSIGN(made.inputs.at(at), operandOf(surface, *kInput));
        const std::optional<Carried> kCarried = made.inputs.at(at)->carried;
        if (kCarried.has_value() && made.out.has_value() && *kCarried != *made.out) {
            return invalid("a quality switch's inputs are of one type");
        }
        made.out = made.out.has_value() ? made.out : kCarried;
    }
    return made;
}

result::Result<Operand> separate3In(const graph::Document& surface, const graph::Node& node) {
    constexpr std::array<std::string_view, 1> kInputs = {"in"};
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, {}, kInputs));
    const Value* kIn = kParts.second->find("in");
    if (kIn == nullptr) {
        return invalid("a separate3 node's input is in, given");
    }
    RAWFRAME_TRY_ASSIGN(Operand made, operandOf(surface, *kIn));
    if (made.carried.has_value() && *made.carried != Carried::Color3) {
        return invalid("a separate3 node's input is a color3");
    }
    return made;
}

result::Result<NormalMap> normalMapIn(const graph::Document& surface, const graph::Node& node) {
    constexpr std::array<std::string_view, 1> kParams = {"scale"};
    constexpr std::array<std::string_view, 1> kInputs = {"in"};
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, kParams, kInputs));
    const Value* kIn = kParts.second->find("in");
    if (kIn == nullptr) {
        return invalid("a normal map's input is in, given");
    }
    NormalMap made;
    RAWFRAME_TRY_ASSIGN(made.in, operandOf(surface, *kIn));
    if (made.in.carried.has_value() && *made.in.carried != Carried::Color3) {
        return invalid("a normal map's input is a color3");
    }
    if (const Value* kScale = kParts.first->find("scale"); kScale != nullptr) {
        const std::optional<double> kReal = kScale->kind() == Value::Kind::Number ? kScale->real() : std::nullopt;
        if (!kReal.has_value() || !std::isfinite(*kReal) || *kReal == 1) {
            return invalid("a normal map's scale is a finite number, left out at one");
        }
        made.scale = *kReal;
    }
    return made;
}

result::Result<Sampling> sampleIn(const graph::Document& surface, const graph::Node& node) {
    constexpr std::array<std::string_view, 3> kParams = {"address", "filter", "texture"};
    constexpr std::array<std::string_view, 1> kInputs = {"uv"};
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, kParams, kInputs));
    Sampling made;
    const Value* kTexture = kParts.first->find("texture");
    const std::optional<std::uint64_t> kId =
        kTexture != nullptr && kTexture->text() != nullptr ? graph::nodeIdOf(*kTexture->text()) : std::nullopt;
    if (!kId.has_value() || *kId == 0) {
        return invalid("a sampled texture names the game's texture by 16 lowercase hex digits, never nought");
    }
    made.texture.id = *kId;
    if (const Value* kAddress = kParts.first->find("address"); kAddress != nullptr) {
        if (kAddress->text() == nullptr || *kAddress->text() != "clamp") {
            return invalid("a sampled texture's address is clamp, when not the default");
        }
        made.texture.address = Address::Clamp;
    }
    if (const Value* kFilter = kParts.first->find("filter"); kFilter != nullptr) {
        if (kFilter->text() == nullptr || *kFilter->text() != "nearest") {
            return invalid("a sampled texture's filter is nearest, when not the default");
        }
        made.texture.filter = Filter::Nearest;
    }
    if (const Value* kUv = kParts.second->find("uv"); kUv != nullptr) {
        const std::optional<graph::Connection> kFrom = graph::connectionOf(*kUv);
        if (!kFrom.has_value()) {
            return invalid("a sampled texture's uv is connected, or left out for a mesh's first");
        }
        RAWFRAME_TRY_ASSIGN(const std::optional<Carried> kCarried, carriedBy(surface, *kFrom));
        if (kCarried.has_value() && *kCarried != Carried::Vec2) {
            return invalid("a sampled texture's uv comes from a vec2 output");
        }
        made.uv = kFrom;
    }
    return made;
}

result::Result<std::optional<Carried>> carriedBy(const graph::Document& surface, const graph::Connection& from) {
    const graph::Node* kSource = nodeOf(surface, from.node);
    if (kSource == nullptr || !known(*kSource)) {
        return std::optional<Carried>{};
    }
    const std::string_view kType = typeOf(*kSource);
    if (kType == kSampleTexture2dType && from.output == "color") {
        return std::optional{Carried::Color3};
    }
    if (kType == kSampleTexture2dType && from.output == "alpha") {
        return std::optional{Carried::Float};
    }
    if (kType == kUvType && from.output == "uv") {
        return std::optional{Carried::Vec2};
    }
    if (math(kType) && from.output == "out") {
        RAWFRAME_TRY_ASSIGN(const Math kMath, mathIn(surface, *kSource));
        return kMath.out;
    }
    if (kType == kNormalMapType && from.output == "out") {
        RAWFRAME_TRY(normalMapIn(surface, *kSource));
        return std::optional{Carried::Vec3};
    }
    if (kType == kQualitySwitchType && from.output == "out") {
        RAWFRAME_TRY_ASSIGN(const Switch kSwitch, switchIn(surface, *kSource));
        return kSwitch.out;
    }
    if (kType == kSeparate3Type && (from.output == "r" || from.output == "g" || from.output == "b")) {
        RAWFRAME_TRY(separate3In(surface, *kSource));
        return std::optional{Carried::Float};
    }
    return invalid("a connection names an output its node has");
}

} // namespace rawframe::material::surface_graph
