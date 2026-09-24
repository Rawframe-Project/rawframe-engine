// The secure source fills what it is given, and two fills differ.

#include "rawframe/base/secure_random.h"
#include "rawframe/test/test.h"

#include <array>

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
