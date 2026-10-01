#pragma once

// What the material families share (D348): their refusals, and the
// library's node records read alike.

#include "rawframe/document/json.h"
#include "rawframe/graph/graph.h"
#include "rawframe/result/result.h"

#include <span>
#include <string_view>
#include <utility>

namespace rawframe::material {

[[nodiscard]] std::unexpected<result::Error> invalid(std::string_view why);
[[nodiscard]] std::unexpected<result::Error> unsupported(std::string_view why);
/// A graph error as this module's.
[[nodiscard]] std::unexpected<result::Error> asMaterial(result::Error error);

/// A node of the library's params and inputs: the record's only members
/// after its type, objects, their names among `params` and `inputs`, in
/// name order.
[[nodiscard]] result::Result<std::pair<const document::Value*, const document::Value*>>
partsOf(const graph::Node& node, std::span<const std::string_view> params, std::span<const std::string_view> inputs);

/// The node of `id`, if the document holds one.
[[nodiscard]] const graph::Node* nodeOf(const graph::Document& document, graph::NodeId id);

} // namespace rawframe::material
