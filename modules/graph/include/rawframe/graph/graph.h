#pragma once

// Graph documents (ADR-0027, SPEC-0028): the grammar every graph family
// shares, in one canonical form:
//
//   {
//     "formatVersion": 1,
//     "kind": "<the family>",
//     "interface": {...},
//     "graph": {
//       "<16 lowercase hex digits>": {
//         "type": "<namespace>/<name>@<major>[.<minor>]",
//         "params": {...},
//         "inputs": {
//           "<input>": {"node": "<id>", "output": "<output>"} or a literal
//         }
//       }
//     },
//     ...the family's own sections, in its order...
//     "presentation": {"<id>": {...}}
//   }
//
// A node is keyed by an id minted at random when it is made and never
// renumbered; nodes are in id order. Its inputs connect by name to another
// node's outputs, or hold a literal; a graph has no cycle. `presentation`,
// optional, holds what editors draw and never means anything. What a type,
// a param, or an input means, and which are left out at their defaults, is
// the family's: this module reads, writes, checks, and hashes the grammar,
// and keeps every node whatever its type, so a family quarantines a type
// it does not know by keeping its record.

#include "rawframe/base/sha256.h"
#include "rawframe/document/json.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::graph {

/// A node's key within its document, 64 random bits; never an identity
/// outside it.
using NodeId = std::uint64_t;

/// Its 16 lowercase hex digits.
[[nodiscard]] std::string nodeIdText(NodeId id);
[[nodiscard]] std::optional<NodeId> nodeIdOf(std::string_view text) noexcept;

/// SPEC-0028's `<namespace>/<name>@<major>[.<minor>]`: the namespace
/// `rawframe` or a package's `publisher/package`, each part a machine name.
[[nodiscard]] bool typeIdInForm(std::string_view type) noexcept;

/// A lowercase letter, then lowercase letters, digits, and underscores, 64
/// at most: an input's, an output's, or a param's name.
[[nodiscard]] bool machineName(std::string_view name) noexcept;

/// An input connected to another node's output.
struct Connection {
    NodeId node = 0;
    std::string output;

    friend bool operator==(const Connection&, const Connection&) = default;
};

[[nodiscard]] document::Value connectionValue(const Connection& connection);

/// An input's value as a connection: exactly `node` (an id in form) and
/// `output` (a machine name); anything else is a literal.
[[nodiscard]] std::optional<Connection> connectionOf(const document::Value& value);

/// A node: its id and its record, `{"type", "params", "inputs"}`.
struct Node {
    NodeId id = 0;
    document::Value record;
};

/// A record's connected inputs, in name order.
[[nodiscard]] std::vector<Connection> connectionsOf(const document::Value& record);

/// A graph document: its family, interface, nodes in id order, the
/// family's own sections in its order, and each node's drawing.
struct Document {
    std::string kind;
    document::Value interface = document::Value::object();
    std::vector<Node> nodes;
    std::vector<std::pair<std::string, document::Value>> sections;
    std::vector<std::pair<NodeId, document::Value>> presentation;
};

/// SPEC-0028's named limit points the grammar checks; past one is
/// `OverLimit`, checked before the work.
struct Limits {
    std::size_t maximumBytes = std::size_t{1} << 22;
    std::size_t maximumNodes = 1024;
    /// A node's inputs, and its params.
    std::size_t maximumInputs = 64;
    std::size_t maximumPresentation = 1024;
};

/// The grammar's rules over a document's nodes: ids in order and once,
/// each record led by its type in form, every connection to a node there,
/// and no cycle.
[[nodiscard]] result::Status validate(const Document& graph, const Limits& limits = {});

/// The document in its canonical text.
[[nodiscard]] result::Result<std::string> writeDocument(const Document& graph, const Limits& limits = {});

/// Reads a document of `kind`, with the family's `sections` (each optional,
/// in that order), as hostile input, and validates it. That it is in its
/// one form is the family's check, which knows the defaults left out: its
/// writer's text, byte for byte.
[[nodiscard]] result::Result<Document> readDocument(std::string_view text,
                                                    std::string_view kind,
                                                    std::span<const std::string_view> sections = {},
                                                    const Limits& limits = {});

/// What a semantic hash covers besides the nodes (SPEC-0028): the family,
/// the interface, each graph output by the node it comes from, and the
/// family's sections that mean something, in its order.
struct HashHeader {
    std::string_view kind;
    document::Value interface = document::Value::object();
    std::vector<std::pair<std::string, NodeId>> outputs;
    std::vector<std::pair<std::string, document::Value>> sections;
};

/// SPEC-0028's semantic hash: a Merkle digest from the outputs down, each
/// node's covering its type, params, and inputs, a connected input by the
/// digest of the node it comes from rather than that node's id; blind to
/// ids, node order, nodes no output reaches, and presentation. The nodes
/// must be valid.
[[nodiscard]] result::Result<base::Sha256Digest> semanticHash(std::span<const Node> nodes, const HashHeader& header);

} // namespace rawframe::graph
