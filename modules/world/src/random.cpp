#include "rawframe/world/random.h"

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

namespace rawframe::world {

std::uint64_t detail::multiplyHigh64(std::uint64_t left, std::uint64_t right) noexcept {
#if defined(_MSC_VER) && !defined(__clang__)
    return __umulh(left, right);
#else
    __extension__ using Wide = unsigned __int128;
    return static_cast<std::uint64_t>((static_cast<Wide>(left) * right) >> 64U);
#endif
}

std::uint64_t Pcg32::nextBelow64(std::uint64_t bound) noexcept {
    // Lemire's method in 64 bits: the low half of the 128-bit product decides
    // rejection, the high half is the draw.
    std::uint64_t value = nextU64();
    std::uint64_t low = value * bound;
    if (low < bound) {
        const std::uint64_t kThreshold = (0U - bound) % bound;
        while (low < kThreshold) {
            value = nextU64();
            low = value * bound;
        }
    }
    return detail::multiplyHigh64(value, bound);
}

std::size_t weightedChoice(std::span<const std::uint64_t> weights, Pcg32& generator) noexcept {
    std::uint64_t total = 0;
    for (const std::uint64_t kWeight : weights) {
        total += kWeight;
    }
    if (total == 0) {
        return weights.size();
    }
    std::uint64_t pick = generator.nextBelow64(total);
    for (std::size_t index = 0; index < weights.size(); ++index) {
        if (pick < weights[index]) {
            return index;
        }
        pick -= weights[index];
    }
    return weights.size();
}

} // namespace rawframe::world
