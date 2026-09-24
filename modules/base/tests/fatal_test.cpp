// The fatal path end to end: each scenario runs in a child process, because the
// path never returns, and checks how the child ended and what it wrote.

#include "rawframe/base/assert.h"
#include "rawframe/base/fatal.h"
#include "rawframe/test/test.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>

using rawframe::base::FatalReason;
using rawframe::base::FatalRecord;
using rawframe::test::runInChild;

namespace {

/// A handler that reports the record it received and exits with a code naming
/// the reason, so the parent can check both.
void recordingHandler(const FatalRecord& record) noexcept {
    std::fprintf(stderr,
                 "reason=%d message=%.*s condition=%.*s",
                 static_cast<int>(record.reason),
                 static_cast<int>(record.message.size()),
                 record.message.data(),
                 static_cast<int>(record.condition.size()),
                 record.condition.data());
    std::fflush(stderr);
    std::_Exit(40 + static_cast<int>(record.reason));
}

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

RAWFRAME_TEST(DefaultHandlerWritesTheRecordAndAborts) {
    const auto kOutcome = runInChild([] {
        RAWFRAME_PANIC("the default path");
    });
    RAWFRAME_EXPECT(kOutcome.signalled);
    RAWFRAME_EXPECT(kOutcome.signal == SIGABRT);
    RAWFRAME_EXPECT(contains(kOutcome.standardError, "rawframe fatal: Panic"));
    RAWFRAME_EXPECT(contains(kOutcome.standardError, "message: the default path"));
    RAWFRAME_EXPECT(contains(kOutcome.standardError, "fatal_test.cpp"));
}

RAWFRAME_TEST(CheckFailureReachesTheInstalledHandler) {
    const auto kOutcome = runInChild([] {
        static_cast<void>(rawframe::base::installFatalHandler(&recordingHandler));
        const int kValue = 3;
        RAWFRAME_CHECK(kValue == 4, "three is not four");
    });
    RAWFRAME_EXPECT(kOutcome.exitCode == 40 + static_cast<int>(FatalReason::CheckFailed));
    RAWFRAME_EXPECT(contains(kOutcome.standardError, "message=three is not four"));
    RAWFRAME_EXPECT(contains(kOutcome.standardError, "condition=kValue == 4"));
}

RAWFRAME_TEST(AssertIsLiveOutsideShipping) {
    const auto kOutcome = runInChild([] {
        static_cast<void>(rawframe::base::installFatalHandler(&recordingHandler));
        RAWFRAME_ASSERT(1 + 1 == 3, "arithmetic");
    });
#if RAWFRAME_ASSERTIONS == 2
    RAWFRAME_EXPECT(kOutcome.exitCode == 40 + static_cast<int>(FatalReason::AssertionFailed));
#else
    // Shipping removes the evaluation: the child returns normally.
    RAWFRAME_EXPECT(!kOutcome.signalled && kOutcome.exitCode == 0);
#endif
}

RAWFRAME_TEST(InstallationIsOnceAndClosesAtTheFreeze) {
    const auto kOutcome = runInChild([] {
        const bool kFirst = rawframe::base::installFatalHandler(&recordingHandler);
        const bool kSecond = rawframe::base::installFatalHandler(&recordingHandler);
        const bool kNull = rawframe::base::installFatalHandler(nullptr);
        std::_Exit(kFirst && !kSecond && !kNull ? 0 : 1);
    });
    RAWFRAME_EXPECT(kOutcome.exitCode == 0);

    const auto kFrozen = runInChild([] {
        rawframe::base::freezeFatalHandler();
        std::_Exit(rawframe::base::installFatalHandler(&recordingHandler) ? 1 : 0);
    });
    RAWFRAME_EXPECT(kFrozen.exitCode == 0);
}

RAWFRAME_TEST(AFailureInsideTheHandlerDoesNotRecurse) {
    const auto kOutcome = runInChild([] {
        static_cast<void>(rawframe::base::installFatalHandler([](const FatalRecord&) noexcept {
            RAWFRAME_PANIC("inside the handler");
        }));
        RAWFRAME_PANIC("outer");
    });
    RAWFRAME_EXPECT(kOutcome.signalled && kOutcome.signal == SIGABRT);
    RAWFRAME_EXPECT(contains(kOutcome.standardError, "FatalPathReentered"));
}
