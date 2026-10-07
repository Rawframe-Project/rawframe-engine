#include "rawframe/process/child.h"

#include "rawframe/process/errors.h"

#include <string>
#include <string_view>
#include <utility>

#if defined(_WIN32)
// WIN32_LEAN_AND_MEAN and NOMINMAX come from the build, for every file (D237).
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace rawframe::process {

namespace {

std::unexpected<result::Error> refuse(ProcessError error, std::string_view why) {
    return result::fail(result::ErrorClass::Unavailable, kProcessDomain, code(error), why);
}

#if defined(_WIN32)
std::wstring wide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int kLength = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring made(static_cast<std::size_t>(kLength), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), made.data(), kLength);
    return made;
}

/// One argument as the C runtime's command-line parser reads it back:
/// quoted, its quotes escaped, and the backslashes before a quote doubled.
void appendQuoted(std::wstring& line, const std::wstring& argument) {
    line += L'"';
    std::size_t backslashes = 0;
    for (const wchar_t kCharacter : argument) {
        if (kCharacter == L'\\') {
            ++backslashes;
            continue;
        }
        if (kCharacter == L'"') {
            line.append((backslashes * 2) + 1, L'\\');
        } else {
            line.append(backslashes, L'\\');
        }
        backslashes = 0;
        line += kCharacter;
    }
    line.append(backslashes * 2, L'\\');
    line += L'"';
}
#endif

} // namespace

Child::Child(Child&& other) noexcept
    : id_(std::exchange(other.id_, 0)), handle_(std::exchange(other.handle_, nullptr)), code_(other.code_) {
}

Child& Child::operator=(Child&& other) noexcept {
    if (this != &other) {
        kill();
#if defined(_WIN32)
        if (handle_ != nullptr) {
            ::CloseHandle(handle_);
        }
#endif
        id_ = std::exchange(other.id_, 0);
        handle_ = std::exchange(other.handle_, nullptr);
        code_ = other.code_;
    }
    return *this;
}

#if defined(_WIN32)

