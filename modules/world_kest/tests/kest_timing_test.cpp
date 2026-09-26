// Kest time per World tick (D210): each tick's runs add up, a new tick
// closes the last, and the summary counts the one still open.

#include "rawframe/test/test.h"
#include "rawframe/world_kest/kest_timing.h"

using namespace rawframe;
using namespace rawframe::world_kest;

namespace {

execution::MonotonicDuration micros(std::int64_t count) {
    return execution::MonotonicDuration{count * 1000};
}

} // namespace

RAWFRAME_TEST(KestTimeAddsUpPerTick) {
    execution::ManualClock clock;
    KestTiming timing{clock};
    RAWFRAME_EXPECT(timing.summary().ticks == 0);
    // Tick 1: three systems; tick 2: one; tick 3: two, still open.
    for (const auto& [kTick, kSpent] : {std::pair{1, 100}, {1, 200}, {1, 300}, {2, 50}, {3, 1000}, {3, 1000}}) {
        timing.add(static_cast<std::uint64_t>(kTick), micros(kSpent));
    }
    const KestTiming::Summary kSummary = timing.summary();
    RAWFRAME_EXPECT(kSummary.ticks == 3);
    RAWFRAME_EXPECT(kSummary.p50 == 600 && kSummary.max == 2000);
    // A clock that went back adds nothing: tick 4 took nought.
    timing.add(4, execution::MonotonicDuration{-5});
    RAWFRAME_EXPECT(timing.summary().ticks == 4 && timing.summary().p50 == 50 && timing.summary().max == 2000);
}
