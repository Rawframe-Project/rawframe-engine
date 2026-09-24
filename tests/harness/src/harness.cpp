#include "rawframe/test/test.h"

#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace rawframe::test {

namespace {

struct Entry {
    std::string_view name;
    TestFunction function;
};

// Function-local statics so registration order across translation units does
// not matter.
std::vector<Entry>& registry() noexcept {
    static std::vector<Entry> entries;
    return entries;
}

int& failuresInCurrentTest() noexcept {
    static int count = 0;
    return count;
}

} // namespace

Registration::Registration(std::string_view name, TestFunction function) noexcept {
    registry().push_back(Entry{name, function});
}

void reportFailure(std::string_view file, int line, std::string_view expression) noexcept {
    ++failuresInCurrentTest();
    std::fprintf(stderr,
                 "  %.*s:%d: expected %.*s\n",
                 static_cast<int>(file.size()),
                 file.data(),
                 line,
                 static_cast<int>(expression.size()),
                 expression.data());
}

ChildOutcome runInChild(void (*body)()) noexcept {
    ChildOutcome outcome;
#if defined(__unix__) || defined(__APPLE__)
    int pipeEnds[2] = {-1, -1};
    if (::pipe(pipeEnds) != 0) {
        outcome.exitCode = -1;
        return outcome;
    }
    const pid_t child = ::fork();
    if (child == 0) {
        ::dup2(pipeEnds[1], STDERR_FILENO);
        ::close(pipeEnds[0]);
        ::close(pipeEnds[1]);
        body();
        std::_Exit(0);
    }
    ::close(pipeEnds[1]);
    char buffer[512];
    for (;;) {
        const ssize_t count = ::read(pipeEnds[0], buffer, sizeof buffer);
        if (count <= 0) {
            break;
        }
        outcome.standardError.append(buffer, static_cast<std::size_t>(count));
    }
    ::close(pipeEnds[0]);
    int status = 0;
    ::waitpid(child, &status, 0);
    outcome.signalled = WIFSIGNALED(status);
    outcome.signal = outcome.signalled ? WTERMSIG(status) : 0;
    outcome.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 0;
#else
    static_cast<void>(body);
    outcome.exitCode = -1;
#endif
    return outcome;
}

} // namespace rawframe::test

/// Runs every registered test, or only those whose name contains the first
/// argument. `--list` prints the names.
int main(int argc, char** argv) {
    using rawframe::test::registry;
    const std::string_view filter = argc > 1 ? std::string_view{argv[1]} : std::string_view{};
    if (filter == "--list") {
        for (const auto& entry : registry()) {
            std::printf("%.*s\n", static_cast<int>(entry.name.size()), entry.name.data());
        }
        return 0;
    }

    int run = 0;
    int failed = 0;
    for (const auto& entry : registry()) {
        if (!filter.empty() && entry.name.find(filter) == std::string_view::npos) {
            continue;
        }
        rawframe::test::failuresInCurrentTest() = 0;
        entry.function();
        ++run;
        if (rawframe::test::failuresInCurrentTest() != 0) {
            ++failed;
            std::fprintf(stderr, "FAIL %.*s\n", static_cast<int>(entry.name.size()), entry.name.data());
        }
    }
    std::printf("%d run, %d failed\n", run, failed);
    return failed == 0 && run > 0 ? 0 : 1;
}
