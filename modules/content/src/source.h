#pragma once

// A source's reading, inside the module: exactly `length` bytes of a
// locator, or a typed error, and a Build's files and their
// ceilings.

#include "rawframe/content/build_manifest.h"
#include "rawframe/content/source.h"

namespace rawframe::content {

struct ContentSource::Implementation {
    virtual ~Implementation() = default;
    /// Blocks: run on the blocking-I/O executor only.
    [[nodiscard]] virtual result::Result<std::vector<std::byte>> read(std::string_view locator,
                                                                      std::uint64_t length) const = 0;
    /// The length of what a locator holds, found as `read` finds it.
    /// Blocks, as `read` does.
    [[nodiscard]] virtual result::Result<std::uint64_t> size(std::string_view locator) const = 0;
};

/// A chunk's content from its blob in `blobs`, the blob verified before it
/// is used and the content after; refused (`DigestMismatch`, or the read's or the frame's own
/// refusal) otherwise. Blocks.
[[nodiscard]] result::Result<std::vector<std::byte>> readChunk(const ContentSource::Implementation& blobs,
                                                               const BuildChunk& chunk);

} // namespace rawframe::content
