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
/// HSV with hue, saturation, and value each from nought to one, as
/// MaterialX's color nodes take it (D698).
constexpr std::string_view kRgbToHsv = "float3 rawframeRgbToHsv(float3 c)\n"
                                       "{\n"
                                       "    const float4 k = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);\n"
                                       "    const float4 p = c.g < c.b ? float4(c.bg, k.wz) : float4(c.gb, k.xy);\n"
                                       "    const float4 q = c.r < p.x ? float4(p.xyw, c.r) : float4(c.r, p.yzx);\n"
                                       "    const float d = q.x - min(q.w, q.y);\n"
                                       "    return float3(abs(q.z + (q.w - q.y) / (6.0 * d + 1.0e-10)), d / (q.x + "
                                       "1.0e-10), q.x);\n"
                                       "}\n";
constexpr std::string_view kHsvToRgb = "float3 rawframeHsvToRgb(float3 c)\n"
                                       "{\n"
                                       "    const float3 p = abs(frac(c.xxx + float3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 "
                                       "- 3.0);\n"
                                       "    return c.z * lerp(float3(1.0), saturate(p - 1.0), c.y);\n"
                                       "}\n";

/// The noise kernels' hash (D699): integers only, so every backend gives
/// the same bits; a cell's value from nought to one.
constexpr std::string_view kHash = "uint rawframeHash(uint x)\n"
                                   "{\n"
                                   "    x ^= x >> 16u;\n"
                                   "    x *= 0x7feb352du;\n"
                                   "    x ^= x >> 15u;\n"
                                   "    x *= 0x846ca68bu;\n"
                                   "    x ^= x >> 16u;\n"
                                   "    return x;\n"
                                   "}\n\n"
                                   "float rawframeCell2(int2 c)\n"
                                   "{\n"
                                   "    return float(rawframeHash(uint(c.x) + rawframeHash(uint(c.y))) >> 8u) / "
                                   "16777216.0;\n"
                                   "}\n";
/// Value noise: the cells' values eased between, from nought to one.
constexpr std::string_view kValueNoise2 =
    "float rawframeValueNoise2(float2 p)\n"
    "{\n"
    "    const float2 i = floor(p);\n"
    "    const float2 f = p - i;\n"
    "    const float2 u = f * f * (3.0 - 2.0 * f);\n"
    "    const int2 c = int2(i);\n"
    "    return lerp(lerp(rawframeCell2(c), rawframeCell2(c + int2(1, 0)), u.x),\n"
    "                lerp(rawframeCell2(c + int2(0, 1)), rawframeCell2(c + "
    "int2(1, 1)), u.x), u.y);\n"
    "}\n";
/// Simplex noise in two dimensions, a gradient a corner, about minus one
/// to one.
constexpr std::string_view kSimplex2 =
    "float rawframeCorner2(int2 c, float2 x)\n"
    "{\n"
    "    const float t = max(0.5 - dot(x, x), 0.0);\n"
    "    const float a = rawframeCell2(c) * 6.2831853;\n"
    "    return t * t * t * t * dot(float2(cos(a), sin(a)), x);\n"
    "}\n\n"
    "float rawframeSimplex2(float2 p)\n"
    "{\n"
    "    const float2 s = floor(p + (p.x + p.y) * 0.36602540378);\n"
    "    const float2 x0 = p - s + (s.x + s.y) * 0.2113248654;\n"
    "    const float2 o = x0.x > x0.y ? float2(1.0, 0.0) : float2(0.0, 1.0);\n"
    "    const float2 x1 = x0 - o + 0.2113248654;\n"
    "    const float2 x2 = x0 - 1.0 + 2.0 * 0.2113248654;\n"
    "    const int2 c = int2(s);\n"
    "    return 70.0 * (rawframeCorner2(c, x0) + rawframeCorner2(c + int2(o), x1) +\n"
    "                   rawframeCorner2(c + int2(1, 1), x2));\n"
    "}\n";
