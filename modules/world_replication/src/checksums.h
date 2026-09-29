#pragma once

// What the replication server keeps to check one connection's checksum
// records (SPEC-0041, D204): its own checksum of the connection's predicted
// scope at each of the latest ticks it published, and how many records the
// connection sent in the current second. Private to the server.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rawframe::world_replication {

class ChecksumBook {
public:
    /// Published ticks kept: a record for an older tick is unverifiable.
    static constexpr std::size_t kKept = 64;

    /// The server's checksum at `tick`, which it published.
    void keep(std::uint64_t tick, std::uint64_t checksum) noexcept {
        kept_[static_cast<std::size_t>(tick % kKept)] = Kept{.tick = tick, .checksum = checksum, .set = true};
    }

    /// What was hashed at `tick`, each value's wire form, for a
    /// development capture (D275): kept only when asked for.
    void keepScope(std::uint64_t tick, std::span<const std::span<const std::byte>> values) {
        std::vector<std::vector<std::byte>>& scope = scopes_[static_cast<std::size_t>(tick % kKept)];
        scope.resize(values.size());
        for (std::size_t index = 0; index < values.size(); ++index) {
            scope[index].assign(values[index].begin(), values[index].end());
        }
    }

    /// What was hashed at `tick`, if kept with its checksum.
    [[nodiscard]] std::vector<std::vector<std::byte>> scopeAt(std::uint64_t tick) const {
        return at(tick).has_value() ? scopes_[static_cast<std::size_t>(tick % kKept)]
                                    : std::vector<std::vector<std::byte>>{};
    }

    /// The server's checksum at `tick`, if it is still kept.
    [[nodiscard]] std::optional<std::uint64_t> at(std::uint64_t tick) const noexcept {
        const Kept& kept = kept_[static_cast<std::size_t>(tick % kKept)];
        return kept.set && kept.tick == tick ? std::optional{kept.checksum} : std::nullopt;
    }

    /// Whether one more record may be looked at, at `tick`: at most
    /// `perSecond` in each second of `ticksPerSecond` ticks, counted before
    /// any work on the record (SPEC-0041's checksum_rate_max).
    [[nodiscard]] bool admit(std::uint64_t tick, std::uint64_t ticksPerSecond, std::uint32_t perSecond) noexcept {
        const std::uint64_t kSecond = tick / (ticksPerSecond == 0 ? 1 : ticksPerSecond);
        if (kSecond != second_) {
            second_ = kSecond;
            taken_ = 0;
        }
        return taken_++ < perSecond;
    }

    enum class Verdict : std::uint8_t {
        Verified,
        /// A tick no longer kept, or never published.
        Unverifiable,
        Diverged
    };

    /// A record's checksum at `tick` against the server's own; a divergence
    /// is counted.
    [[nodiscard]] Verdict check(std::uint64_t tick, std::uint64_t checksum) noexcept {
        const std::optional<std::uint64_t> kExpected = at(tick);
        if (!kExpected.has_value()) {
            return Verdict::Unverifiable;
        }
        if (*kExpected == checksum) {
            return Verdict::Verified;
        }
        ++divergences;
        return Verdict::Diverged;
    }

    /// Records that did not match: queryable, never an automatic response.
    std::uint64_t divergences = 0;

private:
    struct Kept {
        std::uint64_t tick = 0;
        std::uint64_t checksum = 0;
        bool set = false;
    };
    std::array<Kept, kKept> kept_{};
    std::array<std::vector<std::vector<std::byte>>, kKept> scopes_{};
    std::uint64_t second_ = 0;
    std::uint32_t taken_ = 0;
};

} // namespace rawframe::world_replication
