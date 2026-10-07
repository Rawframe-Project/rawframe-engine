#pragma once

// The handles of a preview's mark (D466, D467): the three axes drawn where
// the author chose something, each grabbed by a press whose ray passes near
// it and dragged along its line, and the ring about it, grabbed where the
// ray meets the level plane near it and dragged around.

#include <array>
#include <cstddef>
#include <optional>

namespace rawframe::authoring_session {

using Point = std::array<double, 3>;

/// An axis a press grabbed: which (X, Y, Z), and how far along it.
struct Grabbed {
    std::size_t axis = 0;
    double along = 0;
};

/// The axis of the mark at `mark` that a press's ray from `origin`, as far
/// and which way as `toward`, grabs: the one it passes nearest, within a
/// fortieth of the view's height (`fieldOfView` degrees high) where it
/// passes, on the axis's outer three quarters, the inner quarter being the
/// chosen thing's own. None where it passes none.
[[nodiscard]] std::optional<Grabbed>
grabbedAxis(const Point& mark, const Point& origin, const Point& toward, double fieldOfView) noexcept;

/// Where a ray from `origin`, as far and which way as `toward`, meets the
/// level plane through `mark`, ahead within its reach; none where it does
/// not (D467).
[[nodiscard]] std::optional<Point> levelPoint(const Point& mark, const Point& origin, const Point& toward) noexcept;

/// Whether a press's ray grabs the mark's ring (`view::kMarkRingRadius`
/// about it on the level plane), its turn's handle: it meets the plane
/// within a fortieth of the view's height of the ring (D467).
[[nodiscard]] bool
grabbedRing(const Point& mark, const Point& origin, const Point& toward, double fieldOfView) noexcept;

/// How far along the line of `axis` through `mark` a ray points: the
/// line's point nearest the ray, wherever on the line. None for a ray
/// along the line, or one that points farther than ten kilometers.
[[nodiscard]] std::optional<double>
alongAxis(const Point& mark, std::size_t axis, const Point& origin, const Point& toward) noexcept;

} // namespace rawframe::authoring_session
