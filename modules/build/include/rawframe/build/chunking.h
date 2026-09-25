#pragma once

// SPEC-0021's chunking function: FastCDC with normalized chunking over one
// resource's bytes, its gear table and masks fixed by the container
// generation. A pure function of the bytes: the same bytes always cut the
// same way, and state never crosses from one resource to the next.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rawframe::build {

inline constexpr std::size_t kMinimumChunk = std::size_t{256} * 1024;
inline constexpr std::size_t kTargetChunk = std::size_t{1024} * 1024;
inline constexpr std::size_t kMaximumChunk = std::size_t{4} * 1024 * 1024;

/// Entry i: the first eight bytes of SHA-256 of the one byte i, big-endian.
[[nodiscard]] const std::array<std::uint64_t, 256>& gearTable() noexcept;

/// Where each chunk of `bytes` ends, in order; the last is `bytes.size()`.
/// Empty bytes are one empty chunk.
[[nodiscard]] std::vector<std::size_t> chunkEnds(std::span<const std::byte> bytes);

} // namespace rawframe::build
