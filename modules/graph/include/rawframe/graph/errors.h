#pragma once

#include "rawframe/base/bits128.h"
#include "rawframe/result/error.h"

#include <cstdint>

namespace rawframe::graph {

/// The domain of every Error this module creates.
inline constexpr result::ErrorDomain kGraphDomain{base::parseBits128Hex("911fe78bbbab764f652b9e7b67db8a33").value};

/// Codes within kGraphDomain.
enum class GraphError : std::uint32_t {
    /// Not a graph document, or not in SPEC-0028's grammar: a member out of
    /// place, a node id or type id out of form, an id twice, a connection
    /// to no node or round a cycle, a presentation entry for no node.
    Invalid = 1,
    /// More nodes, inputs, or bytes than the limits allow.
    OverLimit = 2,
};

[[nodiscard]] constexpr result::ErrorCode code(GraphError error) noexcept {
    return result::ErrorCode{static_cast<std::uint32_t>(error)};
}

} // namespace rawframe::graph
