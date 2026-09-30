#pragma once

// Texture import (ADR-0058, D253): a PNG, JPEG, BMP, or TGA source decoded
// by Wuffs, which is memory-safe by construction, then cooked into the
// runtime's texture: its levels made by halving in linear light, and
// block-compressed to BC7 unless kept exact. An environment (D321) is a
// Radiance picture, cooked into a cube whose levels are its light as ever
// rougher surfaces reflect it. Import tooling only; no client or server
// closure may depend on it.

#include "rawframe/result/result.h"
#include "rawframe/texture/texture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rawframe::texture_import {

/// A decoded source: rows top first, red, green, blue, and alpha a byte
/// each, alpha straight (not premultiplied).
struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::byte> rgba;
};

struct DecodeLimits {
    std::uint32_t maximumSide = 8192;
};

/// Decodes a source, whose format its first bytes say. Refuses
/// (`BadSource`) another format or a broken file, and (`OverLimit`) a side
/// past the limit, before its pixels are allocated.
[[nodiscard]] result::Result<Image> decodeImage(std::span<const std::byte> source, const DecodeLimits& limits = {});

/// How an image is cooked. Color is sRGB unless the image holds data
/// (normals, masks); `exact` keeps texels uncompressed, for pixel art and
/// interface images; `levels` makes the full chain down to one texel.
struct CookSettings {
    bool srgb = true;
    bool exact = false;
    bool levels = true;
};

/// The texture of an image: level nought is the image, each next level the
/// one before halved, each texel the average of the ones it covers,
/// weighted by their alpha and taken in linear light for sRGB color, so
/// edges keep their color and do not darken. Refuses (`BadTexture`) an
/// image whose bytes do not fill its sides.
[[nodiscard]] result::Result<texture::Texture> cookTexture(const Image& image, const CookSettings& settings = {});

/// A decoded picture of light: rows top first, linear Rec. 709 red, green,
/// and blue, three floats a texel, unbounded above.
struct LightImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> rgb;
};

/// Decodes a Radiance picture (`.hdr`): RGBE texels, flat or run-length
/// encoded by scanline, rows top first and left to right (`-Y` then `+X`).
/// Refuses (`BadSource`) another pixel format, orientation, or an old
/// run-length scanline, and a broken or short file; and (`OverLimit`) a
/// side past the limit, before its texels are allocated.
[[nodiscard]] result::Result<LightImage> decodeRadiance(std::span<const std::byte> source,
                                                        const DecodeLimits& limits = {});

/// A picture of light as a Radiance file (D326): RGBE texels, each
/// scanline run-length encoded where its width allows (8 to 32767), flat
/// otherwise; rows top first. Light below nought, and not a number, is
/// black; past what RGBE holds, the most it does.
[[nodiscard]] std::vector<std::byte> encodeRadiance(const LightImage& image);

/// The direction the point (`u` across, `v` down, each from 0 to 1) of an
/// equirectangular picture shows, as `cookEnvironment` reads the picture
/// (D326): a unit vector.
[[nodiscard]] std::array<double, 3> pictureDirection(double u, double v) noexcept;

/// How an environment is cooked. A cube's face is `side` texels square, a
/// power of two; its `levels` run from a mirror's reflection (level nought)
/// to the roughest surface's (the last), perceptual roughness rising
/// evenly, each level filtered from `samples` directions.
struct EnvironmentSettings {
    std::uint32_t side = 128;
    std::uint32_t levels = 6;
    std::uint32_t samples = 128;
};

/// The cube of an equirectangular picture of all directions: its middle
/// looks along -Z, its right edge wraps to its left behind, and its top is
/// +Y, so a direction samples the cube as it would the picture. Level
/// nought is the picture resampled; each next level is the one before's
/// light reflected by GGX at its roughness, taken by importance sampling
/// from ever smaller levels (filtered importance sampling). Texels are
/// RGBA16F, alpha one. Refuses (`BadTexture`) an empty or short picture,
/// or settings outside their ranges: a side from 4 to 2048, levels no more
/// than its halvings to one texel, samples from 1 to 4096.
[[nodiscard]] result::Result<texture::Texture> cookEnvironment(const LightImage& image,
                                                               const EnvironmentSettings& settings = {});

/// A cooked texture's level as RGBA again, for tests and tools: BC7 blocks
/// decoded, uncompressed levels copied.
[[nodiscard]] result::Result<Image> texelsOf(const texture::Texture& texture, std::size_t level);

} // namespace rawframe::texture_import
