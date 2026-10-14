#include "surface_graph.h"

#include "common.h"
#include "functions.h"
#include "parameters.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <utility>

namespace rawframe::material::surface_graph {

using document::Value;
using enum Rule;

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

constexpr auto kInInputs = std::to_array<std::string_view>({"in"});
constexpr auto kNameParams = std::to_array<std::string_view>({"name"});
constexpr auto kChannelParams = std::to_array<std::string_view>({"channel"});
constexpr auto kScaleParams = std::to_array<std::string_view>({"scale"});
constexpr auto kPatternParams = std::to_array<std::string_view>({"pattern"});
constexpr auto kOperatorParams = std::to_array<std::string_view>({"operator"});
constexpr auto kABInputs = std::to_array<std::string_view>({"a", "b"});
constexpr auto kXYInputs = std::to_array<std::string_view>({"x", "y"});
constexpr auto kXYZInputs = std::to_array<std::string_view>({"x", "y", "z"});
constexpr auto kWXYZInputs = std::to_array<std::string_view>({"w", "x", "y", "z"});
constexpr auto kOffOnInputs = std::to_array<std::string_view>({"off", "on"});
constexpr auto kConstantParams = std::to_array<std::string_view>({"type", "value"});
constexpr auto kTimeParams = std::to_array<std::string_view>({"period", "timeline"});
constexpr auto kTransformParams = std::to_array<std::string_view>({"from", "kind", "to"});
constexpr auto kSampleParams = std::to_array<std::string_view>({"address", "filter", "texture"});
constexpr auto kSampleInputs = std::to_array<std::string_view>({"texture", "uv"});
constexpr auto kOut = std::to_array<std::string_view>({"out"});
constexpr auto kColorOutputs = std::to_array<std::string_view>({"alpha", "color"});
constexpr auto kUvOutputs = std::to_array<std::string_view>({"uv"});
constexpr auto kRGBOutputs = std::to_array<std::string_view>({"b", "g", "r"});
constexpr auto kRGBAOutputs = std::to_array<std::string_view>({"a", "b", "g", "r"});
constexpr auto kXYOutputs = std::to_array<std::string_view>({"x", "y"});
constexpr auto kSecondsOutputs = std::to_array<std::string_view>({"seconds"});
constexpr auto kPositionOutputs = std::to_array<std::string_view>({"position"});
constexpr auto kNormalOutputs = std::to_array<std::string_view>({"normal"});
constexpr auto kDirectionOutputs = std::to_array<std::string_view>({"direction"});
constexpr auto kTangentOutputs = std::to_array<std::string_view>({"tangent"});
constexpr auto kFrontOutputs = std::to_array<std::string_view>({"front"});
constexpr auto kTextureOutputs = std::to_array<std::string_view>({"texture"});
constexpr auto kInlineParams =
    std::to_array<std::string_view>({"body", "capabilities", "domains", "inputs", "outputs", "stages"});

struct OwnNode {
    std::string_view type;
    Members members;
};

/// The family's own node types, neither a math node's nor a function's,
/// and their members (D731, D732, D733): what `known` takes and
/// `nodeSignatures` lists, but a function's output, which a function alone
/// holds.
constexpr auto kOwnNodes =
    std::to_array<OwnNode>({{kSurfaceType, {}},
                            {kSampleTexture2dType, {kSampleParams, kSampleInputs, kColorOutputs}},
                            {kUvType, {kChannelParams, {}, kUvOutputs}},
                            {kSeparate3Type, {{}, kInInputs, kRGBOutputs}},
                            {kNormalMapType, {kScaleParams, kInInputs, kOut}},
                            {kQualitySwitchType, {{}, kSwitchInputs, kOut}},
                            {kTimeType, {kTimeParams, {}, kSecondsOutputs}},
                            {kWorldPositionType, {{}, {}, kPositionOutputs}},
                            {kWorldNormalType, {{}, {}, kNormalOutputs}},
                            {kCombine3Type, {{}, kXYZInputs, kOut}},
                            {kCombine2Type, {{}, kXYInputs, kOut}},
                            {kSeparate2Type, {{}, kInInputs, kXYOutputs}},
                            {kSwizzleType, {kPatternParams, kInInputs, kOut}},
                            {kVertexColorType, {{}, {}, kColorOutputs}},
                            {kViewDirectionType, {{}, {}, kDirectionOutputs}},
                            {kWorldTangentType, {{}, {}, kTangentOutputs}},
                            {kCompareType, {kOperatorParams, kABInputs, kOut}},
                            {kConstantType, {kConstantParams, {}, kOut}},
                            {kFrontFacingType, {{}, {}, kFrontOutputs}},
                            {kCombine4Type, {{}, kWXYZInputs, kOut}},
                            {kSeparate4Type, {{}, kInInputs, kRGBAOutputs}},
                            {kParameterType, {kNameParams, {}, kOut}},
                            {kParameterTexture2dType, {kNameParams, {}, kTextureOutputs}},
                            {kTransformType, {kTransformParams, kInInputs, kOut}},
                            {kInlineSlangType, {kInlineParams, {}, {}}},
                            {kStaticSwitchType, {kNameParams, kOffOnInputs, kOut}}});

} // namespace

