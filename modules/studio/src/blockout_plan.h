#pragma once

// Where a solid dragged across the blockout plan lands, as the text of its
// line's `at`.

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace rawframe::studio {

/// The `at` of a solid whose `at` reads `at`, carried `meters` across X
/// and Z: X and Z on a quarter-meter grid, the height as it was, each
/// number spelled as the blockout writer spells it, so it reads back
/// exactly. None where `at` does not begin with three numbers.
[[nodiscard]] std::optional<std::string> draggedPlace(std::string_view at, std::array<double, 2> meters);

} // namespace rawframe::studio
