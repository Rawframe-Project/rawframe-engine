#pragma once

// A Build chunk's Zstandard blob decompressed as SPEC-0021 bounds it, inside
// the module: no Zstandard type leaves it.

#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rawframe::content {

/// Exactly one complete Zstandard frame of no dictionary, declaring its
/// content size, which is `size`, in a window of at most 8 MiB, with
/// nothing after it; decompressed to exactly `size` bytes. Anything else is
/// refused (`DigestMismatch` does not apply: `ManifestInvalid` for a blob
/// that is not such a frame, `ReadFailed` for one that does not decode).
[[nodiscard]] result::Result<std::vector<std::byte>> decompressFrame(std::span<const std::byte> blob,
                                                                     std::uint64_t size);

} // namespace rawframe::content
