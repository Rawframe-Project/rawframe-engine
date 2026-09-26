// What a process uses (D211, D213): where the platform says, what is
// resident now is within the peak, touching more raises both, and work
// takes processor time.

#include "rawframe/base/usage.h"
#include "rawframe/test/test.h"

#include <cstdint>
#include <memory>
#include <optional>

using namespace rawframe;

RAWFRAME_TEST(ResidentMemoryIsWithinItsPeakAndGrowsWhenTouched) {
    const auto kBefore = base::residentBytes();
    const auto kPeakBefore = base::peakResidentBytes();
#if defined(__linux__) || defined(__APPLE__)
    RAWFRAME_EXPECT(kBefore.has_value() && kPeakBefore.has_value());
#endif
    if (!kBefore.has_value() || !kPeakBefore.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(*kBefore > 0 && *kBefore <= *kPeakBefore);
    // Sixty-four MiB with a byte written on every page, through a volatile
    // pointer so the compiler cannot leave the block out.
    constexpr std::size_t kBytes = std::size_t{64} << 20U;
    const auto kBlock = std::make_unique_for_overwrite<unsigned char[]>(kBytes);
    volatile unsigned char* const kPages = kBlock.get();
    for (std::size_t at = 0; at < kBytes; at += 1024) {
        kPages[at] = 1;
    }
    const auto kAfter = base::residentBytes();
    RAWFRAME_EXPECT(kAfter.has_value() && *kAfter >= *kBefore + (kBytes / 2));
    RAWFRAME_EXPECT(base::peakResidentBytes().value_or(0) >= *kAfter);
}

RAWFRAME_TEST(ProcessorTimeGrowsWithWork) {
    const auto kBefore = base::cpuNanoseconds();
#if defined(__linux__) || defined(__APPLE__)
    RAWFRAME_EXPECT(kBefore.has_value());
#endif
    if (!kBefore.has_value()) {
        return;
    }
    // Work until the processor time moves, which it must well within a
    // billion rounds.
    volatile std::uint64_t spin = 0;
    std::optional<std::uint64_t> after = kBefore;
    for (std::uint64_t round = 0; round < 1'000'000'000 && after == kBefore; ++round) {
        spin = spin + round;
        if (round % 1'000'000 == 0) {
            after = base::cpuNanoseconds();
        }
    }
    RAWFRAME_EXPECT(after.has_value() && *after > *kBefore);
}