/// Simplex noise in three dimensions, one of twelve edge gradients a
/// corner, about minus one to one.
constexpr std::string_view kSimplex3 =
    "float rawframeCorner3(int3 c, float3 x)\n"
    "{\n"
    "    const float t = max(0.6 - dot(x, x), 0.0);\n"
    "    const uint h = rawframeHash(uint(c.x) + rawframeHash(uint(c.y) + rawframeHash(uint(c.z)))) % 12u;\n"
    "    const float u = h < 8u ? x.x : x.y;\n"
    "    const float v = h < 4u ? x.y : h == 8u || h == 10u ? x.x : x.z;\n"
    "    return t * t * t * t * (((h & 1u) != 0u ? -u : u) + ((h & 2u) != 0u ? -v : v));\n"
    "}\n\n"
    "float rawframeSimplex3(float3 p)\n"
    "{\n"
    "    const float3 s = floor(p + dot(p, float3(1.0 / 3.0)));\n"
    "    const float3 x0 = p - s + dot(s, float3(1.0 / 6.0));\n"
    "    const float3 g = step(x0.yzx, x0.xyz);\n"
    "    const float3 l = 1.0 - g;\n"
    "    const float3 i1 = min(g, l.zxy);\n"
    "    const float3 i2 = max(g, l.zxy);\n"
    "    const int3 c = int3(s);\n"
    "    return 32.0 * (rawframeCorner3(c, x0) + rawframeCorner3(c + int3(i1), x0 - i1 + 1.0 / 6.0) +\n"
    "                   rawframeCorner3(c + int3(i2), x0 - i2 + 1.0 / 3.0) +\n"
    "                   rawframeCorner3(c + int3(1, 1, 1), x0 - 0.5));\n"
    "}\n";

constexpr std::array<MathNode, 53> kMathNodes = {{
    {"rawframe/luminance@1", {"in"}, 1, Tone, "dot($0, float3(0.2126, 0.7152, 0.0722))", false, {}},
    {"rawframe/rgb_to_hsv@1", {"in"}, 1, Recolor, "rawframeRgbToHsv($0)", false, {kRgbToHsv}},
    {"rawframe/hsv_to_rgb@1", {"in"}, 1, Recolor, "rawframeHsvToRgb($0)", false, {kHsvToRgb}},
    {"rawframe/value_noise_2d@1", {"in"}, 1, Noise2, "rawframeValueNoise2($0)", false, {kHash, kValueNoise2}},
    {"rawframe/simplex_noise_2d@1", {"in"}, 1, Noise2, "rawframeSimplex2($0)", false, {kHash, kSimplex2}},
    {"rawframe/simplex_noise_3d@1", {"in"}, 1, Noise3, "rawframeSimplex3($0)", false, {kHash, kSimplex3}},
    {"rawframe/cell_noise_2d@1", {"in"}, 1, Noise2, "rawframeCell2(int2(floor($0)))", false, {kHash}},
    {"rawframe/and@1", {"a", "b"}, 2, Logical, "$0 && $1", false},
    {"rawframe/or@1", {"a", "b"}, 2, Logical, "$0 || $1", false},
    {"rawframe/xor@1", {"a", "b"}, 2, Logical, "$0 != $1", false},
    {"rawframe/not@1", {"in"}, 1, Logical, "!($0)", false},
    {"rawframe/select@1", {"condition", "a", "b"}, 3, Choice, "($0) ? ($1) : ($2)", true},
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
    return kText != nullptr && (*kText == kSurfaceType || *kText == kSampleTexture2dType || *kText == kUvType ||
                                *kText == kSeparate3Type || *kText == kNormalMapType || *kText == kQualitySwitchType ||
                                *kText == kTimeType || *kText == kWorldPositionType || *kText == kWorldNormalType ||
                                *kText == kCombine3Type || *kText == kCombine2Type || *kText == kSeparate2Type ||
                                *kText == kSwizzleType || *kText == kVertexColorType || *kText == kViewDirectionType ||
                                *kText == kWorldTangentType || *kText == kCompareType || math(*kText));
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
    if (const std::optional<bool> kTruth = value.truth(); kTruth.has_value()) {
        made.literal[0] = *kTruth ? 1 : 0;
        made.carried = Carried::Bool;
        return made;
    }
    const std::size_t kCount = value.kind() == Value::Kind::Array ? value.items().size() : 0;
    if (kCount != 2 && kCount != 3) {
        return invalid("a math node's literal is a number, two or three of them, or a bool");
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

result::Result<Combined> combineIn(const graph::Document& surface, const graph::Node& node) {
    constexpr std::array<std::string_view, 3> kInputs = {"x", "y", "z"};
    Combined made{.count = typeOf(node) == kCombine2Type ? 2U : 3U};
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, {}, std::span{kInputs.data(), made.count}));
    for (std::size_t at = 0; at < made.count; ++at) {
        const Value* kInput = kParts.second->find(kInputs.at(at));
        if (kInput == nullptr) {
            return invalid("a combine node's inputs are x, y, and for three z, all given");
        }
        RAWFRAME_TRY_ASSIGN(made.parts.at(at), operandOf(surface, *kInput));
        if (made.parts.at(at).carried.has_value() && *made.parts.at(at).carried != Carried::Float) {
            return invalid("a combine node's inputs are floats");
        }
    }
    return made;
}

result::Result<Operand> separate2In(const graph::Document& surface, const graph::Node& node) {
    constexpr std::array<std::string_view, 1> kInputs = {"in"};
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, {}, kInputs));
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

