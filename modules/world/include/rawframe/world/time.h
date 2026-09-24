#pragma once

#include "rawframe/execution/time.h"
#include "rawframe/result/result.h"

#include <compare>
#include <cstdint>

namespace rawframe::world {

/// Which authoritative tick. Never derived from accumulated float seconds, and
/// never skipped (SPEC-0005 simulation time). 64 bits: at 1000 Hz it lasts
/// longer than any process.
struct TickIndex {
    std::uint64_t value = 0;
    friend constexpr auto operator<=>(const TickIndex&, const TickIndex&) noexcept = default;
};

/// Ticks per second as an exact fraction, `ticks / seconds`.
struct TickRate {
    std::uint32_t ticks = 60;
    std::uint32_t seconds = 1;

    /// The rate, or `invalid_argument` if either part is zero.
    [[nodiscard]] static result::Result<TickRate> of(std::uint32_t ticks, std::uint32_t seconds = 1);

    /// How many whole ticks fit in `elapsed`, exactly.
    [[nodiscard]] std::uint64_t ticksIn(execution::MonotonicDuration elapsed) const noexcept;
};

/// Decides how many ticks a Host iteration runs. Ticks owed accumulate from
/// monotonic time; at most `maximumTicksPerIteration` run in one iteration and
/// the rest stays measured as debt, never dropped and never folded into a
/// larger delta (SPEC-0005 catch-up and overload).
class TickPacer {
public:
    TickPacer(TickRate rate, std::uint32_t maximumTicksPerIteration, execution::MonotonicInstant start) noexcept
        : rate_(rate), maximumTicksPerIteration_(maximumTicksPerIteration), start_(start) {
    }

    struct Due {
        std::uint32_t run = 0;  // ticks to run now
        std::uint64_t debt = 0; // ticks owed beyond those
    };

    /// What is owed at `now`. Call `ran` with the ticks actually run.
    [[nodiscard]] Due due(execution::MonotonicInstant now) const noexcept;
    void ran(std::uint32_t ticks) noexcept {
        executed_ += ticks;
    }

    [[nodiscard]] std::uint64_t executed() const noexcept {
        return executed_;
    }

private:
    TickRate rate_;
    std::uint32_t maximumTicksPerIteration_;
    execution::MonotonicInstant start_;
    std::uint64_t executed_ = 0;
};

} // namespace rawframe::world
