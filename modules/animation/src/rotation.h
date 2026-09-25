#pragma once

// Rotations turned the same on every machine: slerp from the four
// arithmetic operations and the square root alone, which IEEE 754 rounds
// exactly, rather than a platform's sine and arc cosine, which it does not.

#include <array>

namespace rawframe::animation {

/// From `from` at 0 to `to` at 1 along the shorter arc; both unit, as the
/// result is.
[[nodiscard]] std::array<double, 4>
slerp(const std::array<double, 4>& from, const std::array<double, 4>& to, double at) noexcept;

/// `a` turned onto `b`: the Hamilton product a b, which turns by `b` first.
[[nodiscard]] std::array<double, 4> multiplied(const std::array<double, 4>& a, const std::array<double, 4>& b) noexcept;

/// The opposite turn of a unit rotation.
[[nodiscard]] std::array<double, 4> inverted(const std::array<double, 4>& rotation) noexcept;

/// `vector` turned by the unit rotation `rotation`.
[[nodiscard]] std::array<double, 3> rotated(const std::array<double, 4>& rotation,
                                            const std::array<double, 3>& vector) noexcept;

/// Made unit; the identity when there is no length to make unit.
[[nodiscard]] std::array<double, 4> normalized(const std::array<double, 4>& rotation) noexcept;

} // namespace rawframe::animation
