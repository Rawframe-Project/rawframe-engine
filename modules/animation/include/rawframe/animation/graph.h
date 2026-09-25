#pragma once

// Animation graphs (SPEC-0035's `animation.graph`, on SPEC-0028's graph
// document grammar): the one animation authority, a DAG of nodes driven by
// typed parameters, in one canonical form (arrays of numbers shown here on
// one line, where the form has one a line):
//
//   {
//     "formatVersion": 1,
//     "kind": "animation.graph",
//     "interface": {
//       "parameters": {
//         "speed": {
//           "parameter": "5f3a0c2d9e81b746",
//           "type": "float",
//           "default": 0,
//           "minimum": 0,
//           "maximum": 8,
//           "replication": "server_authoritative"
//         }
//       }
//     },
//     "graph": {
//       "1f00000000000001": {
//         "type": "rawframe/clip@1",
//         "params": {
//           "clip": "52771075251e7361deaecf4939c72e56",
//           "speed": {
//             "parameter": "5f3a0c2d9e81b746"
//           }
//         },
//         "inputs": {}
//       },
//       "2f00000000000002": {
//         "type": "rawframe/output@1",
//         "params": {},
//         "inputs": {
//           "pose": {
//             "node": "1f00000000000001",
//             "output": "pose"
//           }
//         }
//       }
//     }
//   }
//
// Parameters are the graph's interface: each by its machine name, with a
// durable identity that replication and saves keep through a rename, a
// type (`bool`, `int`, `float`, or `vec2`), a default, for `int` and
// `float` an optional minimum and maximum, and a replication class
// (`server_authoritative`, `client_predicted`, or `local`). A node is keyed
// by 16 random lowercase hex digits, never renumbered; its inputs connect
// by name to another node's `pose`. Parameters, nodes, inputs, and weights
// are in name order. A number a node takes is a literal or
// `{"parameter": id}` of a `float` parameter; a param at its default is
// left out. The generation-1 node types:
//
//   `rawframe/clip@1`    plays `clip` (a resource identity) at `speed`
//                        (default 1), looping as the clip says or as
//                        `loop` overrides it (`clamp` or `loop`)
//   `rawframe/blend@1`   blends its inputs, each by its entry in
//                        `weights` (default 1), normalized
//   `rawframe/mask@1`    its input `inside` on the bones `mask` (a mask's
//                        resource identity) weighs, blended by that weight
//                        with its input `outside` on every other
//   `rawframe/blend_space_1d@1`
//                        its inputs placed on a line by `points`, a number
//                        each, blended by where `position` (default 0) is
//                        among them: the two either side of it by nearness,
//                        or the end one past the ends
//   `rawframe/blend_space_2d@1`
//                        its inputs placed on a plane by `points`, `[x, y]`
//                        each, and `triangles` over them, three names each;
//                        blended by `position` (a `[x, y]`, or a `vec2`
//                        parameter; default `[0, 0]`) within the first
//                        triangle holding it, or at the nearest point of
//                        the nearest triangle when none does (D135)
//   `rawframe/state_machine@1`
//                        one of its inputs, each a state, at a time,
//                        starting at `entry` and moving by `transitions`
//   `rawframe/output@1`  the graph's pose, from its input `pose`; one
//
// A state machine's transitions are in order of their source, those from
// any state (`from` left out) first, then of priority, highest first, and
// a state's own and those from any state never share a priority, so one
// always wins. Each is `from`, `to` (another state), `priority` (0),
// `duration` in seconds (0), `curve` (`linear`, or `cubic_in_out`),
// `interruption` (`none`, `by_higher_priority`, or `by_any`), `cooldown`
// in seconds (0), `resetPhase` (false), and its `conditions`, all of which
// must hold: `{"parameter": id, "comparison": c, "value": v}` with c one of
// `equal`, `not_equal`, `less`, `less_or_equal`, `greater`, and
// `greater_or_equal`; `{"phase": p}`, the source state's phase reached;
// `{"finished": true}`, its clip played out; or `{"event": id}`, fired in
// it. Members at the defaults in parentheses are left out.
//
// A 2D blend space's triangles each name three of its points in name order,
// are in order, and have area; no two overlap, and every point is in one.
//
// A node of any other type is quarantined: kept byte for byte and written
// back, while a graph holding one cannot be hashed or played. An optional
// `presentation` object holds what editors draw, keyed by node, and never
// means anything.

