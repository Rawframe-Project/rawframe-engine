#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

namespace rawframe::world {

// Deterministic simulation randomness (ADR-0055, SPEC-0041). The algorithms
// below are one versioned identity: changing any of them, the derivation
// included, is a new identity, never a silent swap. The golden vectors in
// tests/random_test.cpp pin them.

/// The generator identity recorded wherever draws must be reproduced.
inline constexpr std::string_view kRandomIdentity = "pcg32-xsh-rr+splitmix64+lemire.v1";

/// The most streams one owner may declare (`rng_streams_per_owner_max`).
inline constexpr std::size_t kMaximumRandomStreamsPerOwner = 16;

/// Advances a SplitMix64 state and returns its next output (Steele, Lea, and
/// Flood's published constants).
[[nodiscard]] constexpr std::uint64_t splitMix64(std::uint64_t& state) noexcept {
    state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t mixed = state;
    mixed = (mixed ^ (mixed >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    mixed = (mixed ^ (mixed >> 27U)) * 0x94D049BB133111EBULL;
    return mixed ^ (mixed >> 31U);
}

/// FNV-1a over bytes, used only to turn a textual stream identity into 64 bits
/// before SplitMix64 mixing.
[[nodiscard]] constexpr std::uint64_t identityHash(std::string_view text) noexcept {
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (const char kCharacter : text) {
        hash = (hash ^ static_cast<unsigned char>(kCharacter)) * 0x100000001B3ULL;
    }
    return hash;
}

/// A World's root seed: chosen once by the authority, not secret, replicated
/// as plain data. Every stream derives from it by identity.
struct RootSeed {
    std::uint64_t value = 0;
};

namespace detail {

/// The high 64 bits of a 128-bit product.
[[nodiscard]] std::uint64_t multiplyHigh64(std::uint64_t left, std::uint64_t right) noexcept;

} // namespace detail

/// PCG32 XSH-RR: 64-bit state, 32-bit output, O'Neill's published
/// construction. Its two words are the stream's whole state, so copying it
/// captures it for a checkpoint or a rollback.
class Pcg32 {
public:
    /// The reference seeding: `pcg32_srandom_r(initialState, sequence)`.
    constexpr Pcg32(std::uint64_t initialState, std::uint64_t sequence) noexcept : increment_((sequence << 1U) | 1U) {
        static_cast<void>(nextU32());
        state_ += initialState;
        static_cast<void>(nextU32());
    }

    /// Restores a captured state exactly.
    [[nodiscard]] static constexpr Pcg32 fromWords(std::uint64_t state, std::uint64_t increment) noexcept {
        Pcg32 generator{0, 0};
        generator.state_ = state;
        generator.increment_ = increment | 1U;
        return generator;
    }

    [[nodiscard]] constexpr std::uint32_t nextU32() noexcept {
        const std::uint64_t kOld = state_;
        state_ = kOld * 6364136223846793005ULL + increment_;
        const auto kShifted = static_cast<std::uint32_t>(((kOld >> 18U) ^ kOld) >> 27U);
        const auto kRotation = static_cast<std::uint32_t>(kOld >> 59U);
        return (kShifted >> kRotation) | (kShifted << ((0U - kRotation) & 31U));
    }

    [[nodiscard]] constexpr std::uint64_t nextU64() noexcept {
        const std::uint64_t kHigh = nextU32();
        return (kHigh << 32U) | nextU32();
    }

    /// Uniform in [0, bound), unbiased: Lemire's multiply-high with the rare
    /// rejection that removes bias. Never modulo. `bound` must be positive.
    [[nodiscard]] constexpr std::uint32_t nextBelow(std::uint32_t bound) noexcept {
        std::uint64_t product = std::uint64_t{nextU32()} * bound;
        auto low = static_cast<std::uint32_t>(product);
        if (low < bound) {
            const std::uint32_t kThreshold = (0U - bound) % bound;
            while (low < kThreshold) {
                product = std::uint64_t{nextU32()} * bound;
                low = static_cast<std::uint32_t>(product);
            }
        }
        return static_cast<std::uint32_t>(product >> 32U);
    }

    /// The 64-bit form, for bounds such as cumulative weights.
    [[nodiscard]] std::uint64_t nextBelow64(std::uint64_t bound) noexcept;

    /// Uniform in [0, 1) with 64 random bits scaled by 2^-64 (exact, no
    /// division).
    [[nodiscard]] constexpr double nextDouble() noexcept {
        return static_cast<double>(nextU64() >> 11U) * 0x1.0p-53;
    }

    /// Uniform in [0, 1) with 24 random bits.
    [[nodiscard]] constexpr float nextFloat() noexcept {
        return static_cast<float>(nextU32() >> 8U) * 0x1.0p-24F;
    }

    [[nodiscard]] constexpr std::uint64_t stateWord() const noexcept {
        return state_;
    }
    [[nodiscard]] constexpr std::uint64_t incrementWord() const noexcept {
        return increment_;
    }

    friend constexpr bool operator==(const Pcg32&, const Pcg32&) noexcept = default;

private:
    std::uint64_t state_ = 0;
    std::uint64_t increment_ = 0;
};

/// The stream a durable owner identity and a declared stream name get under a
/// root seed. Order-free: it depends on nothing but those three values.
[[nodiscard]] constexpr Pcg32 deriveStream(RootSeed root, std::string_view owner, std::string_view name) noexcept {
    std::uint64_t seeder = root.value ^ identityHash(owner);
    seeder = splitMix64(seeder) ^ identityHash(name);
    const std::uint64_t kInitialState = splitMix64(seeder);
    const std::uint64_t kSequence = splitMix64(seeder);
    return Pcg32{kInitialState, kSequence};
}

/// The index-th value of a stream without sequential state, for unordered or
/// parallel access and for record and playback addressing.
[[nodiscard]] constexpr std::uint64_t
indexedDraw(RootSeed root, std::string_view owner, std::string_view name, std::uint64_t index) noexcept {
    std::uint64_t seeder = root.value ^ identityHash(owner);
    seeder = splitMix64(seeder) ^ identityHash(name);
    seeder = splitMix64(seeder) + index;
    return splitMix64(seeder);
}

/// Durstenfeld's Fisher-Yates shuffle over the generator's bounded draws.
template <typename T> constexpr void shuffle(std::span<T> values, Pcg32& generator) noexcept {
    for (std::size_t index = values.size(); index > 1; --index) {
        const std::uint32_t kPick = generator.nextBelow(static_cast<std::uint32_t>(index));
        std::swap(values[index - 1], values[kPick]);
    }
}

/// One index chosen with probability proportional to its weight: a single
/// bounded draw against the cumulative 64-bit total. Returns weights.size()
/// when every weight is zero.
[[nodiscard]] std::size_t weightedChoice(std::span<const std::uint64_t> weights, Pcg32& generator) noexcept;

} // namespace rawframe::world