std::optional<Members> ownMembersOf(std::string_view type) {
    const auto kFound = std::ranges::find(kOwnNodes, type, &OwnNode::type);
    return kFound == kOwnNodes.end() ? std::nullopt : std::optional{kFound->members};
}

result::Result<std::pair<const Value*, const Value*>> ownPartsOf(const graph::Node& node) {
    const std::optional<Members> kMembers = ownMembersOf(typeOf(node));
    return kMembers.has_value() ? partsOf(node, kMembers->params, kMembers->inputs)
                                : invalid("a node of the library is of a type it holds");
}

bool known(const graph::Node& node) {
    const Value* kType = node.record.find("type");
    const std::string* kText = kType != nullptr ? kType->text() : nullptr;
    return kText != nullptr &&
           (ownMembersOf(*kText).has_value() || *kText == kOutputType || functions::isFunction(*kText) || math(*kText));
}

bool math(std::string_view type) {
    return mathNodeOf(type) != nullptr;
}

std::string_view typeOf(const graph::Node& node) {
    return *node.record.find("type")->text();
}

result::Result<std::uint32_t> uvIn(const graph::Node& node) {
    RAWFRAME_TRY_ASSIGN(const auto kParts, ownPartsOf(node));
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

result::Result<Operand> operandOf(const Graph& surface, const Value& value) {
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
    if (const std::optional<bool> kTruth = value.truth(); kTruth.has_value()) {
        made.literal[0] = *kTruth ? 1 : 0;
        made.carried = Carried::Bool;
        return made;
    }
    const std::size_t kCount = value.kind() == Value::Kind::Array ? value.items().size() : 0;
    if (kCount < 2 || kCount > 4) {
        return invalid("a math node's literal is a number, two to four of them, or a bool");
    }
    for (std::size_t at = 0; at < kCount; ++at) {
        const std::optional<double> kChannel = kFinite(value.items()[at]);
        if (!kChannel.has_value()) {
            return invalid("a math node's literal is a number, or two to four of them");
        }
        made.literal.at(at) = *kChannel;
    }
    made.carried = kCount == 2 ? Carried::Vec2 : kCount == 3 ? Carried::Color3 : Carried::Color4;
    return made;
}

result::Result<MathInputs> mathInputsOf(const Graph& surface, const graph::Node& node) {
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
    // Three numbers are a color3 or a vec3, and four a color4 or a vec4, as
    // a connected input is.
    const auto kAny = [&kIn](Carried carried) {
        return std::ranges::any_of(kIn, [carried](const Operand& each) {
            return each.carried == carried;
        });
    };
    const bool kVector3 = kAny(Carried::Vec3);
    const bool kVector4 = kAny(Carried::Vec4);
    for (Operand& each : kIn) {
        if (!each.from.has_value() && each.carried == Carried::Color3 && kVector3) {
            each.carried = Carried::Vec3;
        }
        if (!each.from.has_value() && each.carried == Carried::Color4 && kVector4) {
            each.carried = Carried::Vec4;
        }
    }
    if (std::ranges::any_of(kIn, [](const Operand& each) {
            return each.carried == Carried::Texture2d;
        })) {
        return invalid("a texture input's texture is sampled, never computed with");
    }
    // A whole literal beside an int is an int (D715).
    if (kAny(Carried::Int)) {
        for (Operand& each : kIn) {
            if (!each.from.has_value() && each.carried == Carried::Float && wholeNumber(each.literal[0])) {
                each.carried = Carried::Int;
            }
        }
    }
    const bool kKnown = std::ranges::all_of(kIn, [](const Operand& each) {
        return each.carried.has_value();
    });
    if (!kKnown) {
        return made;
    }
    const auto kIsVector = [](Carried carried) {
        return carried == Carried::Vec2 || carried == Carried::Vec3 || carried == Carried::Vec4;
    };
    switch (kNode.rule) {
    case Logical:
        if (!std::ranges::all_of(kIn, [](const Operand& each) {
                return each.carried == Carried::Bool;
            })) {
            return invalid("a logic node's inputs are bools");
        }
        made.out = Carried::Bool;
        break;
    case Choice:
    case Componentwise: {
        const bool kChoice = kNode.rule == Choice;
        if (kChoice && *kIn[0].carried != Carried::Bool) {
            return invalid("a select node's condition is a bool");
        }
        std::optional<Carried> kind;
        for (const Operand& each : kIn.subspan(kChoice ? 1 : 0)) {
            if (*each.carried == Carried::Bool) {
                return invalid("a bool feeds a logic node or a select's condition alone");
            }
            if (*each.carried == Carried::Float) {
                continue;
            }
            if (kind.has_value() && *kind != *each.carried) {
                return invalid("a math node's inputs are of one type, or floats");
            }
            kind = each.carried;
        }
        // An int is computed with ints alone, by the nodes that take them
        // (D715).
        if (kind == Carried::Int) {
            if (!kChoice && !wholeCodeOf(kNode.type).has_value()) {
                return invalid("an int is computed with by the nodes that take ints alone");
            }
            if (std::ranges::any_of(kIn.subspan(kChoice ? 1 : 0), [](const Operand& each) {
                    return each.carried == Carried::Float;
                })) {
                return invalid("an int is computed with ints alone");
            }
        }
        made.out = kind.value_or(Carried::Float);
        break;
    }
    case Noise2:
    case Noise3:
        if (*kIn[0].carried != (kNode.rule == Noise2 ? Carried::Vec2 : Carried::Vec3)) {
            return invalid("a 2D noise's input is a vec2, a 3D noise's a vec3");
        }
        made.out = Carried::Float;
        break;
    case Tone:
    case Recolor:
        if (*kIn[0].carried != Carried::Color3) {
            return invalid("a luminance or HSV node's input is a color3");
        }
        made.out = kNode.rule == Tone ? Carried::Float : Carried::Color3;
        break;
    case Measure:
    case Unit:
        if (!kIsVector(*kIn[0].carried)) {
            return invalid("a length or normalize node's input is a vector");
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

result::Result<Math> mathIn(const Graph& surface, const graph::Node& node) {
    RAWFRAME_TRY_ASSIGN(const MathInputs kInputs, mathInputsOf(surface, node));
    return Math{.a = kInputs.inputs[0], .b = kInputs.inputs[1], .out = kInputs.out};
}

result::Result<Combined> combineIn(const Graph& surface, const graph::Node& node) {
    // In name order: w before x.
    constexpr std::array<std::string_view, 4> kInputs = {"x", "y", "z", "w"};
    Combined made{.count = typeOf(node) == kCombine2Type ? 2U : typeOf(node) == kCombine3Type ? 3U : 4U};
    RAWFRAME_TRY_ASSIGN(const auto kParts, ownPartsOf(node));
    for (std::size_t at = 0; at < made.count; ++at) {
        const Value* kInput = kParts.second->find(kInputs.at(at));
        if (kInput == nullptr) {
            return invalid("a combine node's inputs are x, y, for three z, for four w too, all given");
        }
        RAWFRAME_TRY_ASSIGN(made.parts.at(at), operandOf(surface, *kInput));
        if (made.parts.at(at).carried.has_value() && *made.parts.at(at).carried != Carried::Float) {
            return invalid("a combine node's inputs are floats");
        }
    }
    return made;
}

result::Result<Operand> separate2In(const Graph& surface, const graph::Node& node) {
    RAWFRAME_TRY_ASSIGN(const auto kParts, ownPartsOf(node));
    const Value* kIn = kParts.second->find("in");
    if (kIn == nullptr) {
        return invalid("a separate2 node's input is in, given");
    }
    RAWFRAME_TRY_ASSIGN(Operand made, operandOf(surface, *kIn));
    if (made.carried.has_value() && *made.carried != Carried::Vec2) {
        return invalid("a separate2 node's input is a vec2");
    }
    return made;
}

result::Result<Swizzle> swizzleIn(const Graph& surface, const graph::Node& node) {
    RAWFRAME_TRY_ASSIGN(const auto kParts, ownPartsOf(node));
    const Value* kPattern = kParts.first->find("pattern");
    const Value* kIn = kParts.second->find("in");
    if (kPattern == nullptr || kPattern->text() == nullptr || kIn == nullptr) {
        return invalid("a swizzle node's pattern and input are given");
    }
    Swizzle made{.pattern = *kPattern->text()};
    RAWFRAME_TRY_ASSIGN(made.in, operandOf(surface, *kIn));
    const std::optional<Carried>& kFrom = made.in.carried;
    const bool kFour = kFrom == Carried::Vec4 || kFrom == Carried::Color4;
    const std::size_t kWidth =
        !kFrom.has_value()                                                                                          ? 4
        : kFrom == Carried::Vec2                                                                                    ? 2
        : kFrom == Carried::Float || kFrom == Carried::Bool || kFrom == Carried::Texture2d || kFrom == Carried::Int ? 0
        : kFour                                                                                                     ? 4
                                                                                                                    : 3;
    // x, y, z, and w: the channel each letter names.
    const auto kChannel = [](char letter) -> std::size_t {
        return letter == 'w' ? 3 : letter >= 'x' && letter <= 'z' ? static_cast<std::size_t>(letter - 'x') : 4;
    };
    if (made.pattern.empty() || made.pattern.size() > 4 || kWidth == 0 ||
        !std::ranges::all_of(made.pattern, [&kChannel, kWidth](char letter) {
            return kChannel(letter) < kWidth;
        })) {
        return invalid("a swizzle's pattern is one to four of x, y, z, and w, within its input's width");
    }
    if (kFrom.has_value()) {
        const bool kColor = kFrom == Carried::Color3 || kFrom == Carried::Color4;
        made.out = made.pattern.size() == 1   ? Carried::Float
                   : made.pattern.size() == 2 ? Carried::Vec2
                   : made.pattern.size() == 3 ? (kColor ? Carried::Color3 : Carried::Vec3)
                                              : (kColor ? Carried::Color4 : Carried::Vec4);
    }
    return made;
}

result::Result<Operand> constantIn(const graph::Node& node) {
    constexpr std::array<std::pair<std::string_view, Carried>, 8> kTypes = {{{"float", Carried::Float},
                                                                             {"int", Carried::Int},
                                                                             {"bool", Carried::Bool},
                                                                             {"vec2", Carried::Vec2},
                                                                             {"vec3", Carried::Vec3},
                                                                             {"color3", Carried::Color3},
                                                                             {"vec4", Carried::Vec4},
                                                                             {"color4", Carried::Color4}}};
    RAWFRAME_TRY_ASSIGN(const auto kParts, ownPartsOf(node));
    const Value* kType = kParts.first->find("type");
    const Value* kValue = kParts.first->find("value");
    if (kType == nullptr || kType->text() == nullptr || kValue == nullptr) {
        return invalid("a constant node declares its type and its value");
    }
    const std::string_view kName = *kType->text();
    const auto kFound = std::ranges::find(kTypes, kName, &std::pair<std::string_view, Carried>::first);
    if (kFound == kTypes.end()) {
        return invalid("a constant's type is one of the graph's types");
    }
    // Its value read as an input's literal is, then held to the declared
    // shape: a literal of three or four numbers reads as a color either way.
    const graph::Document kNone;
    RAWFRAME_TRY_ASSIGN(Operand made, operandOf(Graph{kNone}, *kValue));
    // An int's a whole number an int holds (D715).
    if (kFound->second == Carried::Int && made.carried == Carried::Float && wholeNumber(made.literal[0])) {
        made.carried = Carried::Int;
    }
    const Carried kRead = kFound->second == Carried::Vec3   ? Carried::Color3
                          : kFound->second == Carried::Vec4 ? Carried::Color4
                                                            : kFound->second;
    if (made.carried != kRead) {
        return invalid("a constant's value is of its declared type");
    }
    made.carried = kFound->second;
    return made;
}

result::Result<Compare> compareIn(const Graph& surface, const graph::Node& node) {
    constexpr std::array<std::pair<std::string_view, std::string_view>, 6> kOperators = {{{"less", "<"},
                                                                                          {"less_equal", "<="},
                                                                                          {"greater", ">"},
                                                                                          {"greater_equal", ">="},
                                                                                          {"equal", "=="},
                                                                                          {"not_equal", "!="}}};
    RAWFRAME_TRY_ASSIGN(const auto kParts, ownPartsOf(node));
    const Value* kOperator = kParts.first->find("operator");
    const auto kFound = kOperator != nullptr && kOperator->text() != nullptr
                            ? std::ranges::find(kOperators,
                                                std::string_view{*kOperator->text()},
                                                &std::pair<std::string_view, std::string_view>::first)
                            : kOperators.end();
    const Value* kA = kParts.second->find("a");
    const Value* kB = kParts.second->find("b");
    if (kFound == kOperators.end() || kA == nullptr || kB == nullptr) {
        return invalid("a compare node declares its operator, and its inputs a and b are given");
    }
    Compare made{.symbol = kFound->second};
    RAWFRAME_TRY_ASSIGN(made.a, operandOf(surface, *kA));
    RAWFRAME_TRY_ASSIGN(made.b, operandOf(surface, *kB));
    // Two floats, or two ints, a whole literal beside an int one (D715).
    const bool kWhole = made.a.carried == Carried::Int || made.b.carried == Carried::Int;
    for (Operand* each : {&made.a, &made.b}) {
        if (kWhole && !each->from.has_value() && each->carried == Carried::Float && wholeNumber(each->literal[0])) {
            each->carried = Carried::Int;
        }
        if (each->carried.has_value() && *each->carried != (kWhole ? Carried::Int : Carried::Float)) {
            return invalid("a compare node's inputs are floats, or ints");
        }
    }
    return made;
}

result::Result<std::uint32_t> timeIn(const graph::Node& node) {
    RAWFRAME_TRY_ASSIGN(const auto kParts, ownPartsOf(node));
    const Value* kTimeline = kParts.first->find("timeline");
    if (kTimeline == nullptr || kTimeline->text() == nullptr || *kTimeline->text() != "simulation") {
        return invalid("a time node names its timeline, the simulation's");
    }
    const Value* kPeriod = kParts.first->find("period");
    if (kPeriod == nullptr) {
        return 0U;
    }
    // Whole seconds below 2^24, each a float exactly.
    const std::optional<std::int64_t> kSeconds =
        kPeriod->kind() == Value::Kind::Number ? kPeriod->integer() : std::nullopt;
    if (!kSeconds.has_value() || *kSeconds < 1 || *kSeconds >= (std::int64_t{1} << 24)) {
        return invalid("a time node's period is whole seconds, from one to below 2^24");
    }
    return static_cast<std::uint32_t>(*kSeconds);
}

result::Status streamIn(const graph::Node& node) {
    RAWFRAME_TRY(ownPartsOf(node));
    return {};
}

result::Result<Switch> switchIn(const Graph& surface, const graph::Node& node) {
    RAWFRAME_TRY_ASSIGN(const auto kParts, ownPartsOf(node));
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

result::Result<Transform> transformIn(const Graph& surface, const graph::Node& node) {
    constexpr std::array<std::string_view, 5> kSpaces = {"object", "world", "view", "tangent", "clip"};
    constexpr std::array<std::string_view, 3> kKinds = {"point", "vector", "normal"};
    RAWFRAME_TRY_ASSIGN(const auto kParts, ownPartsOf(node));
    const auto kOneOf = [&kParts](std::string_view name, std::span<const std::string_view> among) {
        const Value* kValue = kParts.first->find(name);
        const auto kFound = kValue != nullptr && kValue->text() != nullptr
                                ? std::ranges::find(among, std::string_view{*kValue->text()})
                                : among.end();
        return kFound != among.end() ? *kFound : std::string_view{};
    };
    Transform made{.from = kOneOf("from", kSpaces), .to = kOneOf("to", kSpaces), .kind = kOneOf("kind", kKinds)};
    const Value* kIn = kParts.second->find("in");
    if (made.from.empty() || made.to.empty() || made.kind.empty() || kIn == nullptr) {
        return invalid("a transform declares its spaces, from and to, and its kind, and its input in is given");
    }
    if (made.from == made.to) {
        return invalid("a transform's spaces differ");
    }
    if (made.kind == "point" && (made.from == "tangent" || made.to == "tangent")) {
        return invalid("a point has no place in the tangent frame");
    }
    if (made.to == "clip" && made.kind != "point") {
        return invalid("only a point goes to the clip's space");
    }
    if (made.from == "clip") {
        return unsupported("a transform from the clip's space waits in generation 1");
    }
    RAWFRAME_TRY_ASSIGN(made.in, operandOf(surface, *kIn));
    if (!made.in.from.has_value() && made.in.carried == Carried::Color3) {
        made.in.carried = Carried::Vec3;
    }
    if (made.in.carried.has_value() && *made.in.carried != Carried::Vec3) {
        return invalid("a transform's input is a vec3");
    }
    return made;
}

result::Result<Operand> separate3In(const Graph& surface, const graph::Node& node) {
    RAWFRAME_TRY_ASSIGN(const auto kParts, ownPartsOf(node));
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

result::Result<Operand> separate4In(const Graph& surface, const graph::Node& node) {
    RAWFRAME_TRY_ASSIGN(const auto kParts, ownPartsOf(node));
    const Value* kIn = kParts.second->find("in");
    if (kIn == nullptr) {
        return invalid("a separate4 node's input is in, given");
    }
    RAWFRAME_TRY_ASSIGN(Operand made, operandOf(surface, *kIn));
    if (made.carried.has_value() && *made.carried != Carried::Color4 && *made.carried != Carried::Vec4) {
        return invalid("a separate4 node's input is a color4 or a vec4");
    }
    return made;
}

result::Result<NormalMap> normalMapIn(const Graph& surface, const graph::Node& node) {
    RAWFRAME_TRY_ASSIGN(const auto kParts, ownPartsOf(node));
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

result::Result<Sampling> sampleIn(const Graph& surface, const graph::Node& node) {
    RAWFRAME_TRY_ASSIGN(const auto kParts, ownPartsOf(node));
    Sampling made;
    const Value* kTexture = kParts.first->find("texture");
    const Value* kGiven = kParts.second->find("texture");
    if ((kTexture == nullptr) == (kGiven == nullptr)) {
        return invalid("a sampled texture's texture is its texture param's or a texture input's, one of them");
    }
    if (kGiven != nullptr) {
        // A texture input's, by a parameter_texture_2d node (D710).
        const std::optional<graph::Connection> kFrom = graph::connectionOf(*kGiven);
        const graph::Node* kSource = kFrom.has_value() ? nodeOf(surface, kFrom->node) : nullptr;
        if (kSource == nullptr || !known(*kSource) || typeOf(*kSource) != kParameterTexture2dType ||
            kFrom->output != "texture") {
            return invalid("a sampled texture's texture input is a parameter_texture_2d node's texture");
        }
        RAWFRAME_TRY_ASSIGN(const InterfaceInput kInput, parameterIn(surface, *kSource));
        made.texture.id = kInput.texture;
        made.input = kInput.slot;
    } else {
        const std::optional<std::uint64_t> kId =
            kTexture->text() != nullptr ? graph::nodeIdOf(*kTexture->text()) : std::nullopt;
        if (!kId.has_value() || *kId == 0) {
            return invalid("a sampled texture names the game's texture by 16 lowercase hex digits, never nought");
        }
        made.texture.id = *kId;
    }
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

result::Status nodeChecked(const Graph& surface, const graph::Node& node) {
    if (typeOf(node) == kUvType) {
        RAWFRAME_TRY(uvIn(node));
    } else if (math(typeOf(node))) {
        RAWFRAME_TRY(mathIn(surface, node));
    } else if (typeOf(node) == kSeparate3Type) {
        RAWFRAME_TRY(separate3In(surface, node));
    } else if (typeOf(node) == kNormalMapType) {
        RAWFRAME_TRY(normalMapIn(surface, node));
    } else if (typeOf(node) == kQualitySwitchType) {
        RAWFRAME_TRY(switchIn(surface, node));
    } else if (typeOf(node) == kCombine3Type || typeOf(node) == kCombine2Type || typeOf(node) == kCombine4Type) {
        RAWFRAME_TRY(combineIn(surface, node));
    } else if (typeOf(node) == kParameterType || typeOf(node) == kParameterTexture2dType) {
        RAWFRAME_TRY(parameterIn(surface, node));
    } else if (typeOf(node) == kSeparate4Type) {
        RAWFRAME_TRY(separate4In(surface, node));
    } else if (typeOf(node) == kCompareType) {
        RAWFRAME_TRY(compareIn(surface, node));
    } else if (typeOf(node) == kTransformType) {
        RAWFRAME_TRY(transformIn(surface, node));
    } else if (typeOf(node) == kInlineSlangType) {
        RAWFRAME_TRY(inlineIn(surface, node));
    } else if (typeOf(node) == kStaticSwitchType) {
        RAWFRAME_TRY(staticSwitchIn(surface, node));
    } else if (typeOf(node) == kConstantType) {
        RAWFRAME_TRY(constantIn(node));
    } else if (typeOf(node) == kSeparate2Type) {
        RAWFRAME_TRY(separate2In(surface, node));
    } else if (typeOf(node) == kSwizzleType) {
        RAWFRAME_TRY(swizzleIn(surface, node));
    } else if (typeOf(node) == kTimeType) {
        RAWFRAME_TRY(timeIn(node));
    } else if (typeOf(node) == kWorldPositionType || typeOf(node) == kWorldNormalType ||
               typeOf(node) == kVertexColorType || typeOf(node) == kViewDirectionType ||
               typeOf(node) == kWorldTangentType || typeOf(node) == kFrontFacingType) {
        RAWFRAME_TRY(streamIn(node));
    } else if (functions::isFunction(typeOf(node))) {
        RAWFRAME_TRY(functions::callIn(surface, node));
    } else if (typeOf(node) == kOutputType) {
        return invalid("an output node is a function's, feeding one of its interface's outputs");
    } else {
        RAWFRAME_TRY(sampleIn(surface, node));
    }
    return {};
}

namespace {

result::Result<std::optional<Carried>> findCarried(const Graph& surface, const graph::Connection& from);

} // namespace

std::optional<std::optional<Carried>> Graph::found(const graph::Connection& from) const {
    const auto kFound = found_.find(std::pair{from.node, from.output});
    if (kFound == found_.end()) {
        return std::nullopt;
    }
    return std::optional<std::optional<Carried>>{std::in_place, kFound->second};
}

void Graph::remember(const graph::Connection& from, std::optional<Carried> carried) const {
    found_.emplace(std::pair{from.node, from.output}, carried);
}

result::Result<std::optional<Carried>> carriedBy(const Graph& surface, const graph::Connection& from) {
    if (const auto kFound = surface.found(from); kFound.has_value()) {
        return *kFound;
    }
    RAWFRAME_TRY_ASSIGN(const std::optional<Carried> kCarried, findCarried(surface, from));
    surface.remember(from, kCarried);
    return kCarried;
}

namespace {
/// What `from` carries, found from its node.
result::Result<std::optional<Carried>> findCarried(const Graph& surface, const graph::Connection& from) {
    const graph::Node* kSource = nodeOf(surface, from.node);
    if (kSource == nullptr || !known(*kSource)) {
        return std::optional<Carried>{};
    }
    const std::string_view kType = typeOf(*kSource);
    // An own node's outputs are its type's (D733); an inline node's as it
    // declares, and a call's as its function's.
    if (const std::optional<Members> kOwn = ownMembersOf(kType);
        kOwn.has_value() && kType != kInlineSlangType && !std::ranges::contains(kOwn->outputs, from.output)) {
        return invalid("a connection names an output its node has");
    }
    if (kType == kSampleTexture2dType && from.output == "color") {
        return std::optional{Carried::Color3};
    }
    if (kType == kSampleTexture2dType && from.output == "alpha") {
        return std::optional{Carried::Float};
    }
    if (kType == kUvType) {
        return std::optional{Carried::Vec2};
    }
    if (math(kType) && from.output == "out") {
        RAWFRAME_TRY_ASSIGN(const MathInputs kMath, mathInputsOf(surface, *kSource));
        return kMath.out;
    }
    if (kType == kCombine3Type || kType == kCombine2Type || kType == kCombine4Type) {
        RAWFRAME_TRY_ASSIGN(const Combined kCombined, combineIn(surface, *kSource));
        return std::optional{kCombined.count == 2   ? Carried::Vec2
                             : kCombined.count == 3 ? Carried::Vec3
                                                    : Carried::Vec4};
    }
    if (kType == kSeparate4Type) {
        RAWFRAME_TRY(separate4In(surface, *kSource));
        return std::optional{Carried::Float};
    }
    if (kType == kSeparate2Type) {
        RAWFRAME_TRY(separate2In(surface, *kSource));
        return std::optional{Carried::Float};
    }
    if (kType == kStaticSwitchType) {
        RAWFRAME_TRY_ASSIGN(const SwitchChoice kChoice, staticSwitchIn(surface, *kSource));
        return kChoice.out;
    }
    if (kType == kInlineSlangType) {
        // An inline node's output is as it declares (D718).
        RAWFRAME_TRY_ASSIGN(const Inline kInline, inlineIn(surface, *kSource));
        const auto kOutput = std::ranges::find(kInline.outputs, from.output, &Pin::name);
        if (kOutput != kInline.outputs.end()) {
            return std::optional{kOutput->carried};
        }
    }
    if (kType == kTransformType) {
        RAWFRAME_TRY_ASSIGN(const Transform kTransform, transformIn(surface, *kSource));
        return std::optional{kTransform.to == "clip" ? Carried::Vec4 : Carried::Vec3};
    }
    if (kType == kCompareType) {
        RAWFRAME_TRY(compareIn(surface, *kSource));
        return std::optional{Carried::Bool};
    }
    if (functions::isFunction(kType)) {
        // A call's output is its function's (D711).
        RAWFRAME_TRY_ASSIGN(const functions::Call kCall, functions::callIn(surface, *kSource));
        const auto kOutput = std::ranges::find(kCall.function.outputs, from.output, &functions::Output::name);
        if (kOutput != kCall.function.outputs.end()) {
            return std::optional{kOutput->carried};
        }
    }
    if (kType == kParameterTexture2dType) {
        RAWFRAME_TRY(parameterIn(surface, *kSource));
        return std::optional{Carried::Texture2d};
    }
    if (kType == kParameterType) {
        RAWFRAME_TRY_ASSIGN(const InterfaceInput kInput, parameterIn(surface, *kSource));
        return std::optional{kInput.carried};
    }
    if (kType == kFrontFacingType) {
        RAWFRAME_TRY(streamIn(*kSource));
        return std::optional{Carried::Bool};
    }
    if (kType == kConstantType) {
        RAWFRAME_TRY_ASSIGN(const Operand kConstant, constantIn(*kSource));
        return kConstant.carried;
    }
    if (kType == kSwizzleType) {
        RAWFRAME_TRY_ASSIGN(const Swizzle kSwizzle, swizzleIn(surface, *kSource));
        return kSwizzle.out;
    }
    if (kType == kVertexColorType) {
        RAWFRAME_TRY(streamIn(*kSource));
        return std::optional{from.output == "color" ? Carried::Color3 : Carried::Float};
    }
    if (kType == kTimeType) {
        RAWFRAME_TRY(timeIn(*kSource));
        return std::optional{Carried::Float};
    }
    if (kType == kWorldPositionType || kType == kWorldNormalType || kType == kViewDirectionType ||
        kType == kWorldTangentType) {
        RAWFRAME_TRY(streamIn(*kSource));
        return std::optional{Carried::Vec3};
    }
    if (kType == kNormalMapType) {
        RAWFRAME_TRY(normalMapIn(surface, *kSource));
        return std::optional{Carried::Vec3};
    }
    if (kType == kQualitySwitchType) {
        RAWFRAME_TRY_ASSIGN(const Switch kSwitch, switchIn(surface, *kSource));
        return kSwitch.out;
    }
    if (kType == kSeparate3Type) {
        RAWFRAME_TRY(separate3In(surface, *kSource));
        return std::optional{Carried::Float};
    }
    return invalid("a connection names an output its node has");
}
} // namespace

} // namespace rawframe::material::surface_graph

namespace rawframe::material {

std::vector<NodeSignature> nodeSignatures() {
    const auto kWords = [](std::span<const std::string_view> names) {
        return std::vector<std::string>{names.begin(), names.end()};
    };
    std::vector<NodeSignature> made;
    for (const surface_graph::OwnNode& kOwn : surface_graph::kOwnNodes) {
        made.push_back(NodeSignature{.type = std::string{kOwn.type},
                                     .params = kWords(kOwn.members.params),
                                     .inputs = kWords(kOwn.members.inputs),
                                     .outputs = kWords(kOwn.members.outputs)});
    }
    // The surface's are the Surface contract's: its parameters, its feature
    // groups' inputs, and the geometry's normal and position offset.
    std::vector<std::string>& surface = made.front().inputs;
    for (const Parameter& kParameter : kParameters) {
        surface.emplace_back(kParameter.name);
    }
    for (const GroupInput& kGroup : kGroupInputs) {
        surface.emplace_back(kGroup.name);
    }
    surface.emplace_back("geometry_normal");
    surface.emplace_back("position_offset");
    std::ranges::sort(surface);
    for (const surface_graph::MathNode& kMath : surface_graph::mathNodes()) {
        NodeSignature math{.type = std::string{kMath.type},
                           .inputs = kWords(std::span{kMath.inputs.data(), kMath.count}),
                           .outputs = {"out"}};
        std::ranges::sort(math.inputs);
        made.push_back(std::move(math));
    }
    for (const std::string_view kType : functions::types()) {
        NodeSignature call{.type = std::string{kType}};
        if (const result::Result<functions::Function> kFunction = functions::functionOf(kType); kFunction.has_value()) {
            for (const surface_graph::InterfaceInput& kInput : kFunction->inputs) {
                call.inputs.emplace_back(kInput.name);
            }
            for (const functions::Output& kOutput : kFunction->outputs) {
                call.outputs.emplace_back(kOutput.name);
            }
            std::ranges::sort(call.outputs);
        }
        made.push_back(std::move(call));
    }
    return made;
}

} // namespace rawframe::material
