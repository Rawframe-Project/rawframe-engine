#pragma once

// A frame's pixels as a file an image tool opens (D284): what a drawing
// participant writes when asked to capture what it drew, for eyes and
// tools, never on a frame's path.

#include "rawframe/base/platform.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#if RAWFRAME_FILE_SYSTEM
#include "rawframe/base/mapped.h"

#include <chrono>
#include <filesystem>
#include <utility>
#endif

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

#if RAWFRAME_FILE_SYSTEM

/// Pictures streamed through one file written in place (D967), as
/// `render.stream` streams them: made once at a capacity and mapped by its
/// writer and its reader, so a picture costs neither a file made, moved, and
/// closed nor one read, which Windows' real-time protection scanned each
/// time (0.8 ms a 4 MB picture written in place against 4.8 ms, and its
/// scan's 6 ms gone). Its header is streamedOf's, but the two bytes after
/// the mark: the first set while a picture is written, the second once the
/// file is given up for a larger one in its place; then the sequence,
/// width, and height, and the pixels, with room past them.
class PictureStreamWriter {
public:
    explicit PictureStreamWriter(std::filesystem::path path) noexcept : path_{std::move(path)} {
    }

    /// `pixels`, `width` by `height`, written as the stream's next picture;
    /// whether it was. A picture past the file's room is written into a
    /// file made larger, put in its place, and the old one given up.
    [[nodiscard]] bool write(std::uint32_t width, std::uint32_t height, std::span<const std::byte> pixels);

    /// The pictures written, the last one's sequence.
    [[nodiscard]] std::uint64_t written() const noexcept {
        return sequence_;
    }

private:
    std::filesystem::path path_;
    std::optional<base::MappedFile> file_;
    std::uint64_t sequence_ = 0;
};

/// The reader's side: each picture once, whole, never one being written.
class PictureStreamReader {
public:
    explicit PictureStreamReader(std::filesystem::path path) noexcept : path_{std::move(path)} {
    }

    /// The newest picture since the last one taken, none while there is no
    /// newer whole one. A file given up, or one unchanged for a second (its
    /// writer may have started again), is opened again from its path.
    [[nodiscard]] std::optional<StreamedPicture> next();

private:
    std::filesystem::path path_;
    std::optional<base::MappedFile> file_;
    std::uint64_t taken_ = 0;
    std::chrono::steady_clock::time_point changed_{};
};

#endif

} // namespace rawframe::render
