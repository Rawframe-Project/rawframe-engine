#pragma once

// A view's geometry (ADR-0052, D366): the eye's axes from its aim, and
// ADR-0052's picking and projection verbs between view points and the
// World, for a perspective view (the scene's) and an orthographic one (the
// canvas's). View points are ADR-0046's screen space: logical pixels from
// the view's top left, y down; the World is right-handed, +Y up, meters.
// Everything is derived from declared camera state and the view's size, so
// it works headlessly, with no frame drawn. A failure is one of a closed
// set, never a NaN.

#include <array>
#include <cstdint>
#include <expected>
#include <optional>

namespace rawframe::view {

/// The eye's right, up, and forward in the World's axes.
struct Axes {
    std::array<float, 3> right{};
    std::array<float, 3> up{};
    std::array<float, 3> forward{};
};

/// The axes of an eye looking along `yaw` radians about +Y (nought looks
/// along -Z, a quarter turn along -X) and `pitch` radians above the
/// ground's plane, kept short of straight up or down.
[[nodiscard]] Axes axesOf(float yaw, float pitch) noexcept;

/// A perspective view: its eye in the World, its aim, its vertical field
/// of view (radians), and its near plane (meters).
struct Perspective {
    std::array<double, 3> eye{};
    float yaw = 0;
    float pitch = 0;
    float fovY = 1;
    float near = 0.1F;
};

/// A perspective view from `eye` looking at `target`, `fovY` radians
/// high (D432); none for an eye at its target or a value not finite.
[[nodiscard]] std::optional<Perspective>
lookingAt(const std::array<double, 3>& eye, const std::array<double, 3>& target, float fovY) noexcept;

/// An orthographic view: the World point at its middle, and how many
/// meters it sees from its top to its bottom.
struct Orthographic {
    std::array<double, 2> middle{};
    float height = 10;
};

/// The view's size in logical pixels.
struct ViewSize {
    float width = 0;
    float height = 0;
};

/// What of a window is covered from each edge, logical pixels: a phone's
/// notch, its home indicator, the system's bars (D589).
struct ViewInsets {
    float top = 0;
    float right = 0;
    float bottom = 0;
    float left = 0;
};

/// Why a conversion has no answer (ADR-0052's closed set): the view has no
/// size; an input is not finite; the camera sees nothing (a field of view
/// or height not above nought, a near plane not above nought); or the
/// World point is not past the near plane.
enum class Failure : std::uint8_t {
    NoViewSize,
    NotFinite,
    SeesNothing,
    BehindNear
};

/// A ray into the World: where it starts, on the near plane, and its unit
/// direction.
struct Ray {
    std::array<double, 3> origin{};
    std::array<float, 3> direction{};
};

/// A point of the view, logical pixels from its top left, and how far
/// ahead of the eye along its forward the World point lies (nought for an
/// orthographic view). A point outside the view is still placed.
struct ViewPoint {
    float x = 0;
    float y = 0;
    float depth = 0;
};

/// `view_point_to_ray`: the ray from the near plane through view point
/// (`x`, `y`).
[[nodiscard]] std::expected<Ray, Failure> pointToRay(const Perspective& view, ViewSize size, float x, float y) noexcept;
/// `world_to_view_point`: where `world` shows in the view, and its depth.
[[nodiscard]] std::expected<ViewPoint, Failure>
worldToPoint(const Perspective& view, ViewSize size, const std::array<double, 3>& world) noexcept;
/// The orthographic verbs: the World point under view point (`x`, `y`),
/// and where a World point shows.
[[nodiscard]] std::expected<std::array<double, 2>, Failure>
pointToWorld(const Orthographic& view, ViewSize size, float x, float y) noexcept;
[[nodiscard]] std::expected<ViewPoint, Failure>
worldToPoint(const Orthographic& view, ViewSize size, const std::array<double, 2>& world) noexcept;

} // namespace rawframe::view