result::Result<Child> Child::start(const ChildSettings& settings) {
    const std::wstring kProgram = settings.program.wstring();
    std::wstring line;
    appendQuoted(line, kProgram);
    for (const std::string& argument : settings.arguments) {
        line += L' ';
        appendQuoted(line, wide(argument));
    }
    // Its output and input as handles it inherits; it shares this process's
    // console, so a Ctrl+Break to its own group reaches it.
    SECURITY_ATTRIBUTES inherited{
        .nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = nullptr, .bInheritHandle = TRUE};
    const std::wstring kOutput = settings.output.empty() ? std::wstring{L"NUL"} : settings.output.wstring();
    HANDLE output = ::CreateFileW(kOutput.c_str(),
                                  GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  &inherited,
                                  CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
    HANDLE input = ::CreateFileW(L"NUL",
                                 GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 &inherited,
                                 OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL,
                                 nullptr);
    if (output == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) {
        if (output != INVALID_HANDLE_VALUE) {
            ::CloseHandle(output);
        }
        if (input != INVALID_HANDLE_VALUE) {
            ::CloseHandle(input);
        }
        return refuse(ProcessError::StartFailed, "the child's output could not be opened");
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = output;
    startup.hStdError = output;
    PROCESS_INFORMATION made{};
    const BOOL kStarted = ::CreateProcessW(kProgram.c_str(),
                                           line.data(),
                                           nullptr,
                                           nullptr,
                                           TRUE,
                                           CREATE_NEW_PROCESS_GROUP,
                                           nullptr,
                                           nullptr,
                                           &startup,
                                           &made);
    ::CloseHandle(output);
    ::CloseHandle(input);
    if (kStarted == FALSE) {
        return refuse(ProcessError::StartFailed, "the system would not start the program");
    }
    ::CloseHandle(made.hThread);
    Child child;
    child.id_ = made.dwProcessId;
    child.handle_ = made.hProcess;
    return child;
}

Child::~Child() {
    kill();
    if (handle_ != nullptr) {
        ::CloseHandle(handle_);
    }
}

std::optional<int> Child::exited() noexcept {
    if (code_.has_value() || handle_ == nullptr) {
        return code_;
    }
    if (::WaitForSingleObject(handle_, 0) == WAIT_OBJECT_0) {
        DWORD exitCode = 0;
        ::GetExitCodeProcess(handle_, &exitCode);
        code_ = static_cast<int>(exitCode);
    }
    return code_;
}

result::Status Child::requestStop() noexcept {
    if (handle_ == nullptr || exited().has_value()) {
        return refuse(ProcessError::StopFailed, "the child has ended");
    }
    if (::GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, static_cast<DWORD>(id_)) == FALSE) {
        return refuse(ProcessError::StopFailed, "the system would not tell the child to stop");
    }
    return {};
}

void Child::kill() noexcept {
    if (handle_ == nullptr || exited().has_value()) {
        return;
    }
    ::TerminateProcess(handle_, 1);
    ::WaitForSingleObject(handle_, INFINITE);
    DWORD exitCode = 0;
    ::GetExitCodeProcess(handle_, &exitCode);
    code_ = static_cast<int>(exitCode);
}

#else

result::Result<Child> Child::start(const ChildSettings& settings) {
    std::vector<std::string> words;
    words.reserve(settings.arguments.size() + 1);
    words.push_back(settings.program.string());
    words.insert(words.end(), settings.arguments.begin(), settings.arguments.end());
    std::vector<char*> argv;
    argv.reserve(words.size() + 1);
    for (std::string& word : words) {
        argv.push_back(word.data());
    }
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        return refuse(ProcessError::StartFailed, "the system has no room to start a process");
    }
    posix_spawnattr_t attributes;
    if (posix_spawnattr_init(&attributes) != 0) {
        posix_spawn_file_actions_destroy(&actions);
        return refuse(ProcessError::StartFailed, "the system has no room to start a process");
    }
    // Its input from nowhere, its output and error to the one file; its
    // stop signals at their defaults and none blocked, whatever this process
    // set for itself, so it can bridge them as a Host does.
    const std::string kOutput = settings.output.empty() ? std::string{"/dev/null"} : settings.output.string();
    sigset_t defaults;
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGINT);
    sigaddset(&defaults, SIGTERM);
    sigaddset(&defaults, SIGPIPE);
    sigset_t none;
    sigemptyset(&none);
    int failed = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    if (failed == 0) {
        failed = posix_spawn_file_actions_addopen(
            &actions, STDOUT_FILENO, kOutput.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    }
    if (failed == 0) {
        failed = posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    }
    if (failed == 0) {
        failed = posix_spawnattr_setsigdefault(&attributes, &defaults);
    }
    if (failed == 0) {
        failed = posix_spawnattr_setsigmask(&attributes, &none);
    }
    if (failed == 0) {
        failed = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
    }
    pid_t pid = 0;
    if (failed == 0) {
        // posix_spawn, not posix_spawnp: the program as it is named.
        failed = posix_spawn(&pid, argv[0], &actions, &attributes, argv.data(), environ);
    }
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    if (failed != 0) {
        // The system's error number says why: the program, its output's
        // file, or no room for another process.
        return std::unexpected{refuse(ProcessError::StartFailed, "the system would not start the program")
                                   .error()
                                   .withContext("errno", std::to_string(failed))};
    }
    Child child;
    child.id_ = static_cast<std::uint64_t>(pid);
    return child;
}

Child::~Child() {
    kill();
}

std::optional<int> Child::exited() noexcept {
    if (code_.has_value() || id_ == 0) {
        return code_;
    }
    int status = 0;
    const pid_t kReaped = waitpid(static_cast<pid_t>(id_), &status, WNOHANG);
    if (kReaped == static_cast<pid_t>(id_)) {
        code_ = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    } else if (kReaped < 0) {
        // Not this process's child any more: reaped elsewhere, so ended.
        code_ = -1;
    }
    return code_;
}

result::Status Child::requestStop() noexcept {
    if (id_ == 0 || exited().has_value()) {
        return refuse(ProcessError::StopFailed, "the child has ended");
    }
    if (::kill(static_cast<pid_t>(id_), SIGTERM) != 0) {
        return refuse(ProcessError::StopFailed, "the system would not tell the child to stop");
    }
    return {};
}

void Child::kill() noexcept {
    if (id_ == 0 || exited().has_value()) {
        return;
    }
    ::kill(static_cast<pid_t>(id_), SIGKILL);
    int status = 0;
    if (waitpid(static_cast<pid_t>(id_), &status, 0) == static_cast<pid_t>(id_)) {
        code_ = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    } else {
        code_ = -1;
    }
}

#endif

} // namespace rawframe::process
