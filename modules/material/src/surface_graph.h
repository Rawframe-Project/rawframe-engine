#pragma once

// A surface material's graph read node by node: what each node of the library this family knows says, checked,
// and what its outputs carry. Compiling to the blob folds it (material.cpp);
// generating Slang writes it out as code (slang.cpp).

#include "rawframe/document/json.h"
#include "rawframe/graph/graph.h"
#include "rawframe/material/material.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::material::surface_graph {

/// What an output of a node this family knows carries.
enum class Carried : std::uint8_t {
    Float,
    Color3,
    Vec2,
    Vec3,
    /// What the logic nodes take and give.
    Bool,
    /// The four-wide types.
    Vec4,
    Color4,
    /// A texture an interface input gives, which only a sampled texture
    /// takes.
    Texture2d,
    /// A whole number, which the int-taking math nodes, compare, and select
    /// take with ints alone.
    Int
};

/// A surface's or a function's document while it is checked, compiled, or
/// written, with what each output it reaches carries, found once: a node
/// many read is reached by every path to it, and finding it again each
/// time grows with the paths, twice over a level where a node reads one
/// output twice.
class Graph {
public:
    explicit Graph(const graph::Document& document) noexcept : document_(document) {
    }
    Graph(const Graph&) = delete;
    Graph& operator=(const Graph&) = delete;

    // The document, as the helpers that only look a node up take it.
    operator const graph::Document&() const noexcept {
        return document_;
    }
    [[nodiscard]] const graph::Document& document() const noexcept {
        return document_;
    }

    /// What `from` carries, if found before.
    [[nodiscard]] std::optional<std::optional<Carried>> found(const graph::Connection& from) const;
    void remember(const graph::Connection& from, std::optional<Carried> carried) const;

private:
    const graph::Document& document_;
    mutable std::map<std::pair<graph::NodeId, std::string>, std::optional<Carried>> found_;
};

/// The states' words, in their enumerations' order.
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

/// The params and inputs a node of one of the family's own types may hold,
/// and the outputs a connection may name, each in name order:
/// what its reader checks a node against and `nodeSignatures` lists. The
/// surface's inputs are the Surface contract's and an inline-Slang node's
/// inputs and outputs those its params declare, so neither holds them here.
struct Members {
    std::span<const std::string_view> params;
    std::span<const std::string_view> inputs;
    std::span<const std::string_view> outputs;
};
/// The members of one of the family's own types; none for another type.
[[nodiscard]] std::optional<Members> ownMembersOf(std::string_view type);
/// A node of one of the family's own types: its params and inputs, checked
/// as `partsOf` checks them against its type's members.
[[nodiscard]] result::Result<std::pair<const document::Value*, const document::Value*>>
ownPartsOf(const graph::Node& node);
/// Whether a type is one of the atomic core's math nodes (`mathNodeOf`).
[[nodiscard]] bool math(std::string_view type);
/// A known node's type.
[[nodiscard]] std::string_view typeOf(const graph::Node& node);

/// What a `uv` node's params say: the channel.
[[nodiscard]] result::Result<std::uint32_t> uvIn(const graph::Node& node);

/// An input of `multiply` or `add`: connected, or a literal, and
/// what it carries when that is known.
struct Operand {
    std::optional<graph::Connection> from;
    std::array<double, 4> literal{};
    std::optional<Carried> carried;
};

[[nodiscard]] result::Result<Operand> operandOf(const Graph& surface, const document::Value& value);

/// How a math node types its inputs and its `out`.
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
    Across,
    /// Bools in, a bool out: and, or, xor, not.
    Logical,
    /// A bool, then two inputs of one type or floats; one of them out:
    /// select.
    Choice,
    /// A color3 in, a float out: luminance.
    Tone,
    /// A color3 in, a color3 out: rgb_to_hsv, hsv_to_rgb.
    Recolor,
    /// A vec2 in, a float out: the 2D noise kernels.
    Noise2,
    /// A vec3 in, a float out: the 3D noise kernel.
    Noise3
};

/// One of the atomic core's math nodes: its type, its
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
    /// The functions its code calls, those they call first, each written
    /// once before the material where any node uses it; none
    /// for most.
    std::array<std::string_view, 3> helpers;
};

/// The math node of `type`; none for another type.
[[nodiscard]] const MathNode* mathNodeOf(std::string_view type);
/// Every math node, as `nodeSignatures` lists them.
[[nodiscard]] std::span<const MathNode> mathNodes();

/// The code a math node is written as over ints, `$0` to `$2` its inputs:
/// add, subtract, multiply, divide (truncating), modulo (floored, as the
/// float's is), negate, abs, sign, min, max, and clamp; none for
/// another.
[[nodiscard]] std::optional<std::string_view> wholeCodeOf(std::string_view type);

