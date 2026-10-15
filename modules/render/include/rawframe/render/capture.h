#pragma once

// A frame's pixels as a file an image tool opens (D284): what a drawing
// participant writes when asked to capture what it drew, for eyes and
// tools, never on a frame's path.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rawframe::render {

/// Pixels, RGBA8 rows top first, as an uncompressed TGA image: the simplest
/// file every image tool opens.
[[nodiscard]] std::vector<std::byte>
tgaOf(std::span<const std::byte> pixels, std::uint32_t width, std::uint32_t height);

/// A picture streamed to another process (D789): what `render.stream`
/// writes each picture read back as, and an editor reads to show it.
struct StreamedPicture {
    /// Counts up from one with each picture written.
    std::uint64_t sequence = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    /// RGBA8, rows top first.
    std::vector<std::byte> pixels;
};

/// `picture` as a stream file: `rfpic1`, two bytes of nought, then the
/// sequence, width, and height little-endian, then the pixels.
[[nodiscard]] std::vector<std::byte> streamedOf(const StreamedPicture& picture);

/// The picture a stream file holds; none for bytes that are not one whole
/// (a header that does not read, a size past 8192 pixels a side, or pixels
/// of another count), as a reader that came while it was written may find.
[[nodiscard]] std::optional<StreamedPicture> streamedFrom(std::span<const std::byte> bytes);

} // namespace rawframe::render
