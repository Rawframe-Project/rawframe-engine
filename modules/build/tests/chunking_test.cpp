// SPEC-0021 chunking: the gear table as fixed, chunks within their bounds
// that cover the bytes exactly, the same bytes cut the same way, and an
// edit early in a resource leaving later cuts where they were.

#include "rawframe/base/sha256.h"
#include "rawframe/build/chunking.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace rawframe;
using namespace rawframe::build;

namespace {

/// Bytes no chunker would find structure in: SHA-256 run in counter mode.
std::vector<std::byte> noise(std::size_t size, std::uint8_t seed) {
    std::vector<std::byte> bytes;
    bytes.reserve(size + 32);
    for (std::uint64_t block = 0; bytes.size() < size; ++block) {
        std::array<std::byte, 9> input{};
        input[0] = std::byte{seed};
        for (std::size_t at = 0; at < 8; ++at) {
            input[1 + at] = static_cast<std::byte>(block >> (8U * at));
        }
        const base::Sha256Digest kDigest = base::sha256(input);
        bytes.insert(bytes.end(), kDigest.begin(), kDigest.end());
    }
    bytes.resize(size);
    return bytes;
}

} // namespace

RAWFRAME_TEST(TheGearTableIsAsFixed) {
    // gear[0] is the first eight bytes of SHA-256 of the one byte 0x00,
    // 6e340b9cffb37a98..., big-endian.
    RAWFRAME_EXPECT(gearTable()[0] == 0x6e340b9cffb37a98ULL);
    RAWFRAME_EXPECT(gearTable()[1] != gearTable()[0]);
}

RAWFRAME_TEST(ChunksCoverTheBytesWithinTheirBounds) {
    // Small resources are one chunk, the empty one included.
    RAWFRAME_EXPECT(chunkEnds({}) == std::vector<std::size_t>{0});
    const std::vector<std::byte> kSmall = noise(kMinimumChunk, 1);
    RAWFRAME_EXPECT(chunkEnds(kSmall) == std::vector<std::size_t>{kMinimumChunk});

    const std::vector<std::byte> kLarge = noise(std::size_t{12} * 1024 * 1024, 2);
    const std::vector<std::size_t> kEnds = chunkEnds(kLarge);
    RAWFRAME_EXPECT(kEnds.back() == kLarge.size() && std::ranges::is_sorted(kEnds));
    std::size_t start = 0;
    bool bounded = true;
    for (std::size_t index = 0; index < kEnds.size(); ++index) {
        const std::size_t kSize = kEnds[index] - start;
        const bool kLast = index + 1 == kEnds.size();
        bounded = bounded && kSize <= kMaximumChunk && (kLast || kSize > kMinimumChunk);
        start = kEnds[index];
    }
    RAWFRAME_EXPECT(bounded);
    // Normalized chunking keeps the average near the target: 12 MiB in
    // well over the 3 maximum-size chunks and under the 48 minimum-size.
    RAWFRAME_EXPECT(kEnds.size() > 6 && kEnds.size() < 24);
    RAWFRAME_EXPECT(chunkEnds(kLarge) == kEnds);

    // An edit in the first chunk moves only the cuts near it: every later
    // cut is where it was, shifted by what was inserted.
    std::vector<std::byte> edited(kLarge.begin(), kLarge.begin() + 1000);
    edited.push_back(std::byte{0x5a});
    edited.insert(edited.end(), kLarge.begin() + 1000, kLarge.end());
    const std::vector<std::size_t> kEdited = chunkEnds(edited);
    std::size_t kept = 0;
    for (const std::size_t kEnd : kEnds) {
        kept += std::ranges::count(kEdited, kEnd + 1) != 0 ? 1 : 0;
    }
    RAWFRAME_EXPECT(kept + 2 >= kEnds.size());
}
