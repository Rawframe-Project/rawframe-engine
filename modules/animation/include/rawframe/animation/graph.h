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
//   `rawframe/output@1`  the graph's pose, from its input `pose`; one
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

/// A `float` parameter, by its identity.
struct ParameterRef {
    std::uint64_t parameter = 0;

    friend bool operator==(const ParameterRef&, const ParameterRef&) = default;
};

/// A number a node takes: a literal, or a parameter's value.
using Scalar = std::variant<double, ParameterRef>;

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

struct OutputNode {
    Connection pose;

    friend bool operator==(const OutputNode&, const OutputNode&) = default;
};

/// A node of a type this engine does not know, as it was read.
struct QuarantinedNode {
    std::string type;
    std::string record;

    friend bool operator==(const QuarantinedNode&, const QuarantinedNode&) = default;
};

struct GraphNode {
    std::uint64_t id = 0;
    std::variant<ClipNode, BlendNode, OutputNode, QuarantinedNode> node;

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
    std::size_t maximumInputs = 64;
    std::size_t maximumParameters = 256;
};

/// Refuses (`GraphInvalid`) a graph out of its rules: an id twice, a
/// connection to no node or into a cycle, a reference to no `float`
/// parameter, other than one output.
[[nodiscard]] result::Status validate(const Graph& graph, const GraphLimits& limits = {});

[[nodiscard]] result::Result<std::string> writeGraph(const Graph& graph, const GraphLimits& limits = {});

/// Reads the canonical form only, as hostile input.
[[nodiscard]] result::Result<Graph> readGraph(std::string_view text, const GraphLimits& limits = {});

/// SPEC-0028's semantic hash: a Merkle digest from the output down, blind
/// to node ids, node order, nodes the output does not reach, and
/// presentation. Refuses (`GraphInvalid`) a graph with a quarantined node.
[[nodiscard]] result::Result<base::Sha256Digest> semanticHash(const Graph& graph);

} // namespace rawframe::animation
