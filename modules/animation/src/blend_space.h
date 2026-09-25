#pragma once

// How a blend space shares its weight among its points at a position
// (SPEC-0035, D135): deterministic, from the four arithmetic operations.

#include <array>
#include <cstddef>
#include <span>

namespace rawframe::animation {

/// Along a line, by each point's first number: the two points either side
/// of `position` by nearness, the one there when it is on a point, or the
/// end one past the ends. `shares` has a place for each point.
void lineShares(std::span<const std::array<double, 2>> points, double position, std::span<double> shares);

/// On a plane: the first triangle holding `position`, by its barycentric
/// weights; when none does, the nearest point of the nearest triangle (the
/// first of equals), by its weights there.
void planeShares(std::span<const std::array<double, 2>> points,
                 std::span<const std::array<std::size_t, 3>> triangles,
                 const std::array<double, 2>& position,
                 std::span<double> shares);

/// Twice the signed area of a, b, c: positive turning left.
[[nodiscard]] double
turn(const std::array<double, 2>& a, const std::array<double, 2>& b, const std::array<double, 2>& c) noexcept;

} // namespace rawframe::animation
