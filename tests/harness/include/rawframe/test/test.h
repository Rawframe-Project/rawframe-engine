#pragma once

// The test harness: a registry of named test functions, one failure macro, and
// a child-process runner for the fatal path. Tests register through static
// objects, which production code may not do (ADR-0010); a test executable is not
// production code and has no composition root to register through.

#include <cstdint>
#include <string>
#include <string_view>

namespace rawframe::test {

using TestFunction = void (*)();

struct Registration {
    Registration(std::string_view name, TestFunction function) noexcept;
};

/// Records a failed expectation. The test keeps running so that one run reports
/// every failed expectation in it.
void reportFailure(std::string_view file, int line, std::string_view expression) noexcept;

/// How a child process ended.
struct ChildOutcome {
    bool signalled = false;
    int signal = 0;
    int exitCode = 0;
    std::string standardError;
};

/// Runs `body` in a forked child and reports how the child ended and what it
/// wrote to standard error. For the fatal path, which never returns. POSIX only.
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
