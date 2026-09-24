#pragma once

#include "rawframe/execution/time.h"
#include "rawframe/result/result.h"

#include <span>

namespace rawframe::execution {

/// SPEC-0048 budget nesting: each child budget is strictly less than its
/// parent's and the children together do not exceed it. Composition runs this
/// over every level of its shutdown plan before constructing anything, and a
/// failure is `invalid_argument`.
[[nodiscard]] result::Status checkBudgetNesting(MonotonicDuration parent, std::span<const MonotonicDuration> children);

} // namespace rawframe::execution
