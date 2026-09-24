#pragma once

#include <atomic>
#include <compare>
#include <cstdint>

namespace rawframe::execution {

/// A span of monotonic time in nanoseconds. Monotonic time owns durations,
/// deadlines, and timeouts; it is never converted from wall time (SPEC-0005).
struct MonotonicDuration {
    std::int64_t nanoseconds = 0;

    [[nodiscard]] static constexpr MonotonicDuration fromMilliseconds(std::int64_t value) noexcept {
        return MonotonicDuration{value * 1'000'000};
    }
    [[nodiscard]] static constexpr MonotonicDuration fromSeconds(std::int64_t value) noexcept {
        return MonotonicDuration{value * 1'000'000'000};
    }

    friend constexpr auto operator<=>(const MonotonicDuration&, const MonotonicDuration&) noexcept = default;
    friend constexpr MonotonicDuration operator+(MonotonicDuration left, MonotonicDuration right) noexcept {
        return MonotonicDuration{left.nanoseconds + right.nanoseconds};
    }
};

/// A point on the process's monotonic timeline. Meaningful only against
/// another instant from the same process.
struct MonotonicInstant {
    std::int64_t nanoseconds = 0;

    friend constexpr auto operator<=>(const MonotonicInstant&, const MonotonicInstant&) noexcept = default;
    friend constexpr MonotonicInstant operator+(MonotonicInstant instant, MonotonicDuration duration) noexcept {
        return MonotonicInstant{instant.nanoseconds + duration.nanoseconds};
    }
    friend constexpr MonotonicDuration operator-(MonotonicInstant later, MonotonicInstant earlier) noexcept {
        return MonotonicDuration{later.nanoseconds - earlier.nanoseconds};
    }
};

/// Where monotonic time comes from. The Host owns one; tests own a ManualClock.
class MonotonicSource {
public:
    virtual ~MonotonicSource() = default;
    [[nodiscard]] virtual MonotonicInstant now() const noexcept = 0;
};

/// The process steady clock.
class SteadyClock final : public MonotonicSource {
public:
    [[nodiscard]] MonotonicInstant now() const noexcept override;
};

/// A clock that moves only when told to, for deterministic tests of deadlines
/// and budgets. Safe to read from any thread.
class ManualClock final : public MonotonicSource {
public:
    explicit ManualClock(MonotonicInstant start = {}) noexcept : nanoseconds_(start.nanoseconds) {
    }

    [[nodiscard]] MonotonicInstant now() const noexcept override {
        return MonotonicInstant{nanoseconds_.load(std::memory_order_acquire)};
    }

    void advance(MonotonicDuration duration) noexcept {
        nanoseconds_.fetch_add(duration.nanoseconds, std::memory_order_acq_rel);
    }

private:
    std::atomic<std::int64_t> nanoseconds_;
};

} // namespace rawframe::execution
