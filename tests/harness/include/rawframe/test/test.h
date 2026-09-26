#pragma once

// The test harness: a registry of named test functions, one failure macro, and
// a child-process runner for the fatal path. Tests register through static
// objects, which production code may not do (ADR-0010); a test executable is not
// production code and has no composition root to register through.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Whether allocationCount() counts: not under ThreadSanitizer, whose runtime
// owns the global allocation operators.
#if defined(__SANITIZE_THREAD__)
#define RAWFRAME_TEST_COUNTS_ALLOCATIONS 0
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define RAWFRAME_TEST_COUNTS_ALLOCATIONS 0
#endif
#endif
#ifndef RAWFRAME_TEST_COUNTS_ALLOCATIONS
#define RAWFRAME_TEST_COUNTS_ALLOCATIONS 1
#endif

namespace rawframe::test {

using TestFunction = void (*)();

struct Registration {
    Registration(std::string_view name, TestFunction function) noexcept;
};

/// Records a failed expectation. The test keeps running so that one run reports
/// every failed expectation in it.
void reportFailure(std::string_view file, int line, std::string_view expression) noexcept;

/// Global allocations since the process started, for proving a call makes
/// none. Always zero when RAWFRAME_TEST_COUNTS_ALLOCATIONS is 0.
[[nodiscard]] std::size_t allocationCount() noexcept;

/// How a child process ended.
struct ChildOutcome {
    bool signalled = false;
    int signal = 0;
    int exitCode = 0;
    std::string standardError;
};

/// Runs `body` in a child process and reports how the child ended and what it
/// wrote to standard error. For the fatal path, which never returns. A forked
/// child on POSIX; on Windows this executable run again for the calling test
/// alone, whose abort reads as SIGABRT (D237). Not on WebAssembly.
[[nodiscard]] ChildOutcome runInChild(void (*body)()) noexcept;

} // namespace rawframe::test

#define RAWFRAME_TEST_CONCAT_INNER(a, b) a##b
#define RAWFRAME_TEST_CONCAT(a, b) RAWFRAME_TEST_CONCAT_INNER(a, b)

/// Declares and registers a test function.
#define RAWFRAME_TEST(testName)                                                                                        \
    static void testName();                                                                                            \
    static const ::rawframe::test::Registration RAWFRAME_TEST_CONCAT(testName, Registration){#testName, &testName};    \
    static void testName()

/// Fails the current test if the condition is false, and continues.
#define RAWFRAME_EXPECT(conditionExpression)                                                                           \
    do {                                                                                                               \
        if (!(conditionExpression)) {                                                                                  \
            ::rawframe::test::reportFailure(__FILE__, __LINE__, #conditionExpression);                                 \
        }                                                                                                              \
    } while (false)
