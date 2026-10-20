#include "rawframe/host/main.h"

#if RAWFRAME_THREADS && RAWFRAME_FILE_SYSTEM

#include "rawframe/base/fatal.h"
#include "rawframe/composition/configuration.h"
#include "rawframe/host/host.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <cstdlib>
#include <windows.h>
#elif defined(__linux__)
#include <cstdlib>
#include <sys/prctl.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <cstdlib>
#include <dispatch/dispatch.h>
#include <unistd.h>
#endif

namespace rawframe::host {

namespace {

// The one process-wide value in the engine: a signal handler can reach nothing
// else. It is only ever set, and only read by the Host loop or a tool's.
std::atomic<bool> stopped{false};

extern "C" void requestStop(int) {
    stopped.store(true, std::memory_order_release);
}

#if defined(_WIN32)
/// Windows asks a console process to stop with a control event, on a thread
/// of its own: Ctrl+C, Ctrl+Break, the console closing, logoff, or shutdown
/// (D237).
BOOL WINAPI requestStopOnEvent(DWORD event) {
    switch (event) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        requestStop(0);
        return TRUE;
    default:
        return FALSE;
    }
}

VOID CALLBACK requestStopOnParentEnd(PVOID, BOOLEAN) {
    requestStop(0);
}

/// A program started to stop with its parent (D646) waits on it through the
/// system's thread pool and stops at once if it has already gone. A process
/// now under the parent's identity began after this one did, so it is not
/// the parent.
void stopWithParent(const char* parent) {
    const auto kIdentity = static_cast<DWORD>(std::strtoul(parent, nullptr, 10));
    HANDLE held = ::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, kIdentity);
    if (held == nullptr) {
        // Refused for another reason than its absence: not watched.
        if (::GetLastError() == ERROR_INVALID_PARAMETER) {
            requestStop(0);
        }
        return;
    }
    FILETIME parentBegan{};
    FILETIME thisBegan{};
    std::array<FILETIME, 3> rest{};
    const bool kTimed = ::GetProcessTimes(held, &parentBegan, &rest[0], &rest[1], &rest[2]) != FALSE &&
                        ::GetProcessTimes(::GetCurrentProcess(), &thisBegan, &rest[0], &rest[1], &rest[2]) != FALSE;
    HANDLE wait = nullptr;
    if (kTimed && ::CompareFileTime(&parentBegan, &thisBegan) > 0) {
        requestStop(0);
    } else if (::RegisterWaitForSingleObject(
                   &wait, held, &requestStopOnParentEnd, nullptr, INFINITE, WT_EXECUTEONLYONCE) != FALSE) {
        // Kept, with the wait, for the rest of the process's life.
        return;
    }
    ::CloseHandle(held);
}
#elif defined(__APPLE__)
void requestStopOnParentEnd(void*) {
    requestStop(SIGTERM);
}

/// A program started to stop with its parent (D646) is told by the system
/// when the parent ends, on a queue of its own, and stops at once if it has
/// already: an orphan is a child of launchd.
void stopWithParent(const char* parent) {
    const auto kIdentity = static_cast<pid_t>(std::strtol(parent, nullptr, 10));
    // Kept for the rest of the process's life.
    dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_PROC,
                                                      static_cast<uintptr_t>(kIdentity),
                                                      DISPATCH_PROC_EXIT,
                                                      dispatch_get_global_queue(QOS_CLASS_UTILITY, 0));
    if (source != nullptr) {
        dispatch_source_set_event_handler_f(source, &requestStopOnParentEnd);
        dispatch_resume(source);
    }
    if (::getppid() != kIdentity) {
        requestStop(SIGTERM);
    }
}
#endif

bool writeStandardOutput(void*, std::span<const char> bytes) noexcept {
    const bool kWritten = std::fwrite(bytes.data(), 1, bytes.size(), stdout) == bytes.size();
    return std::fflush(stdout) == 0 && kWritten;
}

bool readFile(const char* path, std::string& text) {
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }
    std::array<char, 4096> buffer{};
    std::size_t read = 0;
    while ((read = std::fread(buffer.data(), 1, buffer.size(), file)) > 0) {
        text.append(buffer.data(), read);
    }
    const bool kOk = std::ferror(file) == 0;
    std::fclose(file);
    return kOk;
}

} // namespace

void installStopBridge() {
#if defined(_WIN32)
    ::SetConsoleCtrlHandler(&requestStopOnEvent, TRUE);
    if (const char* parent = std::getenv("RAWFRAME_PARENT"); parent != nullptr) {
        stopWithParent(parent);
    }
#elif defined(__unix__) || defined(__APPLE__)
    struct sigaction action{};
    action.sa_handler = &requestStop;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
#if defined(__linux__)
    // A program started to stop with its parent (D646) is sent a stop
    // signal when the parent goes, and stops at once if it has already.
    if (const char* parent = std::getenv("RAWFRAME_PARENT"); parent != nullptr) {
        ::prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (std::to_string(::getppid()) != parent) {
            requestStop(SIGTERM);
        }
    }
#elif defined(__APPLE__)
    if (const char* parent = std::getenv("RAWFRAME_PARENT"); parent != nullptr) {
        stopWithParent(parent);
    }
#endif
#else
    std::signal(SIGINT, &requestStop);
    std::signal(SIGTERM, &requestStop);
#endif
}