/// Whether a literal number is whole and an int holds it.
[[nodiscard]] bool wholeNumber(double number);

/// A math node's inputs, checked, in its code's order, and what its `out`
/// carries where that is known.
struct MathInputs {
    std::array<Operand, 5> inputs;
    std::size_t count = 0;
    std::optional<Carried> out;
};

[[nodiscard]] result::Result<MathInputs> mathInputsOf(const Graph& surface, const graph::Node& node);

/// A `multiply` or `add` node's inputs, checked, and what its `out`
/// carries: their type, or the one not a float when the other is.
struct Math {
    Operand a;
    Operand b;
    std::optional<Carried> out;
};

[[nodiscard]] result::Result<Math> mathIn(const Graph& surface, const graph::Node& node);

/// A `combine2`, `combine3`, or `combine4` node's inputs, `x`, `y`, and
/// for three `z`, for four `w` too, checked: each a float, all given.
struct Combined {
    std::array<Operand, 4> parts;
    std::size_t count = 0;
};

[[nodiscard]] result::Result<Combined> combineIn(const Graph& surface, const graph::Node& node);

/// A `separate2` node's input, checked: a vec2, its outputs `x` and `y`.
[[nodiscard]] result::Result<Operand> separate2In(const Graph& surface, const graph::Node& node);

/// A `separate4` node's input, checked: a vec4 or a color4, its outputs
/// `r`, `g`, `b`, and `a`, as a separate3's are.
[[nodiscard]] result::Result<Operand> separate4In(const Graph& surface, const graph::Node& node);

/// A `swizzle` node's input and its declared pattern, checked: one
/// to four of `x`, `y`, `z`, and `w`, each within the input's width, a
/// vector or a color; its `out` a float or a vector by the pattern's
/// length, a color3 or a color4 for three or four of a color.
struct Swizzle {
    Operand in;
    std::string pattern;
    std::optional<Carried> out;
};

[[nodiscard]] result::Result<Swizzle> swizzleIn(const Graph& surface, const graph::Node& node);

/// A `constant` node's declared type and value, checked: a float, a
/// bool, a vec2, a vec3, or a color3, its value of that shape; an int, a
/// vec4, or a color4 waits for those types. Its `out` is the value.
[[nodiscard]] result::Result<Operand> constantIn(const graph::Node& node);

/// One of a surface's interface inputs: its
/// name, its type, and its default, held in the parameters' vector `slot`.
struct InterfaceInput {
    std::string_view name;
    Carried carried = Carried::Float;
    std::array<double, 4> initial{};
    std::size_t slot = 0;
    /// A texture input's default, the game's texture by its identity;
    /// nought for any other.
    std::uint64_t texture = 0;
    /// The stream a function's input defaults to (`uv`, `world_position`,
    /// `world_normal`, or `view_direction`); empty for a literal.
    std::string_view stream;
};

/// A Kest name's shape: lower case letters, digits, and underscores, a
/// letter first, at most 64; an interface input's, a switch's, and an
/// inline node's pin's.
[[nodiscard]] bool kestNamed(std::string_view name);

/// What an interface input's default or an instance's override of it
/// says, of the input's type: numbers, three or four a vector
/// or a color alike; an int a whole number within 2^24 either way of
/// nought, which the blob's float holds exactly; a bool true or false, held
/// as one or nought.
[[nodiscard]] result::Result<std::array<double, 4>> interfaceValueOf(Carried carried, const document::Value& value);

/// The document's interface, checked: none, or `inputs`, each named as a
/// Kest name is, eight at most, its `default` and its `type` (a float, an
/// int, a bool, a vector, a color, or a `texture_2d` defaulting to a
/// game's texture by 16 lowercase hex digits), in name order, a slot
/// each in that order.
/// A function's texture input is its type alone, each call giving it a
/// texture.
[[nodiscard]] result::Result<std::vector<InterfaceInput>> interfaceOf(const Graph& surface);

/// A `parameter` node's input, checked: the interface input its `name`
/// param names, its `out` of that input's type; or a
/// `parameter_texture_2d` node's, a texture input, its `texture` that
/// texture.
[[nodiscard]] result::Result<InterfaceInput> parameterIn(const Graph& surface, const graph::Node& node);

/// A `compare` node's operator and inputs, checked: `less`,
/// `less_equal`, `greater`, `greater_equal`, `equal`, or `not_equal`, over
/// two floats; a bool out.
struct Compare {
    std::string_view symbol;
    Operand a;
    Operand b;
};

[[nodiscard]] result::Result<Compare> compareIn(const Graph& surface, const graph::Node& node);

/// A `time` node's params, checked: its timeline, the simulation's,
/// the presented World's ticks over its tick rate in seconds;
/// and the whole seconds it wraps at, none when it is not wrapped.
[[nodiscard]] result::Result<std::uint32_t> timeIn(const graph::Node& node);

