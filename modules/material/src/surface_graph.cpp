#include "surface_graph.h"

#include "common.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <utility>

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

namespace {

using enum Rule;

/// SPEC-0026's atomic math core as this family writes it (D691, D694).
/// `fract` is Slang's `frac`, `mix` its `lerp`; `modulo` is floored, as
/// MaterialX's is, never C's truncated remainder; `remap` takes `in` from
/// between `in_low` and `in_high` to between `out_low` and `out_high`.
constexpr std::array<MathNode, 41> kMathNodes = {{
    {kAddType, {"a", "b"}, 2, Componentwise, "$0 + $1", false},
    {kSubtractType, {"a", "b"}, 2, Componentwise, "$0 - $1", false},
    {kMultiplyType, {"a", "b"}, 2, Componentwise, "$0 * $1", false},
    {"rawframe/divide@1", {"a", "b"}, 2, Componentwise, "$0 / $1", false},
    {"rawframe/modulo@1", {"a", "b"}, 2, Componentwise, "($0) - ($1) * floor(($0) / ($1))", false},
    {"rawframe/negate@1", {"in"}, 1, Componentwise, "-($0)", false},
    {"rawframe/reciprocal@1", {"in"}, 1, Componentwise, "1.0 / ($0)", false},
    {"rawframe/abs@1", {"in"}, 1, Componentwise, "abs($0)", true},
    {"rawframe/sign@1", {"in"}, 1, Componentwise, "sign($0)", true},
    {"rawframe/floor@1", {"in"}, 1, Componentwise, "floor($0)", true},
    {"rawframe/ceil@1", {"in"}, 1, Componentwise, "ceil($0)", true},
    {"rawframe/round@1", {"in"}, 1, Componentwise, "round($0)", true},
    {"rawframe/fract@1", {"in"}, 1, Componentwise, "frac($0)", true},
    {"rawframe/min@1", {"a", "b"}, 2, Componentwise, "min($0, $1)", true},
    {"rawframe/max@1", {"a", "b"}, 2, Componentwise, "max($0, $1)", true},
    {"rawframe/clamp@1", {"in", "low", "high"}, 3, Componentwise, "clamp($0, $1, $2)", true},
    {"rawframe/saturate@1", {"in"}, 1, Componentwise, "saturate($0)", true},
    {"rawframe/mix@1", {"a", "b", "t"}, 3, Componentwise, "lerp($0, $1, $2)", true},
    {"rawframe/step@1", {"edge", "in"}, 2, Componentwise, "step($0, $1)", true},
    {"rawframe/smoothstep@1", {"low", "high", "in"}, 3, Componentwise, "smoothstep($0, $1, $2)", true},
    {"rawframe/remap@1",
     {"in", "in_low", "in_high", "out_low", "out_high"},
     5,
     Componentwise,
     "($3) + (($0) - ($1)) * (($4) - ($3)) / (($2) - ($1))",
     false},
    {"rawframe/power@1", {"a", "b"}, 2, Componentwise, "pow($0, $1)", true},
    {"rawframe/sqrt@1", {"in"}, 1, Componentwise, "sqrt($0)", true},
    {"rawframe/exp@1", {"in"}, 1, Componentwise, "exp($0)", true},
    {"rawframe/exp2@1", {"in"}, 1, Componentwise, "exp2($0)", true},
    {"rawframe/log@1", {"in"}, 1, Componentwise, "log($0)", true},
    {"rawframe/log2@1", {"in"}, 1, Componentwise, "log2($0)", true},
    {kSinType, {"in"}, 1, Componentwise, "sin($0)", true},
    {kCosType, {"in"}, 1, Componentwise, "cos($0)", true},
    {"rawframe/tan@1", {"in"}, 1, Componentwise, "tan($0)", true},
    {"rawframe/asin@1", {"in"}, 1, Componentwise, "asin($0)", true},
    {"rawframe/acos@1", {"in"}, 1, Componentwise, "acos($0)", true},
    {"rawframe/atan2@1", {"y", "x"}, 2, Componentwise, "atan2($0, $1)", true},
    {"rawframe/one_minus@1", {"in"}, 1, Componentwise, "1.0 - ($0)", false},
    {"rawframe/length@1", {"in"}, 1, Measure, "length($0)", false},
    {kNormalizeType, {"in"}, 1, Unit, "normalize($0)", false},
    {"rawframe/dot@1", {"a", "b"}, 2, Between, "dot($0, $1)", false},
    {"rawframe/distance@1", {"a", "b"}, 2, Between, "distance($0, $1)", false},
    {"rawframe/cross@1", {"a", "b"}, 2, Across, "cross($0, $1)", false},
    {"rawframe/reflect@1", {"in", "normal"}, 2, Across, "reflect($0, $1)", false},
    {"rawframe/refract@1", {"in", "normal", "eta"}, 3, Across, "refract($0, $1, $2)", false},
}};

} // namespace

const MathNode* mathNodeOf(std::string_view type) {
    const auto kFound = std::ranges::find(kMathNodes, type, &MathNode::type);
    return kFound == kMathNodes.end() ? nullptr : &*kFound;
}

bool known(const graph::Node& node) {
    const Value* kType = node.record.find("type");
    const std::string* kText = kType != nullptr ? kType->text() : nullptr;
    return kText != nullptr &&
           (*kText == kSurfaceType || *kText == kSampleTexture2dType || *kText == kUvType || *kText == kSeparate3Type ||
            *kText == kNormalMapType || *kText == kQualitySwitchType || *kText == kTimeType ||
            *kText == kWorldPositionType || *kText == kWorldNormalType || *kText == kCombine3Type || math(*kText));
}

