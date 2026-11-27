// The secure source fills what it is given, two fills differ, and its words
// do not repeat.

#include "rawframe/base/secure_random.h"
#include "rawframe/test/test.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

using namespace rawframe::base;

RAWFRAME_TEST(SecureRandomFillsAndDiffers) {
    std::array<std::byte, 32> first{};
    std::array<std::byte, 32> second{};
    RAWFRAME_EXPECT(fillSecureRandom(first));
    RAWFRAME_EXPECT(fillSecureRandom(second));
    // Equal 256-bit draws would mean the source is not random at all.
    RAWFRAME_EXPECT(first != second);
    RAWFRAME_EXPECT(fillSecureRandom({}));
}

RAWFRAME_TEST(SecureRandomWordsDoNotRepeat) {
    // A thousand words of a working source never meet: a repeat here is a
    // source that answers zeros or a constant, as some processors' RDSEED
    // does under load.
    std::vector<std::uint64_t> words;
    for (int each = 0; each < 1000; ++each) {
        words.push_back(secureRandomWord());
    }
    std::ranges::sort(words);
    RAWFRAME_EXPECT(std::ranges::adjacent_find(words) == words.end());
}
