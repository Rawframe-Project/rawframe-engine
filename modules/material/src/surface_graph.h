#pragma once

// A surface material's graph read node by node (D308, D311 to D313,
// D318): what each node of the library this family knows says, checked,
// and what its outputs carry. Compiling to the blob folds it (material.cpp);
// generating Slang writes it out as code (slang.cpp, D483).

#include "rawframe/document/json.h"
#include "rawframe/graph/graph.h"
#include "rawframe/material/material.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace rawframe::material::surface_graph {

/// What an output of a node this family knows carries.
enum class Carried : std::uint8_t {
    Float,
    Color3,
    Vec2,
    Vec3
};

/// SPEC-0026's states' words, in their enumerations' order.
inline constexpr std::array<std::string_view, 2> kShadings = {"lit", "unlit"};
inline constexpr std::array<std::string_view, 3> kBlends = {"opaque", "masked", "translucent"};

/// The document's `states` section, if it has one.
[[nodiscard]] const document::Value* statesOf(const graph::Document& surface);
/// The declared states a section says, checked, in a material otherwise
/// at its defaults; all at their defaults for none.
[[nodiscard]] result::Result<Material> statesIn(const document::Value* states);

/// The document's one surface node, if it has exactly one.
[[nodiscard]] const graph::Node* surfaceNode(const graph::Document& surface);
/// Whether a node is of a type this family knows.
[[nodiscard]] bool known(const graph::Node& node);
/// Whether a type is one of the atomic core's math nodes (`mathNodeOf`).
[[nodiscard]] bool math(std::string_view type);
/// A known node's type.
[[nodiscard]] std::string_view typeOf(const graph::Node& node);

/// What a `uv` node's params say: the channel.
[[nodiscard]] result::Result<std::uint32_t> uvIn(const graph::Node& node);

/// An input of `multiply` or `add` (D311): connected, or a literal, and
/// what it carries when that is known.
struct Operand {
    std::optional<graph::Connection> from;
    std::array<double, 3> literal{};
    std::optional<Carried> carried;
};

[[nodiscard]] result::Result<Operand> operandOf(const graph::Document& surface, const document::Value& value);

/// How a math node types its inputs and its `out` (SPEC-0026; D691, D694).
enum class Rule : std::uint8_t {
    /// Each input of one type, or a float; the out of that type.
    Componentwise,
    /// One vector in, a float out: its length.
    Measure,
    /// One vector in, one of its type out: normalized.
    Unit,
    /// Two vectors of one type in, a float out: dot, distance.
    Between,
    /// Two vec3s in, a vec3 out: cross, reflect.
    Across
};

/// One of the atomic core's math nodes (SPEC-0026, D694): its type, its
/// inputs in the order its code takes them, how it types them, and its
/// Slang, `$0` to `$4` standing for its inputs. A componentwise call
/// (`call`) has its float inputs made the out's vector where the out is
/// one.
struct MathNode {
    std::string_view type;
    std::array<std::string_view, 5> inputs;
    std::size_t count = 0;
    Rule rule = Rule::Componentwise;
    std::string_view code;
    bool call = false;
};

/// The math node of `type`; none for another type.
[[nodiscard]] const MathNode* mathNodeOf(std::string_view type);

/// A math node's inputs, checked, in its code's order, and what its `out`
/// carries where that is known.
struct MathInputs {
    std::array<Operand, 5> inputs;
    std::size_t count = 0;
    std::optional<Carried> out;
};

[[nodiscard]] result::Result<MathInputs> mathInputsOf(const graph::Document& surface, const graph::Node& node);

/// A `multiply` or `add` node's inputs, checked, and what its `out`
/// carries: their type, or the one not a float when the other is.
struct Math {
    Operand a;
    Operand b;
    std::optional<Carried> out;
};

[[nodiscard]] result::Result<Math> mathIn(const graph::Document& surface, const graph::Node& node);

/// A `combine2` or `combine3` node's inputs, `x`, `y`, and for three `z`,
/// checked: each a float, all given (D691, D695).
struct Combined {
    std::array<Operand, 3> parts;
    std::size_t count = 0;
};

[[nodiscard]] result::Result<Combined> combineIn(const graph::Document& surface, const graph::Node& node);

/// A `separate2` node's input, checked: a vec2, its outputs `x` and `y`
/// (D695).
[[nodiscard]] result::Result<Operand> separate2In(const graph::Document& surface, const graph::Node& node);

/// A `swizzle` node's input and its declared pattern, checked (D695): one
/// to three of `x`, `y`, and `z`, each within the input's width, a vec2 or
/// a vec3 or a color3; its `out` a float, a vec2, or a vec3 by the
/// pattern's length, a color3 for three of a color3.
struct Swizzle {
    Operand in;
    std::string pattern;
    std::optional<Carried> out;
};

[[nodiscard]] result::Result<Swizzle> swizzleIn(const graph::Document& surface, const graph::Node& node);

/// A `time` node's params, checked (D691): its timeline, the simulation's,
/// the presented World's ticks over its tick rate in seconds (ADR-0010).
[[nodiscard]] result::Status timeIn(const graph::Node& node);

/// A stream node, `world_position`, `world_normal`, or `vertex_color`,
/// checked: no params and no inputs (D691, D694, D695).
[[nodiscard]] result::Status streamIn(const graph::Node& node);

/// A `quality_switch` node's inputs, checked, and what its `out` carries
/// (D318): `default` given, all of one type where it is known.
struct Switch {
    std::array<std::optional<Operand>, 4> inputs;
    std::optional<Carried> out;
};

/// A quality switch's inputs, in the order `Switch::inputs` holds them.
inline constexpr std::array<std::string_view, 4> kSwitchInputs = {"default", "high", "low", "medium"};

[[nodiscard]] result::Result<Switch> switchIn(const graph::Document& surface, const graph::Node& node);

/// A `separate3` node's input, checked: a color3 (D312), or a vec3, a
/// place's axes r, g, and b (D691).
[[nodiscard]] result::Result<Operand> separate3In(const graph::Document& surface, const graph::Node& node);

/// A `normal_map` node's input and scale, checked (D313).
struct NormalMap {
    Operand in;
    double scale = 1;
};

[[nodiscard]] result::Result<NormalMap> normalMapIn(const graph::Document& surface, const graph::Node& node);

/// What a `sample_texture_2d` node's params say, and what its `uv` comes
/// from, if connected.
struct Sampling {
    SampledTexture texture;
    std::optional<graph::Connection> uv;
};

[[nodiscard]] result::Result<Sampling> sampleIn(const graph::Document& surface, const graph::Node& node);

/// What the output a connection names carries, if its node is of a type
/// this family knows; refused when the node has no such output.
[[nodiscard]] result::Result<std::optional<Carried>> carriedBy(const graph::Document& surface,
                                                               const graph::Connection& from);

} // namespace rawframe::material::surface_graph
