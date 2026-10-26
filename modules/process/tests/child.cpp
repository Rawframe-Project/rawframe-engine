// What the process tests start (child_test.cpp): `exit <code>` ends with
// that code; `say <words>...` writes its words to its standard output, then
// "said" to its standard error, and ends; `wait` says "waiting", waits for
// a stop request (SIGTERM, or on Windows Ctrl+Break or its stop event
// set), and ends with code 3, or with code 4 after ten seconds without one;
// `parent` writes the parent it was told to stop with, or "none".

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <thread>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

namespace {

std::atomic<bool> stopAsked{false};

extern "C" void askStop(int) {
    stopAsked.store(true);
}

#if defined(_WIN32)
BOOL WINAPI askStopOnEvent(DWORD event) {
    if (event == CTRL_BREAK_EVENT) {
        stopAsked.store(true);
        return TRUE;
    }
    return FALSE;
}

VOID CALLBACK askStopOnEventSet(PVOID, BOOLEAN) {
    stopAsked.store(true);
}
#endif

} // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    // Its lines as written, not with the C runtime's carriage returns.
    static_cast<void>(::_setmode(::_fileno(stdout), _O_BINARY));
    static_cast<void>(::_setmode(::_fileno(stderr), _O_BINARY));
#endif
    const std::string_view kMode = argc > 1 ? argv[1] : "";
    if (kMode == "exit" && argc > 2) {
        return std::atoi(argv[2]);
    }
    if (kMode == "say") {
        for (int at = 2; at < argc; ++at) {
            std::fputs(argv[at], stdout);
            std::fputc('|', stdout);
        }
        std::fflush(stdout);
        std::fputs("said\n", stderr);
        return 0;
    }
    if (kMode == "parent") {
        const char* parent = std::getenv("RAWFRAME_PARENT");
        std::fputs(parent != nullptr ? parent : "none", stdout);
        return 0;
    }
    if (kMode == "wait") {
#if defined(_WIN32)
        ::SetConsoleCtrlHandler(&askStopOnEvent, TRUE);
        if (const char* event = std::getenv("RAWFRAME_STOP"); event != nullptr) {
            auto* held = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::strtoull(event, nullptr, 10)));
            HANDLE wait = nullptr;
            static_cast<void>(
                ::RegisterWaitForSingleObject(&wait, held, &askStopOnEventSet, nullptr, INFINITE, WT_EXECUTEONLYONCE));
        }
#else
        std::signal(SIGTERM, &askStop);
#endif
        std::puts("waiting");
        std::fflush(stdout);
        for (int tick = 0; tick < 1000 && !stopAsked.load(); ++tick) {
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
        return stopAsked.load() ? 3 : 4;
    }
    return 2;
}
