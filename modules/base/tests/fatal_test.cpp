// The fatal path end to end: each scenario runs in a child process, because the
// path never returns, and checks how the child ended and what it wrote. The
// crash handler too (D617): a fault leaves its bounded record, and the process
// still ends as it would have.

#include "rawframe/base/assert.h"
#include "rawframe/base/fatal.h"
#include "rawframe/test/test.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

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

RAWFRAME_TEST(AFaultLeavesACrashRecordAndEndsTheProcessAsItWould) {
    const auto kOutcome = runInChild([] {
#ifdef _WIN32
        // No error box for the child's fault: the run is a test's.
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
        static_cast<void>(rawframe::base::installCrashCapture());
        // A second installation is refused, and changes nothing.
        if (rawframe::base::installCrashCapture()) {
            std::_Exit(1);
        }
        volatile int* volatile nowhere = nullptr;
        *nowhere = 7;
    });
#ifdef _WIN32
    RAWFRAME_EXPECT(kOutcome.exitCode == static_cast<int>(0xC0000005U));
    RAWFRAME_EXPECT(contains(kOutcome.standardError, "rawframe crash: exception\n  code: 3221225477\n"));
#else
    // Ended by the fault, or by a sanitizer's report of it, the handler
    // before this one.
    RAWFRAME_EXPECT((kOutcome.signalled && kOutcome.signal == SIGSEGV) ||
                    (!kOutcome.signalled && kOutcome.exitCode != 0));
    RAWFRAME_EXPECT(contains(kOutcome.standardError, "rawframe crash: SIGSEGV\n  code: "));
    RAWFRAME_EXPECT(contains(kOutcome.standardError, "\n  address: 0x0000000000000000\n  thread: "));
#endif
    RAWFRAME_EXPECT(contains(kOutcome.standardError, "\n  program: 0x"));
    RAWFRAME_EXPECT(contains(kOutcome.standardError, "\n  frames: 0x"));
}

#ifndef _WIN32
RAWFRAME_TEST(AnAbortTheFatalPathDidNotRaiseLeavesACrashRecord) {
    const auto kOutcome = runInChild([] {
        static_cast<void>(rawframe::base::installCrashCapture());
        std::abort();
    });
    RAWFRAME_EXPECT(kOutcome.signalled && kOutcome.signal == SIGABRT);
#if !defined(__ANDROID__)
    // The x86_64 emulators these tests run on play arm64 programs by
    // translation, which hands a program's own SIGABRT to no handler of its
    // (a raise reaches none either), though its faults reach it (D617).
    RAWFRAME_EXPECT(contains(kOutcome.standardError, "rawframe crash: SIGABRT\n"));
#endif
    // The fatal path's own abort says what it says once.
    const auto kPanicked = runInChild([] {
        static_cast<void>(rawframe::base::installCrashCapture());
        RAWFRAME_PANIC("said once");
    });
    RAWFRAME_EXPECT(kPanicked.signalled && kPanicked.signal == SIGABRT);
    RAWFRAME_EXPECT(contains(kPanicked.standardError, "message: said once") &&
                    !contains(kPanicked.standardError, "rawframe crash"));
}
#endif