void settleStopBridge() {
#if defined(__unix__) || defined(__APPLE__)
    // Stopped, a process has nothing left to stop: a stop signal or its
    // parent's going is let fall rather than taken while the process ends,
    // where the address sanitizer's leak check traces it and a signal that
    // came then left that check waiting for good (D820).
    std::signal(SIGINT, SIG_IGN);
    std::signal(SIGTERM, SIG_IGN);
#if defined(__linux__)
    ::prctl(PR_SET_PDEATHSIG, 0);
#endif
#endif
}

const std::atomic<bool>& stopRequested() noexcept {
    return stopped;
}

int hostMain(int argc, char** argv, const ProcessEntry& entry) {
    const auto kName = static_cast<int>(entry.name.size());
    std::string configurationText;
    // Relative paths in the configuration are under its own directory, so
    // where the process starts from never changes what it names (D188).
    std::string base;
    for (int index = 1; index < argc; ++index) {
        const std::string_view kArgument{argv[index]};
        if (kArgument == "--config" && index + 1 < argc) {
            std::error_code error;
            base = std::filesystem::absolute(argv[index + 1], error).parent_path().string();
            if (error || !readFile(argv[++index], configurationText)) {
                std::fprintf(stderr, "%.*s: cannot read the configuration file\n", kName, entry.name.data());
                return exitCode(HostExit::InvalidInvocation);
            }
        } else if (!entry.fileKey.empty() && !kArgument.starts_with("--")) {
            // A file opened with the program (D920): one line of the
            // configuration, so a path holding a line's end or another
            // control character, which could add lines of its own, is
            // refused.
            std::error_code error;
            const std::string kOpened = std::filesystem::absolute(argv[index], error).generic_string();
            if (error || std::ranges::any_of(kOpened, [](char each) {
                    return static_cast<unsigned char>(each) < 0x20 || each == 0x7F;
                })) {
                std::fprintf(stderr, "%.*s: the file's path cannot be read\n", kName, entry.name.data());
                return exitCode(HostExit::InvalidInvocation);
            }
            configurationText += "\n" + std::string{entry.fileKey} + " = " + kOpened + "\n";
        } else {
            std::fprintf(stderr, "usage: %.*s [--config <file>]\n", kName, entry.name.data());
            return exitCode(HostExit::InvalidInvocation);
        }
    }
    // The program's own defaults where the file names none of a key (D920).
    if (!entry.defaults.empty()) {
        const auto kGiven = composition::Configuration::parse(configurationText, base);
        std::string_view rest = entry.defaults;
        while (!rest.empty() && kGiven.has_value()) {
            const std::size_t kEnd = rest.find('\n');
            const std::string_view kLine = rest.substr(0, kEnd);
            rest = kEnd == std::string_view::npos ? std::string_view{} : rest.substr(kEnd + 1);
            const std::size_t kEquals = kLine.find('=');
            if (kEquals == std::string_view::npos) {
                continue;
            }
            std::string_view key = kLine.substr(0, kEquals);
            while (!key.empty() && key.back() == ' ') {
                key.remove_suffix(1);
            }
            if (!kGiven->text(key).has_value()) {
                configurationText += "\n" + std::string{kLine} + "\n";
            }
        }
    }
    const auto kConfiguration = composition::Configuration::parse(configurationText, base);
    if (!kConfiguration.has_value()) {
        std::fprintf(stderr,
                     "%.*s: %.*s\n",
                     kName,
                     entry.name.data(),
                     static_cast<int>(kConfiguration.error().description().size()),
                     kConfiguration.error().description().data());
        return exitCode(HostExit::InvalidLaunchDescriptor);
    }
    installStopBridge();
    // A fault leaves its bounded record on standard error before the process
    // ends as it would have (ADR-0017, D617).
    static_cast<void>(base::installCrashCapture());
    const HostRequest kRequest{
        .role = entry.role,
        .registrars = entry.registrars,
        .configuration = &*kConfiguration,
        .log = {.write = &writeStandardOutput, .context = nullptr},
        .stopRequested = &stopped,
        .defaultShutdownBudgetMs = entry.defaultShutdownBudgetMs,
    };
    const HostExit kExit = entry.drive != nullptr ? entry.drive(kRequest) : runHost(kRequest);
    settleStopBridge();
    return exitCode(kExit);
}

} // namespace rawframe::host
#endif
