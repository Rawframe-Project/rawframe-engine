#pragma once

// A source's reading, inside the module: exactly `length` bytes of a
// locator, or the typed error SPEC-0008 names, and a Build's files and their
// ceilings.

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

/// SPEC-0021's hard ceiling on a Build manifest, and a signature envelope's
/// own.
inline constexpr std::size_t kMaximumBuildManifest = std::size_t{64} * 1024 * 1024;
inline constexpr std::size_t kMaximumBuildSignature = 1024;

} // namespace rawframe::content
