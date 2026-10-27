#pragma once

// A chunk compressed as the canonical packing does, inside the
// module: no Zstandard type leaves it.

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace rawframe::build {

/// One Zstandard frame of `chunk` under this packer generation's pinned
/// parameters, or none when that would not be smaller than the chunk.
[[nodiscard]] std::optional<std::vector<std::byte>> compressChunk(std::span<const std::byte> chunk);

} // namespace rawframe::build
