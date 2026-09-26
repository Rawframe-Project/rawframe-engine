#include "rawframe/world_kest/kest_timing.h"

#include <algorithm>

namespace rawframe::world_kest {

KestTiming::KestTiming(const execution::MonotonicSource& clock) noexcept : clock_(&clock) {
}

void KestTiming::add(std::uint64_t tick, execution::MonotonicDuration spent) {
    if (opened_ && tick != tick_) {
        close();
    }
    tick_ = tick;
    opened_ = true;
    open_ += std::max<std::int64_t>(spent.nanoseconds, 0);
}

void KestTiming::close() {
    if (totals_.size() < kKeptKestTicks) {
        totals_.push_back(open_);
    } else {
        totals_[closed_ % kKeptKestTicks] = open_;
    }
    ++closed_;
    open_ = 0;
    opened_ = false;
}

KestTiming::Summary KestTiming::summary() const {
    std::vector<std::int64_t> sorted = totals_;
    if (opened_) {
        sorted.push_back(open_);
    }
    if (sorted.empty()) {
        return {};
    }
    std::ranges::sort(sorted);
    const auto kAt = [&sorted](double fraction) {
        return static_cast<double>(
                   sorted[static_cast<std::size_t>(fraction * static_cast<double>(sorted.size() - 1))]) /
               1000.0;
    };
    return Summary{
        .ticks = closed_ + (opened_ ? 1 : 0), .p50 = kAt(0.50), .p95 = kAt(0.95), .p99 = kAt(0.99), .max = kAt(1.0)};
}

} // namespace rawframe::world_kest
