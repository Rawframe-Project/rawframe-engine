#pragma once

// What this process uses, as the platform counts it: memory (SPEC-0013's
// memory objectives, D211) and processor time (its CPU objective, D213).
// Measurement only: nothing the engine decides depends on it. A platform
// that does not say answers nothing.

#include <cstdint>
#include <optional>

namespace rawframe::base {

/// Bytes resident now.
[[nodiscard]] std::optional<std::uint64_t> residentBytes() noexcept;

/// Of those, bytes backed by files: the program, its libraries, and files
/// mapped in (SPEC-0013's mapped artifacts, D216).
[[nodiscard]] std::optional<std::uint64_t> fileResidentBytes() noexcept;

/// The most bytes resident at once since the process began.
[[nodiscard]] std::optional<std::uint64_t> peakResidentBytes() noexcept;

/// Processor time every thread has spent since the process began, user and
/// system, in nanoseconds.
[[nodiscard]] std::optional<std::uint64_t> cpuNanoseconds() noexcept;

} // namespace rawframe::base
