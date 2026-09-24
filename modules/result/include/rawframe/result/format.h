#pragma once

#include "rawframe/result/error.h"

#include <cstddef>
#include <span>

namespace rawframe::result {

/// How much of a rendering was written.
struct FormatResult {
    std::size_t written = 0;
    bool truncated = false;
};

/// A source file name longer than this renders as its last bytes after "...".
inline constexpr std::size_t kMaximumFormattedFileBytes = 96;

/// Enough room for any Error at every bound, including its full cause chain.
/// format.cpp proves at compile time that the worst case fits (SPEC-0050's
/// original 4096 did not; work/decisions.md D10).
inline constexpr std::size_t kMaximumFormattedBytes = 20480;

/// Renders an Error as developer-facing text into caller-owned bytes. Never
/// allocates, never fails, never writes past `destination`. The text is not a
/// diagnostic record or a wire format, is not stable, and nothing may parse it.
[[nodiscard]] FormatResult formatError(const Error& error, std::span<char> destination) noexcept;

} // namespace rawframe::result
