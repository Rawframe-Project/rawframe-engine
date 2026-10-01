#pragma once

#include <array>
#include <cmath>
#include <cstdint>

// The scene's small geometry: vectors of three floats, their cross product
// and direction, a color's linear light, and a quaternion's turn.

namespace rawframe::render_scene {

using Vector = std::array<float, 3>;

inline Vector cross(const Vector& a, const Vector& b) noexcept {
    return {(a[1] * b[2]) - (a[2] * b[1]), (a[2] * b[0]) - (a[0] * b[2]), (a[0] * b[1]) - (a[1] * b[0])};
}

/// `a` made a meter long; straight up if it has no length.
inline Vector normalized(const Vector& a) noexcept {
    const float kLength = std::sqrt((a[0] * a[0]) + (a[1] * a[1]) + (a[2] * a[2]));
    return kLength > 0 ? Vector{a[0] / kLength, a[1] / kLength, a[2] / kLength} : Vector{0, 1, 0};
}

/// An sRGB channel of 0xRRGGBBAA, `shift` bits up, in linear light.
inline float linearOf(std::uint32_t color, unsigned shift) noexcept {
    const float kEncoded = static_cast<float>((color >> shift) & 0xFFU) / 255.0F;
    return kEncoded <= 0.04045F ? kEncoded / 12.92F : std::pow((kEncoded + 0.055F) / 1.055F, 2.4F);
}

/// 0xRRGGBBAA's red, green, and blue in linear light.
inline Vector colorOf(std::uint32_t color) noexcept {
    return {linearOf(color, 24), linearOf(color, 16), linearOf(color, 8)};
}

/// 0xRRGGBBAA's red, green, and blue in linear light, and its alpha.
inline std::array<float, 4> colorAndAlphaOf(std::uint32_t color) noexcept {
    const Vector kColor = colorOf(color);
    return {kColor[0], kColor[1], kColor[2], static_cast<float>(color & 0xFFU) / 255.0F};
}

/// The columns of a unit quaternion's turn.
inline std::array<Vector, 3> turnOf(const std::array<float, 4>& q) noexcept {
    const float kX = q[0];
    const float kY = q[1];
    const float kZ = q[2];
    const float kW = q[3];
    return {{{1 - (2 * ((kY * kY) + (kZ * kZ))), 2 * ((kX * kY) + (kZ * kW)), 2 * ((kX * kZ) - (kY * kW))},
             {2 * ((kX * kY) - (kZ * kW)), 1 - (2 * ((kX * kX) + (kZ * kZ))), 2 * ((kY * kZ) + (kX * kW))},
             {2 * ((kX * kZ) + (kY * kW)), 2 * ((kY * kZ) - (kX * kW)), 1 - (2 * ((kX * kX) + (kY * kY)))}}};
}

} // namespace rawframe::render_scene
