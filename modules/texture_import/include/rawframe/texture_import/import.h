#pragma once

// Texture import (ADR-0058, D253): a PNG, JPEG, BMP, or TGA source decoded
// by Wuffs, which is memory-safe by construction, then cooked into the
// runtime's texture: its levels made by halving in linear light, and
// block-compressed to BC7 unless kept exact. Import tooling only; no client
// or server closure may depend on it.

#include "rawframe/result/result.h"
#include "rawframe/texture/texture.h"

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

/// A cooked texture's level as RGBA again, for tests and tools: BC7 blocks
/// decoded, uncompressed levels copied.
[[nodiscard]] result::Result<Image> texelsOf(const texture::Texture& texture, std::size_t level);

} // namespace rawframe::texture_import
