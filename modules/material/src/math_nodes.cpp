#include "surface_graph.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace rawframe::material::surface_graph {

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

std::span<const MathNode> mathNodes() {
    return kMathNodes;
}

const MathNode* mathNodeOf(std::string_view type) {
    const auto kFound = std::ranges::find(kMathNodes, type, &MathNode::type);
    return kFound == kMathNodes.end() ? nullptr : &*kFound;
}

std::optional<std::string_view> wholeCodeOf(std::string_view type) {
    constexpr std::array<std::pair<std::string_view, std::string_view>, 11> kWhole = {{
        {kAddType, "$0 + $1"},
        {kSubtractType, "$0 - $1"},
        {kMultiplyType, "$0 * $1"},
        {"rawframe/divide@1", "$0 / $1"},
        // Slang's remainder takes the dividend's sign; floored as the
        // float's modulo is, the divisor's.
        {"rawframe/modulo@1", "((($0) % ($1)) + ($1)) % ($1)"},
        {"rawframe/negate@1", "-($0)"},
        {"rawframe/abs@1", "abs($0)"},
        {"rawframe/sign@1", "sign($0)"},
        {"rawframe/min@1", "min($0, $1)"},
        {"rawframe/max@1", "max($0, $1)"},
        {"rawframe/clamp@1", "clamp($0, $1, $2)"},
    }};
    const auto kFound = std::ranges::find(kWhole, type, &std::pair<std::string_view, std::string_view>::first);
    return kFound == kWhole.end() ? std::nullopt : std::optional{kFound->second};
}

bool wholeNumber(double number) {
    return std::isfinite(number) && number == std::trunc(number) && number >= -2147483648.0 && number <= 2147483647.0;
}

} // namespace rawframe::material::surface_graph
