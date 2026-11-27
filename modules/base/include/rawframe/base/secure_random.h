#pragma once

// Bytes from the operating system's cryptographically secure source, for
// values a remote peer must not predict (session nonces and epochs) and
// identities that must never collide (what the tools mint for a game's
// components, resources and entities). Simulation randomness never comes
// from here; it is seeded and replayable.
//
// Nothing in the engine reads std::random_device instead: libstdc++ answers
// it from the processor's RDSEED where there is one, and AMD's Zen 5 answers
// RDSEED with zero, reported as success, for a large share of calls under
// load, so identities minted from it repeat.

#include <cstddef>
#include <cstdint>
#include <span>

namespace rawframe::base {

/// Fills every byte of `into`. Answers false, with `into` unusable, only when
/// the source is unavailable.
[[nodiscard]] bool fillSecureRandom(std::span<std::byte> into) noexcept;

/// Eight bytes of the source as a word. Stops the process when the source is
/// unavailable, which on every supported system it is not once booted.
[[nodiscard]] std::uint64_t secureRandomWord() noexcept;

} // namespace rawframe::base
