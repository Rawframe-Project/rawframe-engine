#pragma once

// The packed color the engine's components carry (D357): 0xRRGGBBAA, its
// red, green, and blue sRGB-encoded and its alpha straight, decoded once
// here for every client module that draws one.

#include <array>
#include <cmath>
#include <cstdint>

namespace rawframe::base {

/// An sRGB-encoded channel of 0xRRGGBBAA, `shift` bits up, in linear light.
inline float linearOf(std::uint32_t color, unsigned shift) noexcept {
    const float kEncoded = static_cast<float>((color >> shift) & 0xFFU) / 255.0F;
    return kEncoded <= 0.04045F ? kEncoded / 12.92F : std::pow((kEncoded + 0.055F) / 1.055F, 2.4F);
}

/// 0xRRGGBBAA's red, green, and blue in linear light, and its alpha.
inline std::array<float, 4> colorAndAlphaOf(std::uint32_t color) noexcept {
    return {linearOf(color, 24), linearOf(color, 16), linearOf(color, 8), static_cast<float>(color & 0xFFU) / 255.0F};
}

} // namespace rawframe::base
