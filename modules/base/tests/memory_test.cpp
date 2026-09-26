// A process's resident memory (D211): where the platform says, what is
// resident now is within the peak, and touching more raises both.

#include "rawframe/base/memory.h"
#include "rawframe/test/test.h"

#include <memory>

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
