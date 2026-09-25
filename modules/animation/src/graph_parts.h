#pragma once

// What the graph document's node types share: its errors, parameters by
// identity, and connections as the document holds them.

#include "rawframe/animation/graph.h"
#include "rawframe/document/json.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace rawframe::animation {

/// An int parameter's range: whole numbers a double holds exactly and
/// Kest's int reaches.
inline constexpr double kIntLimit = 2147483648.0;

[[nodiscard]] std::unexpected<result::Error> graphInvalid(std::string_view why);
[[nodiscard]] std::unexpected<result::Error> graphOverLimit(std::string_view why);

/// The graph's parameter of that identity, if it has one.
[[nodiscard]] const Parameter* parameterOf(const Graph& graph, std::uint64_t id);

/// 16 lowercase hex digits.
[[nodiscard]] document::Value hexValue(std::uint64_t id);

[[nodiscard]] document::Value connectionValue(const Connection& connection);
[[nodiscard]] std::optional<Connection> connectionOf(const document::Value& value);

/// A state machine node's params, and its inputs: one a state.
[[nodiscard]] document::Value stateMachineParams(const Graph& graph, const StateMachineNode& node);
[[nodiscard]] document::Value stateMachineInputs(const StateMachineNode& node);
[[nodiscard]] result::Result<StateMachineNode> stateMachineOf(const document::Value& params,
                                                              const document::Value& inputs);

/// Refuses (`GraphInvalid`, `OverLimit`) a state machine out of its rules.
[[nodiscard]] result::Status
stateMachineInForm(const Graph& graph, const StateMachineNode& node, const GraphLimits& limits);

} // namespace rawframe::animation