/// A stream node, `world_position`, `world_normal`, `vertex_color`,
/// `view_direction`, or `world_tangent`, checked: no params and no inputs.
[[nodiscard]] result::Status streamIn(const graph::Node& node);

/// A `quality_switch` node's inputs, checked, and what its `out` carries:
/// `default` given, all of one type where it is known.
struct Switch {
    std::array<std::optional<Operand>, 4> inputs;
    std::optional<Carried> out;
};

/// A quality switch's inputs, in the order `Switch::inputs` holds them.
inline constexpr std::array<std::string_view, 4> kSwitchInputs = {"default", "high", "low", "medium"};

[[nodiscard]] result::Result<Switch> switchIn(const Graph& surface, const graph::Node& node);

/// A `transform` node's spaces, what it takes, and its input, checked:
/// `from` and `to` two of `object`, `world`, `view`, `tangent`,
/// and `clip`, which differ; `kind` a `point`, a `vector`, or a `normal`;
/// `in` a vec3. A point has no place in the tangent frame, and only a
/// point goes to the clip's space, where its `out` is a vec4, any other a
/// vec3; a transform from the clip's space waits (`Unsupported`).
struct Transform {
    std::string_view from;
    std::string_view to;
    std::string_view kind;
    Operand in;
};

[[nodiscard]] result::Result<Transform> transformIn(const Graph& surface, const graph::Node& node);

/// One of a surface's static switches: its name and the branch it takes
/// unless an instance overrides it.
struct StaticSwitch {
    std::string_view name;
    bool on = false;
};

/// The surface's declared switches: none, or its interface's `switches`,
/// one to eight, each named as a Kest name is, `{"default": bool}`, in
/// name order.
[[nodiscard]] result::Result<std::vector<StaticSwitch>> switchesOf(const Graph& surface);

/// A `static_switch` node, checked: its `name` param a declared switch, its
/// inputs `off` and `on` of one type, never a texture; the branch the
/// switch takes, and what `out` carries where that is known.
struct SwitchChoice {
    Operand off;
    Operand on;
    bool taken = false;
    std::optional<Carried> out;
};

[[nodiscard]] result::Result<SwitchChoice> staticSwitchIn(const Graph& surface, const graph::Node& node);

/// One of an inline-Slang node's declared inputs or outputs: its name and
/// its type.
struct Pin {
    std::string_view name;
    Carried carried = Carried::Float;
};

/// An `inline_slang` node, checked:
/// its params `body`, one function's, at most 4096 bytes of printable
/// ASCII, tabs, and new lines, with no preprocessor line, string, import,
/// include, module, extern, export, or double underscore, and its braces
/// closing in order; `capabilities`, none in generation 1; `domains`, of
/// `surface`, `canvas`, and `post_process`, a surface's among them;
/// `inputs` and `outputs`, each named as a Kest name is and typed from
/// the closed set but a texture, eight inputs at most and one to four
/// outputs; and `stages`, of `fragment` and `vertex`. Its node inputs give
/// each declared input, of its type.
struct Inline {
    std::vector<Pin> inputs;
    std::vector<Pin> outputs;
    std::vector<Operand> given;
    bool fragment = false;
    bool vertex = false;
    std::string_view body;
};

[[nodiscard]] result::Result<Inline> inlineIn(const Graph& surface, const graph::Node& node);

/// A `separate3` node's input, checked: a color3, or a vec3, a
/// place's axes r, g, and b.
[[nodiscard]] result::Result<Operand> separate3In(const Graph& surface, const graph::Node& node);

/// A `normal_map` node's input and scale, checked.
struct NormalMap {
    Operand in;
    double scale = 1;
};

[[nodiscard]] result::Result<NormalMap> normalMapIn(const Graph& surface, const graph::Node& node);

/// What a `sample_texture_2d` node's params say, and what its `uv` comes
/// from, if connected. Its texture is its `texture` param's, or the
/// texture input its `texture` input is connected to, then `input`
/// that input's slot and the texture its default.
struct Sampling {
    SampledTexture texture;
    std::optional<graph::Connection> uv;
    std::optional<std::size_t> input;
};

[[nodiscard]] result::Result<Sampling> sampleIn(const Graph& surface, const graph::Node& node);

/// A node of a type this family knows, other than the surface, checked as
/// its type checks it: in a surface or a function's document.
[[nodiscard]] result::Status nodeChecked(const Graph& surface, const graph::Node& node);

/// What the output a connection names carries, if its node is of a type
/// this family knows; refused when the node has no such output.
[[nodiscard]] result::Result<std::optional<Carried>> carriedBy(const Graph& surface, const graph::Connection& from);

} // namespace rawframe::material::surface_graph
