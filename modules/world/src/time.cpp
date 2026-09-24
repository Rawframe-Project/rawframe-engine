#include "rawframe/world/time.h"

#include "rawframe/world/errors.h"

#include <algorithm>

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

namespace rawframe::world {

namespace {

constexpr std::uint64_t kNanosecondsPerSecond = 1'000'000'000;

/// floor(left * right / divisor) without overflow, for a quotient that fits 64
/// bits.
std::uint64_t multiplyDivide(std::uint64_t left, std::uint64_t right, std::uint64_t divisor) noexcept {
#if defined(_MSC_VER) && !defined(__clang__)
    std::uint64_t high = 0;
    const std::uint64_t kLow = _umul128(left, right, &high);
    std::uint64_t remainder = 0;
    return _udiv128(high, kLow, divisor, &remainder);
#else
    __extension__ using Wide = unsigned __int128;
    return static_cast<std::uint64_t>(static_cast<Wide>(left) * right / divisor);
#endif
}

} // namespace

result::Result<TickRate> TickRate::of(std::uint32_t ticks, std::uint32_t seconds) {
    if (ticks == 0 || seconds == 0) {
        return result::fail(result::ErrorClass::InvalidArgument,
                            kWorldDomain,
                            code(WorldError::InvalidTickRate),
                            "a tick rate needs a positive numerator and denominator");
    }
    return TickRate{ticks, seconds};
}

std::uint64_t TickRate::ticksIn(execution::MonotonicDuration elapsed) const noexcept {
    if (elapsed.nanoseconds <= 0) {
        return 0;
    }
    return multiplyDivide(static_cast<std::uint64_t>(elapsed.nanoseconds), ticks, kNanosecondsPerSecond * seconds);
}

TickPacer::Due TickPacer::due(execution::MonotonicInstant now) const noexcept {
    const std::uint64_t kOwedTotal = rate_.ticksIn(now - start_);
    const std::uint64_t kOwed = kOwedTotal > executed_ ? kOwedTotal - executed_ : 0;
    const std::uint64_t kRun = std::min<std::uint64_t>(kOwed, maximumTicksPerIteration_);
    return Due{.run = static_cast<std::uint32_t>(kRun), .debt = kOwed - kRun};
}

} // namespace rawframe::world
