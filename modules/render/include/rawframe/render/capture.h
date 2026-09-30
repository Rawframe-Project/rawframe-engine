#pragma once

// A frame's pixels as a file an image tool opens (D284): what a drawing
// participant writes when asked to capture what it drew, for eyes and
// tools, never on a frame's path.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rawframe::render {

/// Pixels, RGBA8 rows top first, as an uncompressed TGA image: the simplest
/// file every image tool opens.
[[nodiscard]] std::vector<std::byte>
tgaOf(std::span<const std::byte> pixels, std::uint32_t width, std::uint32_t height);

} // namespace rawframe::render
