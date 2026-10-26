#include "rawframe/process/child.h"

#include "rawframe/process/errors.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
// WIN32_LEAN_AND_MEAN and NOMINMAX come from the build, for every file.
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

/// This process's environment as a block CreateProcessW takes, with its
/// identity for a child that stops with it and the child's own stop
/// event in place of any it was given itself.
std::wstring environmentFor(const ChildSettings& settings, HANDLE stop) {
    const std::wstring kParent = wide(std::string{kParentVariable} + "=");
    const std::wstring kStop = wide(std::string{kStopVariable} + "=");
    std::wstring block;
    if (wchar_t* strings = ::GetEnvironmentStringsW(); strings != nullptr) {
        for (const wchar_t* each = strings; *each != L'\0'; each += std::wstring_view{each}.size() + 1) {
            const std::wstring_view kEntry{each};
            if (!kEntry.starts_with(kParent) && !kEntry.starts_with(kStop)) {
                block.append(kEntry);
                block += L'\0';
            }
        }
        ::FreeEnvironmentStringsW(strings);
    }
    if (settings.stopsWithThis) {
        block += kParent + std::to_wstring(::GetCurrentProcessId());
        block += L'\0';
    }
    block += kStop + std::to_wstring(reinterpret_cast<std::uintptr_t>(stop));
    block += L'\0';
    block += L'\0';
    return block;
}

void closeHandles(std::span<HANDLE const> handles) {
    for (HANDLE each : handles) {
        if (each != nullptr && each != INVALID_HANDLE_VALUE) {
            ::CloseHandle(each);
        }
    }
}
#endif

} // namespace

Child::Child(Child&& other) noexcept
    : id_(std::exchange(other.id_, 0)), handle_(std::exchange(other.handle_, nullptr)),
      stop_(std::exchange(other.stop_, nullptr)), code_(other.code_) {
}

Child& Child::operator=(Child&& other) noexcept {
    if (this != &other) {
        kill();
#if defined(_WIN32)
        closeHandles(std::array<HANDLE, 2>{handle_, stop_});
#endif
        id_ = std::exchange(other.id_, 0);
        handle_ = std::exchange(other.handle_, nullptr);
        stop_ = std::exchange(other.stop_, nullptr);
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
    // Its output, its input, and its stop event as handles it inherits, and
    // only those, so a child started at the same time on another thread
    // holds none of this one's files open.
    SECURITY_ATTRIBUTES inherited{
        .nLength = sizeof(SECURITY_ATTRIBUTES), .lpSecurityDescriptor = nullptr, .bInheritHandle = TRUE};
    const std::wstring kOutput = settings.output.empty() ? std::wstring{L"NUL"} : settings.output.wstring();
    std::array<HANDLE, 3> handed{::CreateFileW(kOutput.c_str(),
                                               GENERIC_WRITE,
                                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                                               &inherited,
                                               CREATE_ALWAYS,
                                               FILE_ATTRIBUTE_NORMAL,
                                               nullptr),
                                 ::CreateFileW(L"NUL",
                                               GENERIC_READ,
                                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                                               &inherited,
                                               OPEN_EXISTING,
                                               FILE_ATTRIBUTE_NORMAL,
                                               nullptr),
                                 ::CreateEventW(&inherited, TRUE, FALSE, nullptr)};
    const auto [kOutputHandle, kInputHandle, kStopHandle] = handed;
    if (kOutputHandle == INVALID_HANDLE_VALUE || kInputHandle == INVALID_HANDLE_VALUE || kStopHandle == nullptr) {
        closeHandles(handed);
        return refuse(ProcessError::StartFailed, "the child's output could not be opened");
    }
    SIZE_T listSize = 0;
    static_cast<void>(::InitializeProcThreadAttributeList(nullptr, 1, 0, &listSize));
    std::vector<std::byte> listBytes(listSize);
    auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(listBytes.data());
    if (::InitializeProcThreadAttributeList(list, 1, 0, &listSize) == FALSE) {
        closeHandles(handed);
        return refuse(ProcessError::StartFailed, "the system has no room to start a process");
    }
    const BOOL kListed = ::UpdateProcThreadAttribute(
        list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handed.data(), sizeof(HANDLE) * handed.size(), nullptr, nullptr);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof startup;
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = kInputHandle;
    startup.StartupInfo.hStdOutput = kOutputHandle;
    startup.StartupInfo.hStdError = kOutputHandle;
    startup.lpAttributeList = list;
    PROCESS_INFORMATION made{};
    std::wstring environment = environmentFor(settings, kStopHandle);
    // A console of its own with no window: a windowed parent (Studio, an
    // exported game's launcher) shows no console for each child, and one in
    // a console keeps that console its own.
    const BOOL kStarted = kListed != FALSE && ::CreateProcessW(kProgram.c_str(),
                                                               line.data(),
                                                               nullptr,
                                                               nullptr,
                                                               TRUE,
                                                               CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT |
                                                                   CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                                                               environment.data(),
                                                               nullptr,
                                                               &startup.StartupInfo,
                                                               &made) != FALSE;
    ::DeleteProcThreadAttributeList(list);
    closeHandles(std::array<HANDLE, 2>{kOutputHandle, kInputHandle});
    if (!kStarted) {
        ::CloseHandle(kStopHandle);
        return refuse(ProcessError::StartFailed, "the system would not start the program");
    }
    ::CloseHandle(made.hThread);
    Child child;
    child.id_ = made.dwProcessId;
    child.handle_ = made.hProcess;
    child.stop_ = kStopHandle;
    return child;
}

Child::~Child() {
    kill();
    closeHandles(std::array<HANDLE, 2>{handle_, stop_});
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
    if (::SetEvent(stop_) == FALSE) {
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
    // This process's environment, with its identity for a child that stops
    // with it in place of any it was given itself.
    std::vector<std::string> told;
    const std::string kParentPrefix = std::string{kParentVariable} + "=";
    for (char** each = environ; *each != nullptr; ++each) {
        if (std::string_view{*each}.substr(0, kParentPrefix.size()) != kParentPrefix) {
            told.emplace_back(*each);
        }
    }
    if (settings.stopsWithThis) {
        told.push_back(kParentPrefix + std::to_string(::getpid()));
    }
    std::vector<char*> envp;
    envp.reserve(told.size() + 1);
    for (std::string& each : told) {
        envp.push_back(each.data());
    }
    envp.push_back(nullptr);
    pid_t pid = 0;
    if (failed == 0) {
        // posix_spawn, not posix_spawnp: the program as it is named.
        failed = posix_spawn(&pid, argv[0], &actions, &attributes, argv.data(), envp.data());
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