#include "rawframe/animation/clip.h"
#include "rawframe/base/bits128.h"
#include "rawframe/base/sha256.h"
#include "rawframe/document/json.h"
#include "rawframe/result/result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace rawframe::animation {

enum class ParameterType : std::uint8_t {
    Bool,
    Int,
    Float,
    Vec2,
};

enum class Replication : std::uint8_t {
    ServerAuthoritative,
    ClientPredicted,
    Local,
};

/// A value of any parameter type: a `vec2` uses both numbers, the others
/// the first (a `bool` as 0 or 1).
using ParameterValue = std::array<double, 2>;

struct Parameter {
    std::string name;
    std::uint64_t id = 0;
    ParameterType type = ParameterType::Float;
    ParameterValue initial{};
    std::optional<double> minimum;
    std::optional<double> maximum;
    Replication replication = Replication::ServerAuthoritative;

    friend bool operator==(const Parameter&, const Parameter&) = default;
};

/// A parameter, by its identity: a `float` one where a node takes a number,
/// a `vec2` one where it takes a point.
struct ParameterRef {
    std::uint64_t parameter = 0;

    friend bool operator==(const ParameterRef&, const ParameterRef&) = default;
};

/// A number a node takes: a literal, or a parameter's value.
using Scalar = std::variant<double, ParameterRef>;

/// A point a node takes: a literal, or a `vec2` parameter's value.
using Point = std::variant<std::array<double, 2>, ParameterRef>;

struct Connection {
    std::uint64_t node = 0;
    std::string output = "pose";

    friend bool operator==(const Connection&, const Connection&) = default;
};

struct ClipNode {
    base::Bits128 clip;
    Scalar speed = 1.0;
    std::optional<Loop> loop;

    friend bool operator==(const ClipNode&, const ClipNode&) = default;
};

struct BlendInput {
    std::string name;
    Connection from;
    Scalar weight = 1.0;

    friend bool operator==(const BlendInput&, const BlendInput&) = default;
};

struct BlendNode {
    std::vector<BlendInput> inputs;

    friend bool operator==(const BlendNode&, const BlendNode&) = default;
};

/// SPEC-0035's mask node: `inside` where the mask weighs a bone, `outside`
/// elsewhere, blended by the bone's weight.
struct MaskNode {
    base::Bits128 mask;
    Connection inside;
    Connection outside;

    friend bool operator==(const MaskNode&, const MaskNode&) = default;
};

/// A blend space's input, placed at `at` (a line's uses the first number).
struct BlendSpacePoint {
    std::string name;
    Connection from;
    std::array<double, 2> at{};

    friend bool operator==(const BlendSpacePoint&, const BlendSpacePoint&) = default;
};

/// SPEC-0035's `blend_space_1d`: inputs on a line, weighed by `position`.
struct BlendSpace1DNode {
    Scalar position = 0.0;
    /// In name order.
    std::vector<BlendSpacePoint> points;

    friend bool operator==(const BlendSpace1DNode&, const BlendSpace1DNode&) = default;
};

/// SPEC-0035's `blend_space_2d`: inputs on a plane with a declared
/// triangulation, weighed by `position`.
struct BlendSpace2DNode {
    Point position = std::array<double, 2>{};
    /// In name order.
    std::vector<BlendSpacePoint> points;
    /// Each three points by name, in name order; the triangles in order.
    std::vector<std::array<std::string, 3>> triangles;

    friend bool operator==(const BlendSpace2DNode&, const BlendSpace2DNode&) = default;
};

struct OutputNode {
    Connection pose;

    friend bool operator==(const OutputNode&, const OutputNode&) = default;
};

enum class Comparison : std::uint8_t {
    Equal,
    NotEqual,
    Less,
    LessOrEqual,
    Greater,
    GreaterOrEqual,
};

/// A parameter compared with a value of its type; a `vec2` compares with
/// nothing, a `bool` only equal or not.
struct ParameterCondition {
    std::uint64_t parameter = 0;
    Comparison comparison = Comparison::Equal;
    double value = 0.0;

    friend bool operator==(const ParameterCondition&, const ParameterCondition&) = default;
};

/// The source state's phase has reached this, in [0, 1].
struct PhaseCondition {
    double phase = 0.0;

