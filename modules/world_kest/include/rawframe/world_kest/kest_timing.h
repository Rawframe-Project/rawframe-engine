#pragma once

// Wall time Kest systems spend in each World tick, over every machine that
// shares one timing: a game's own and each mod's (SPEC-0013's aggregate
// script time per World tick, D210). It only measures: nothing it records
// changes what a system does or when.

#include "rawframe/execution/time.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rawframe::world_kest {

/// Ticks of totals kept for the summary; older ones are overwritten.
inline constexpr std::size_t kKeptKestTicks = 1U << 16U;

class KestTiming {
public:
    explicit KestTiming(const execution::MonotonicSource& clock) noexcept;

    [[nodiscard]] execution::MonotonicInstant now() const noexcept {
        return clock_->now();
    }

    /// Adds `spent` to the total of World tick `tick`. Ticks come in order;
    /// a new one closes the one before.
    void add(std::uint64_t tick, execution::MonotonicDuration spent);

    struct Summary {
        /// Ticks with any Kest system run, and their totals in microseconds.
        std::uint64_t ticks = 0;
        double p50 = 0;
        double p95 = 0;
        double p99 = 0;
        double max = 0;
    };
    /// Over the kept ticks, the one still open among them.
    [[nodiscard]] Summary summary() const;

private:
    void close();

    const execution::MonotonicSource* clock_;
    std::uint64_t tick_ = 0;
    std::int64_t open_ = 0;
    bool opened_ = false;
    std::vector<std::int64_t> totals_;
    std::uint64_t closed_ = 0;
};

} // namespace rawframe::world_kest
