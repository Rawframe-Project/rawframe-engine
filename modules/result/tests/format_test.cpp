#include "rawframe/result/format.h"
#include "rawframe/result/result.h"
#include "rawframe/test/test.h"

#include <array>
#include <new>
#include <string>
#include <string_view>

using namespace rawframe::result;

namespace {

constexpr ErrorDomain kTestDomain{rawframe::base::parseBits128Hex("00112233445566778899aabbccddeeff").value};

/// An Error at every bound at once: longest description, full frames and
/// context with drops, and the deepest cause chain.
Error worstCase() {
    Error error = fail(ErrorClass::Internal, kTestDomain, ErrorCode{4294967295U}, std::string(400, 'd')).error();
    for (std::size_t depth = 0; depth < kMaximumCauseDepth + 1; ++depth) {
        for (std::size_t index = 0; index < kMaximumFrames + 1; ++index) {
            error = std::move(error).withFrame(std::string(200, 'f'));
        }
        for (std::size_t index = 0; index < kMaximumContextFields + 1; ++index) {
            error = std::move(error).withContext(std::string(40, 'k'), std::string(200, 'v'));
        }
        error = std::move(error).mappedTo(ErrorClass::Internal, kTestDomain, ErrorCode{1}, std::string(400, 'm'));
    }
    return error;
}

} // namespace

RAWFRAME_TEST(TheAllocationCounterCounts) {
    // The allocation-free claims below are only as good as the counter.
    // A volatile pointer, so an optimizer cannot elide the pair.
    static void* volatile held = nullptr;
    const std::size_t kBefore = rawframe::test::allocationCount();
    held = ::operator new(sizeof(int));
    const std::size_t kAfter = rawframe::test::allocationCount();
    ::operator delete(held);
    RAWFRAME_EXPECT(!RAWFRAME_TEST_COUNTS_ALLOCATIONS || kAfter > kBefore);
}

RAWFRAME_TEST(FormattingNamesTheClassDomainCodeAndCause) {
    const Error kError = std::move(fail(ErrorClass::NotFound, kTestDomain, ErrorCode{12}, "no such save").error())
                             .withContext("slot", "2")
                             .mappedTo(ErrorClass::Unavailable, kTestDomain, ErrorCode{1}, "load failed");
    std::array<char, kMaximumFormattedBytes> buffer{};
    const FormatResult kResult = formatError(kError, buffer);
    const std::string_view kText{buffer.data(), kResult.written};
    RAWFRAME_EXPECT(!kResult.truncated);
    RAWFRAME_EXPECT(kText.starts_with("unavailable (domain 00112233445566778899aabbccddeeff, code 1): load failed"));
    RAWFRAME_EXPECT(kText.find("caused by: not_found") != std::string_view::npos);
    RAWFRAME_EXPECT(kText.find("slot=2") != std::string_view::npos);
}

RAWFRAME_TEST(TheWorstCaseFitsTheDocumentedBound) {
    const Error kError = worstCase();
    std::array<char, kMaximumFormattedBytes> buffer{};
    const FormatResult kResult = formatError(kError, buffer);
    RAWFRAME_EXPECT(!kResult.truncated);
    RAWFRAME_EXPECT(kResult.written <= kMaximumFormattedBytes);
}

RAWFRAME_TEST(ASmallDestinationIsNeverOverrun) {
    const Error kError = worstCase();
    std::array<char, 64> buffer{};
    buffer.fill('#');
    const FormatResult kShort = formatError(kError, std::span<char>{buffer.data(), 40});
    RAWFRAME_EXPECT(kShort.truncated && kShort.written == 40);
    RAWFRAME_EXPECT(buffer[40] == '#');
    const FormatResult kEmpty = formatError(kError, std::span<char>{});
    RAWFRAME_EXPECT(kEmpty.truncated && kEmpty.written == 0);
}

RAWFRAME_TEST(FormattingAllocatesNothing) {
    const Error kError = worstCase();
    std::array<char, kMaximumFormattedBytes> buffer{};
    const std::size_t kBefore = rawframe::test::allocationCount();
    static_cast<void>(formatError(kError, buffer));
    RAWFRAME_EXPECT(rawframe::test::allocationCount() == kBefore);
}
