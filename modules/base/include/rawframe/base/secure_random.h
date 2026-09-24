#pragma once

// Bytes from the operating system's cryptographically secure source, for
// values a remote peer must not predict: session nonces and epochs.
// Simulation randomness never comes from here; it is seeded and replayable.

#include <cstddef>
#include <span>

namespace rawframe::base {

/// Fills every byte of `into`. Answers false, with `into` unusable, only when
/// the source is unavailable.
[[nodiscard]] bool fillSecureRandom(std::span<std::byte> into) noexcept;

} // namespace rawframe::base