    friend bool operator==(const PhaseCondition&, const PhaseCondition&) = default;
};

/// The source state's clip has played to its end without looping.
struct FinishedCondition {
    friend bool operator==(const FinishedCondition&, const FinishedCondition&) = default;
};

/// An event of this identity fired in the source state this advance.
struct EventCondition {
    std::uint64_t event = 0;

    friend bool operator==(const EventCondition&, const EventCondition&) = default;
};

using Condition = std::variant<ParameterCondition, PhaseCondition, FinishedCondition, EventCondition>;

enum class BlendCurve : std::uint8_t {
    Linear,
    CubicInOut,
};

/// Whether a transition under way may be replaced by another.
enum class Interruption : std::uint8_t {
    None,
    ByHigherPriority,
    ByAny,
};

struct Transition {
    /// None for a transition from any state.
    std::optional<std::string> from;
    std::string to;
    std::int32_t priority = 0;
    double duration = 0.0;
    BlendCurve curve = BlendCurve::Linear;
    Interruption interruption = Interruption::None;
    /// Seconds `to` must have been left before it is entered again.
    double cooldown = 0.0;
    /// Entering `to` starts its clips from their beginning.
    bool resetPhase = false;
    /// All of them, or none for always.
    std::vector<Condition> conditions;

    friend bool operator==(const Transition&, const Transition&) = default;
};

struct State {
    std::string name;
    Connection from;

    friend bool operator==(const State&, const State&) = default;
};

struct StateMachineNode {
    std::vector<State> states;
    std::string entry;
    std::vector<Transition> transitions;

    friend bool operator==(const StateMachineNode&, const StateMachineNode&) = default;
};

/// A node of a type this engine does not know, as it was read.
struct QuarantinedNode {
    std::string type;
    std::string record;

    friend bool operator==(const QuarantinedNode&, const QuarantinedNode&) = default;
};

struct GraphNode {
    std::uint64_t id = 0;
    std::variant<ClipNode,
                 BlendNode,
                 StateMachineNode,
                 OutputNode,
                 MaskNode,
                 BlendSpace1DNode,
                 BlendSpace2DNode,
                 QuarantinedNode>
        node;

    friend bool operator==(const GraphNode&, const GraphNode&) = default;
};

struct Graph {
    std::vector<Parameter> parameters;
    std::vector<GraphNode> nodes;
    /// Each node's drawing, as its compact text; never meaning.
    std::vector<std::pair<std::uint64_t, std::string>> presentation;

    friend bool operator==(const Graph&, const Graph&) = default;
};

/// SPEC-0028's and SPEC-0035's named limit points for graphs; past one is
/// `OverLimit`.
struct GraphLimits {
    std::size_t maximumNodes = 1024;
    /// A blend's inputs, a blend space's points, and a state machine's
    /// states.
    std::size_t maximumInputs = 64;
    /// A 2D blend space's triangles.
    std::size_t maximumTriangles = 128;
    std::size_t maximumParameters = 256;
    std::size_t maximumTransitions = 256;
    std::size_t maximumConditions = 16;
};

/// Refuses (`GraphInvalid`) a graph out of its rules: an id twice, a
/// connection to no node or into a cycle, a reference to no `float`
/// parameter, other than one output.
[[nodiscard]] result::Status validate(const Graph& graph, const GraphLimits& limits = {});

[[nodiscard]] result::Result<std::string> writeGraph(const Graph& graph, const GraphLimits& limits = {});

/// Reads the canonical form only, as hostile input.
[[nodiscard]] result::Result<Graph> readGraph(std::string_view text, const GraphLimits& limits = {});

/// Every clip the graph's clip nodes name, each once, in identity order:
/// the clips it must be compiled with.
[[nodiscard]] std::vector<base::Bits128> clipsOf(const Graph& graph);

/// Every mask the graph's mask nodes name, each once, in identity order.
[[nodiscard]] std::vector<base::Bits128> masksOf(const Graph& graph);

/// SPEC-0028's semantic hash: a Merkle digest from the output down, blind
/// to node ids, node order, nodes the output does not reach, and
/// presentation. Refuses (`GraphInvalid`) a graph with a quarantined node.
[[nodiscard]] result::Result<base::Sha256Digest> semanticHash(const Graph& graph);

} // namespace rawframe::animation
