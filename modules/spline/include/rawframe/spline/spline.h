#pragma once

// Splines (SPEC-0051's spline row, D867): a smooth path through points,
// open or closed. The points are a scene's entities (rawframe.spline's
// Point, each with a pose), so a spline is edited as any entity is; this
// is the curve they make. It passes through every point in order, a
// centripetal Catmull-Rom curve between each two, so it neither overshoots
// nor loops at a sharp turn and is straight where its points are in a
// line; a closed one joins its last point back to its first. It is
// measured along its length, so what follows it moves at a speed in
// meters. The same points give the same path on every machine.

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rawframe::spline {

using Point = std::array<double, 3>;

struct Spline {
    std::vector<Point> points;
    bool closed = false;
};

/// The straight pieces each span between two points is measured and drawn
/// as.
inline constexpr std::uint32_t kPiecesEach = 16;

/// The spans between its points: one fewer than its points, or as many
/// where it is closed (at least three); none for fewer than two.
[[nodiscard]] std::size_t spans(const Spline& spline) noexcept;

/// The point the path is at `along` spans in, from nought to `spans`, held
/// to that range.
[[nodiscard]] Point at(const Spline& spline, double along) noexcept;

/// The path as straight pieces, kPiecesEach a span, with the distance
/// along it at each point: what it is measured by.
struct Walk {
    std::vector<Point> points;
    std::vector<double> distances;

    [[nodiscard]] double length() const noexcept {
        return distances.empty() ? 0 : distances.back();
    }
};
[[nodiscard]] Walk walk(const Spline& spline);

/// Where the path is `meters` along it, held to its ends (a closed path's
/// wrapping around), and the way it heads there, a unit vector.
struct Along {
    Point point{};
    Point heading{1, 0, 0};
};
[[nodiscard]] Along along(const Walk& walk, double meters, bool closed = false) noexcept;

} // namespace rawframe::spline
