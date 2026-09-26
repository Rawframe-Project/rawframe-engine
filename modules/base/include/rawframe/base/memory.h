#pragma once

// How much memory this process holds, as the platform counts it (SPEC-0013's
// memory objectives, D211). Measurement only: nothing the engine decides
// depends on it. A platform that does not say answers nothing.

#include <cstdint>
#include <optional>

namespace rawframe::base {

/// Bytes resident now.
[[nodiscard]] std::optional<std::uint64_t> residentBytes() noexcept;

/// The most bytes resident at once since the process began.
[[nodiscard]] std::optional<std::uint64_t> peakResidentBytes() noexcept;

} // namespace rawframe::base