bool math(std::string_view type) {
    return mathNodeOf(type) != nullptr;
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

result::Result<MathInputs> mathInputsOf(const graph::Document& surface, const graph::Node& node) {
    const MathNode& kNode = *mathNodeOf(typeOf(node));
    // The record names its inputs in name order.
    // Sorted by insertion: GCC's ranges::sort over a span of five takes its
    // sixteen-element insertion pass for out of bounds.
    std::array<std::string_view, 5> sorted = kNode.inputs;
    for (std::size_t at = 1; at < kNode.count; ++at) {
        for (std::size_t back = at; back > 0 && sorted.at(back) < sorted.at(back - 1); --back) {
            std::swap(sorted.at(back), sorted.at(back - 1));
        }
    }
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, {}, std::span{sorted.data(), kNode.count}));
    MathInputs made{.count = kNode.count};
    for (std::size_t at = 0; at < kNode.count; ++at) {
        const Value* kInput = kParts.second->find(kNode.inputs.at(at));
        if (kInput == nullptr) {
            return invalid("a math node's inputs are all given");
        }
        RAWFRAME_TRY_ASSIGN(made.inputs.at(at), operandOf(surface, *kInput));
    }
    const std::span<Operand> kIn{made.inputs.data(), kNode.count};
    // Three numbers are a color3 or a vec3, as a connected input is.
    const bool kVector = std::ranges::any_of(kIn, [](const Operand& each) {
        return each.carried == Carried::Vec3;
    });
    for (Operand& each : kIn) {
        if (!each.from.has_value() && each.carried == Carried::Color3 && kVector) {
            each.carried = Carried::Vec3;
        }
    }
    const bool kKnown = std::ranges::all_of(kIn, [](const Operand& each) {
        return each.carried.has_value();
    });
    if (!kKnown) {
        return made;
    }
    const auto kIsVector = [](Carried carried) {
        return carried == Carried::Vec2 || carried == Carried::Vec3;
    };
    switch (kNode.rule) {
    case Componentwise: {
        std::optional<Carried> kind;
        for (const Operand& each : kIn) {
            if (*each.carried == Carried::Float) {
                continue;
            }
            if (kind.has_value() && *kind != *each.carried) {
                return invalid("a math node's inputs are of one type, or floats");
            }
            kind = each.carried;
        }
        made.out = kind.value_or(Carried::Float);
        break;
    }
    case Measure:
    case Unit:
        if (!kIsVector(*kIn[0].carried)) {
            return invalid("a length or normalize node's input is a vec2 or a vec3");
        }
        made.out = kNode.rule == Measure ? Carried::Float : *kIn[0].carried;
        break;
    case Between:
        if (!kIsVector(*kIn[0].carried) || *kIn[0].carried != *kIn[1].carried) {
            return invalid("a dot or distance node's inputs are vectors of one type");
        }
        made.out = Carried::Float;
        break;
    case Across:
        if (*kIn[0].carried != Carried::Vec3 || *kIn[1].carried != Carried::Vec3 ||
            (kNode.count == 3 && *kIn[2].carried != Carried::Float)) {
            return invalid("a cross, reflect, or refract node's vectors are vec3s, a refraction's ratio a float");
        }
        made.out = Carried::Vec3;
        break;
    }
    return made;
}

result::Result<Math> mathIn(const graph::Document& surface, const graph::Node& node) {
    RAWFRAME_TRY_ASSIGN(const MathInputs kInputs, mathInputsOf(surface, node));
    return Math{.a = kInputs.inputs[0], .b = kInputs.inputs[1], .out = kInputs.out};
}

result::Result<std::array<Operand, 3>> combine3In(const graph::Document& surface, const graph::Node& node) {
    constexpr std::array<std::string_view, 3> kInputs = {"x", "y", "z"};
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, {}, kInputs));
    std::array<Operand, 3> made;
    for (std::size_t at = 0; at < kInputs.size(); ++at) {
        const Value* kInput = kParts.second->find(kInputs.at(at));
        if (kInput == nullptr) {
            return invalid("a combine3 node's inputs are x, y, and z, all given");
        }
        RAWFRAME_TRY_ASSIGN(made.at(at), operandOf(surface, *kInput));
        if (made.at(at).carried.has_value() && *made.at(at).carried != Carried::Float) {
            return invalid("a combine3 node's inputs are floats");
        }
    }
    return made;
}

result::Status timeIn(const graph::Node& node) {
    constexpr std::array<std::string_view, 1> kParams = {"timeline"};
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, kParams, {}));
    const Value* kTimeline = kParts.first->find("timeline");
    if (kTimeline == nullptr || kTimeline->text() == nullptr || *kTimeline->text() != "simulation") {
        return invalid("a time node names its timeline, the simulation's");
    }
    return {};
}

result::Status streamIn(const graph::Node& node) {
    RAWFRAME_TRY(partsOf(node, {}, {}));
    return {};
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
    if (made.carried.has_value() && *made.carried != Carried::Color3 && *made.carried != Carried::Vec3) {
        return invalid("a separate3 node's input is a color3 or a vec3");
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
        RAWFRAME_TRY_ASSIGN(const MathInputs kMath, mathInputsOf(surface, *kSource));
        return kMath.out;
    }
    if (kType == kCombine3Type && from.output == "out") {
        RAWFRAME_TRY(combine3In(surface, *kSource));
        return std::optional{Carried::Vec3};
    }
    if (kType == kTimeType && from.output == "seconds") {
        RAWFRAME_TRY(timeIn(*kSource));
        return std::optional{Carried::Float};
    }
    if ((kType == kWorldPositionType && from.output == "position") ||
        (kType == kWorldNormalType && from.output == "normal")) {
        RAWFRAME_TRY(streamIn(*kSource));
        return std::optional{Carried::Vec3};
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
