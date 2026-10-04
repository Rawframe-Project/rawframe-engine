// What the process tests start (child_test.cpp): `exit <code>` ends with
// that code; `say <words>...` writes its words to its standard output, then
// "said" to its standard error, and ends; `wait` says "waiting", waits for
// a stop request (SIGTERM, or Ctrl+Break on Windows), and ends with code 3,
// or with code 4 after ten seconds without one.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <thread>

#if defined(_WIN32)
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
#endif

} // namespace

int main(int argc, char** argv) {
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
    if (kMode == "wait") {
#if defined(_WIN32)
        ::SetConsoleCtrlHandler(&askStopOnEvent, TRUE);
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
