#include "rawframe/test/test.h"

#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <csignal>
#include <string>
#include <windows.h>
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

#if defined(_WIN32)
// Windows has no fork (D237): runInChild runs this executable again with
// `--child <test> <n>`, which runs only that test and, in it, the body of
// its n-th runInChild, then ends. Earlier calls in the child return at once
// and its failed expectations say nothing, so standard error is the body's.
struct ChildRun {
    bool child = false;
    std::string_view test;
    int wanted = 0;
    int calls = 0;
    std::string_view current;
};

ChildRun& childRun() noexcept {
    static ChildRun run;
    return run;
}
#endif

} // namespace

Registration::Registration(std::string_view name, TestFunction function) noexcept {
    registry().push_back(Entry{name, function});
}

void reportFailure(std::string_view file, int line, std::string_view expression) noexcept {
    ++failuresInCurrentTest();
#if defined(_WIN32)
    if (childRun().child) {
        return;
    }
#endif
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
#elif defined(_WIN32)
    ChildRun& run = childRun();
    const int kCall = ++run.calls;
    if (run.child) {
        if (kCall == run.wanted) {
            // No abort dialog and no error report: the parent reads the exit.
            static_cast<void>(_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT));
            body();
            std::_Exit(0);
        }
        return outcome;
    }
    std::wstring self(MAX_PATH, L'\0');
    self.resize(::GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size())));
    std::wstring command = L"\"" + self + L"\" --child ";
    command.append(run.current.begin(), run.current.end());
    command += L" " + std::to_wstring(kCall);
    SECURITY_ATTRIBUTES inherited{.nLength = sizeof(SECURITY_ATTRIBUTES), .bInheritHandle = TRUE};
    HANDLE reading = nullptr;
    HANDLE writing = nullptr;
    if (::CreatePipe(&reading, &writing, &inherited, 0) == 0) {
        outcome.exitCode = -1;
        return outcome;
    }
    ::SetHandleInformation(reading, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{.cb = sizeof(STARTUPINFOW)};
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = ::GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = writing;
    PROCESS_INFORMATION process{};
    const BOOL kStarted =
        ::CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process);
    ::CloseHandle(writing);
    if (kStarted == 0) {
        ::CloseHandle(reading);
        outcome.exitCode = -1;
        return outcome;
    }
    char buffer[512];
    DWORD count = 0;
    while (::ReadFile(reading, buffer, sizeof buffer, &count, nullptr) != 0 && count != 0) {
        outcome.standardError.append(buffer, count);
    }
    ::CloseHandle(reading);
    ::WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 0;
    ::GetExitCodeProcess(process.hProcess, &code);
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    // The C runtime ends an aborted process with 3, SIGABRT's default.
    outcome.signalled = code == 3;
    outcome.signal = outcome.signalled ? SIGABRT : 0;
    outcome.exitCode = outcome.signalled ? 0 : static_cast<int>(code);
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
#if defined(_WIN32)
    // A child runInChild started: one test, exactly, up to its body.
    if (argc == 4 && std::string_view{argv[1]} == "--child") {
        rawframe::test::ChildRun& run = rawframe::test::childRun();
        run.child = true;
        run.test = argv[2];
        run.wanted = std::atoi(argv[3]);
        for (const auto& entry : registry()) {
            if (entry.name == run.test) {
                run.current = entry.name;
                entry.function();
            }
        }
        return 0;
    }
#endif
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
#if defined(_WIN32)
        rawframe::test::childRun().current = entry.name;
        rawframe::test::childRun().calls = 0;
#endif
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
