#include "rawframe/result/result.h"
#include "rawframe/test/test.h"

#include <memory>
#include <type_traits>

using namespace rawframe::result;

namespace {

constexpr ErrorDomain kTestDomain{rawframe::base::parseBits128Hex("9d3c1b0a8f7e6d5c4b3a291807f6e5d4").value};

int evaluations = 0;

Status succeed() {
    ++evaluations;
    return {};
}

Status refuse() {
    ++evaluations;
    return fail(ErrorClass::PermissionDenied, kTestDomain, ErrorCode{3}, "not yours");
}

Result<int> number(bool ok) {
    ++evaluations;
    if (!ok) {
        return fail(ErrorClass::OutOfRange, kTestDomain, ErrorCode{4}, "too big");
    }
    return 5;
}

Result<std::unique_ptr<int>> boxed() {
    return std::make_unique<int>(11);
}

Status twoTries(bool secondOk) {
    RAWFRAME_TRY(succeed());
    RAWFRAME_TRY(secondOk ? succeed() : refuse());
    return {};
}

Result<int> sum(bool firstOk, bool secondOk) {
    RAWFRAME_TRY_ASSIGN(const int first, number(firstOk));
    RAWFRAME_TRY_ASSIGN(const int second, number(secondOk));
    return first + second;
}

Result<int> unbox() {
    RAWFRAME_TRY_ASSIGN(std::unique_ptr<int> box, boxed());
    return *box;
}

} // namespace

static_assert(std::is_same_v<Result<int>, std::expected<int, Error>>);
static_assert(std::is_same_v<Status, std::expected<void, Error>>);

RAWFRAME_TEST(FailConvertsToEveryResultType) {
    const Result<int> kPlain = fail(ErrorClass::Internal, kTestDomain, ErrorCode{1}, "x");
    const Result<std::unique_ptr<int>> kMoveOnly = fail(ErrorClass::Internal, kTestDomain, ErrorCode{1}, "x");
    const Status kStatus = fail(ErrorClass::Internal, kTestDomain, ErrorCode{1}, "x");
    RAWFRAME_EXPECT(!kPlain.has_value() && !kMoveOnly.has_value() && !kStatus.has_value());
}

RAWFRAME_TEST(TryEvaluatesOnceAndStopsAtTheFirstFailure) {
    evaluations = 0;
    RAWFRAME_EXPECT(twoTries(true).has_value());
    RAWFRAME_EXPECT(evaluations == 2);

    evaluations = 0;
    const Status kFailed = twoTries(false);
    RAWFRAME_EXPECT(!kFailed.has_value());
    RAWFRAME_EXPECT(evaluations == 2);
}

RAWFRAME_TEST(TryAssignBindsAndPropagatesUnchanged) {
    evaluations = 0;
    const Result<int> kOk = sum(true, true);
    RAWFRAME_EXPECT(kOk.has_value() && *kOk == 10);
    RAWFRAME_EXPECT(evaluations == 2);

    evaluations = 0;
    const Result<int> kFailed = sum(false, true);
    RAWFRAME_EXPECT(evaluations == 1);
    RAWFRAME_EXPECT(!kFailed.has_value());
    RAWFRAME_EXPECT(kFailed.error().errorClass() == ErrorClass::OutOfRange);
    RAWFRAME_EXPECT(kFailed.error().description() == "too big");
    RAWFRAME_EXPECT(kFailed.error().frames().empty() && kFailed.error().context().empty());
}

RAWFRAME_TEST(TryAssignMovesAMoveOnlyValue) {
    const Result<int> kUnboxed = unbox();
    RAWFRAME_EXPECT(kUnboxed.has_value() && *kUnboxed == 11);
}
