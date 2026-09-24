#include "rawframe/base/bits128.h"
#include "rawframe/test/test.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <unordered_set>

using rawframe::base::Bits128;
using rawframe::base::formatBits128Hex;
using rawframe::base::kBits128HexDigits;
using rawframe::base::parseBits128Hex;

namespace {

std::string_view format(const Bits128& value, std::array<char, kBits128HexDigits>& out) {
    formatBits128Hex(value, out);
    return std::string_view{out.data(), out.size()};
}

} // namespace

RAWFRAME_TEST(ParseAndFormatRoundTrip) {
    constexpr std::string_view kText = "0123456789abcdeffedcba9876543210";
    const auto kParsed = parseBits128Hex(kText);
    RAWFRAME_EXPECT(kParsed.parsed);
    RAWFRAME_EXPECT(kParsed.value.high == 0x0123456789abcdefULL);
    RAWFRAME_EXPECT(kParsed.value.low == 0xfedcba9876543210ULL);
    std::array<char, kBits128HexDigits> out{};
    RAWFRAME_EXPECT(format(kParsed.value, out) == kText);
}

RAWFRAME_TEST(ParseIsUsableAtCompileTime) {
    static_assert(parseBits128Hex("00000000000000000000000000000001").value.low == 1);
    static_assert(!parseBits128Hex("0x000000000000000000000000000001").parsed);
}

RAWFRAME_TEST(ParseRejectsEveryNonCanonicalForm) {
    constexpr std::array<std::string_view, 7> kRejected = {
        "",
        "0123456789abcdeffedcba987654321",   // one digit short
        "0123456789abcdeffedcba98765432100", // one digit long
        "0123456789ABCDEFFEDCBA9876543210",  // uppercase
        "0x23456789abcdeffedcba9876543210",  // prefix
        "01234567-9abcdeffedcba9876543210",  // separator
        " 123456789abcdeffedcba9876543210",  // padding
    };
    for (const auto kText : kRejected) {
        const auto kParsed = parseBits128Hex(kText);
        RAWFRAME_EXPECT(!kParsed.parsed);
        RAWFRAME_EXPECT(kParsed.value == Bits128{});
    }
}

RAWFRAME_TEST(OrderingIsHighThenLow) {
    RAWFRAME_EXPECT((Bits128{1, 0} > Bits128{0, ~std::uint64_t{0}}));
    RAWFRAME_EXPECT((Bits128{1, 2} < Bits128{1, 3}));
    RAWFRAME_EXPECT((Bits128{7, 7} == Bits128{7, 7}));
}

RAWFRAME_TEST(HashDistinguishesSwappedHalves) {
    const std::unordered_set<Bits128> kSet{Bits128{1, 2}, Bits128{2, 1}};
    RAWFRAME_EXPECT(kSet.size() == 2);
}