result::Result<Swizzle> swizzleIn(const graph::Document& surface, const graph::Node& node) {
    constexpr std::array<std::string_view, 1> kParams = {"pattern"};
    constexpr std::array<std::string_view, 1> kInputs = {"in"};
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, kParams, kInputs));
    const Value* kPattern = kParts.first->find("pattern");
    const Value* kIn = kParts.second->find("in");
    if (kPattern == nullptr || kPattern->text() == nullptr || kIn == nullptr) {
        return invalid("a swizzle node's pattern and input are given");
    }
    Swizzle made{.pattern = *kPattern->text()};
    RAWFRAME_TRY_ASSIGN(made.in, operandOf(surface, *kIn));
    const std::size_t kWidth = !made.in.carried.has_value()         ? 3
                               : *made.in.carried == Carried::Vec2  ? 2
                               : *made.in.carried == Carried::Float ? 0
                                                                    : 3;
    if (made.pattern.empty() || made.pattern.size() > 3 || kWidth == 0 ||
        !std::ranges::all_of(made.pattern, [kWidth](char letter) {
            return letter >= 'x' && letter <= 'z' && static_cast<std::size_t>(letter - 'x') < kWidth;
        })) {
        return invalid("a swizzle's pattern is one to three of x, y, and z, within its input's width");
    }
    if (made.in.carried.has_value()) {
        made.out = made.pattern.size() == 1              ? Carried::Float
                   : made.pattern.size() == 2            ? Carried::Vec2
                   : *made.in.carried == Carried::Color3 ? Carried::Color3
                                                         : Carried::Vec3;
    }
    return made;
}

result::Result<Compare> compareIn(const graph::Document& surface, const graph::Node& node) {
    constexpr std::array<std::string_view, 1> kParams = {"operator"};
    constexpr std::array<std::string_view, 2> kInputs = {"a", "b"};
    constexpr std::array<std::pair<std::string_view, std::string_view>, 6> kOperators = {{{"less", "<"},
                                                                                          {"less_equal", "<="},
                                                                                          {"greater", ">"},
                                                                                          {"greater_equal", ">="},
                                                                                          {"equal", "=="},
                                                                                          {"not_equal", "!="}}};
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, kParams, kInputs));
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
    for (const Operand* kEach : {&made.a, &made.b}) {
        if (kEach->carried.has_value() && *kEach->carried != Carried::Float) {
            return invalid("a compare node's inputs are floats");
        }
    }
    return made;
}

result::Result<std::uint32_t> timeIn(const graph::Node& node) {
    constexpr std::array<std::string_view, 2> kParams = {"period", "timeline"};
    RAWFRAME_TRY_ASSIGN(const auto kParts, partsOf(node, kParams, {}));
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
    if ((kType == kCombine3Type || kType == kCombine2Type) && from.output == "out") {
        RAWFRAME_TRY_ASSIGN(const Combined kCombined, combineIn(surface, *kSource));
        return std::optional{kCombined.count == 2 ? Carried::Vec2 : Carried::Vec3};
    }
    if (kType == kSeparate2Type && (from.output == "x" || from.output == "y")) {
        RAWFRAME_TRY(separate2In(surface, *kSource));
        return std::optional{Carried::Float};
    }
    if (kType == kCompareType && from.output == "out") {
        RAWFRAME_TRY(compareIn(surface, *kSource));
        return std::optional{Carried::Bool};
    }
    if (kType == kSwizzleType && from.output == "out") {
        RAWFRAME_TRY_ASSIGN(const Swizzle kSwizzle, swizzleIn(surface, *kSource));
        return kSwizzle.out;
    }
    if (kType == kVertexColorType && (from.output == "color" || from.output == "alpha")) {
        RAWFRAME_TRY(streamIn(*kSource));
        return std::optional{from.output == "color" ? Carried::Color3 : Carried::Float};
    }
    if (kType == kTimeType && from.output == "seconds") {
        RAWFRAME_TRY(timeIn(*kSource));
        return std::optional{Carried::Float};
    }
    if ((kType == kWorldPositionType && from.output == "position") ||
        (kType == kWorldNormalType && from.output == "normal") ||
        (kType == kViewDirectionType && from.output == "direction") ||
        (kType == kWorldTangentType && from.output == "tangent")) {
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
