#include "rawframe/build/chunking.h"

#include "rawframe/base/sha256.h"

#include <algorithm>

namespace rawframe::build {

namespace {

// The small-region mask takes the gear hash's top 22 bits, the large-region
// mask its top 18: harder to match before the target size, easier after.
constexpr std::uint64_t kSmallMask = ~std::uint64_t{0} << 42U;
constexpr std::uint64_t kLargeMask = ~std::uint64_t{0} << 46U;

std::array<std::uint64_t, 256> makeGear() noexcept {
    std::array<std::uint64_t, 256> gear{};
    for (std::size_t index = 0; index < gear.size(); ++index) {
        const std::byte kByte = static_cast<std::byte>(index);
        const base::Sha256Digest kDigest = base::sha256(std::span{&kByte, 1});
        std::uint64_t value = 0;
        for (std::size_t at = 0; at < 8; ++at) {
            value = (value << 8U) | static_cast<std::uint64_t>(kDigest[at]);
        }
        gear[index] = value;
    }
    return gear;
}

/// The length of the chunk that begins `bytes`: FastCDC's normalized cut,
/// the hash starting fresh past the minimum.
std::size_t cut(std::span<const std::byte> bytes, const std::array<std::uint64_t, 256>& gear) noexcept {
    const std::size_t kAvailable = std::min(bytes.size(), kMaximumChunk);
    if (kAvailable <= kMinimumChunk) {
        return kAvailable;
    }
    const std::size_t kNormal = std::min(kAvailable, kTargetChunk);
    std::uint64_t hash = 0;
    std::size_t at = kMinimumChunk;
    for (; at < kNormal; ++at) {
        hash = (hash << 1U) + gear[static_cast<std::uint8_t>(bytes[at])];
        if ((hash & kSmallMask) == 0) {
            return at + 1;
        }
    }
    for (; at < kAvailable; ++at) {
        hash = (hash << 1U) + gear[static_cast<std::uint8_t>(bytes[at])];
        if ((hash & kLargeMask) == 0) {
            return at + 1;
        }
    }
    return kAvailable;
}

} // namespace

const std::array<std::uint64_t, 256>& gearTable() noexcept {
    static const std::array<std::uint64_t, 256> kGear = makeGear();
    return kGear;
}

std::vector<std::size_t> chunkEnds(std::span<const std::byte> bytes) {
    std::vector<std::size_t> ends;
    std::size_t at = 0;
    do {
        at += cut(bytes.subspan(at), gearTable());
        ends.push_back(at);
    } while (at < bytes.size());
    return ends;
}

} // namespace rawframe::build
