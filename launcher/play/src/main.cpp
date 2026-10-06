// A standalone game's launcher (ADR-0041's standalone export; ADR-0084: a
// launcher may start a local dedicated-server process, D395). It starts the
// game's dedicated server, waits for the certificate fingerprint the server
// writes as it listens, starts the client pinned to that file, and when the
// client ends, asks the server to stop as a Host stops and kills it if it
// has not within its grace. A stop asked of the launcher (Ctrl+C, SIGTERM,
// the console closing) is passed on the same way, client first.
//
//   rawframe-play [--config <file>]
//
// With no file it reads `play.conf` beside its own executable. Paths in the
// file are under the file's directory:
//
//   play.server          the dedicated server's executable
//   play.server_config   its configuration, which writes the fingerprint
//   play.server_log      where its output goes (optional)
//   play.client          the client's executable (any process that plays)
//   play.client_config   its configuration, which pins the fingerprint
//   play.client_log      where its output goes (optional)
//   play.fingerprint     the file the server writes and the client pins
//   play.wait_ms         how long the server has to listen (30000)
//   play.grace_ms        how long it has to stop (10000)
//
// It ends with the client's exit code, or 1 when the server did not listen
// or ended first, or 2 for a configuration it cannot read.

#include "rawframe/composition/configuration.h"
#include "rawframe/process/child.h"
#include "rawframe/process/self.h"

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#if defined(_WIN32)
// WIN32_LEAN_AND_MEAN and NOMINMAX come from the build, for every file (D237).
#include <windows.h>
#endif

namespace {

using namespace rawframe;

// This program's one process-wide value, as a Host's: a signal handler can
// reach nothing else. It is only ever set, and only read by the loop below.
std::atomic<bool> stopAsked{false};

extern "C" void askStop(int) {
    stopAsked.store(true, std::memory_order_release);
}

#if defined(_WIN32)
BOOL WINAPI askStopOnEvent(DWORD event) {
    switch (event) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        askStop(0);
        return TRUE;
    default:
        return FALSE;
    }
}
#endif

void bridgeStops() {
#if defined(_WIN32)
    ::SetConsoleCtrlHandler(&askStopOnEvent, TRUE);
#else
    std::signal(SIGINT, &askStop);
    std::signal(SIGTERM, &askStop);
#endif
}

constexpr auto kPoll = std::chrono::milliseconds{20};

bool readFile(const std::filesystem::path& path, std::string& text) {
    std::FILE* file = std::fopen(path.string().c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    std::array<char, 4096> buffer{};
    std::size_t read = 0;
    while ((read = std::fread(buffer.data(), 1, buffer.size(), file)) > 0) {
        text.append(buffer.data(), read);
    }
    const bool kRead = std::ferror(file) == 0;
    std::fclose(file);
    return kRead;
}

/// Whether the file holds something, as the server writes it once it
/// listens.
bool written(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && std::filesystem::file_size(path, error) > 0 && !error;
}

/// Asks the child to stop and waits its grace, then kills it; its exit code.
int stopWithin(process::Child& child, std::chrono::milliseconds grace) {
    if (const auto kEnded = child.exited()) {
        return *kEnded;
    }
    static_cast<void>(child.requestStop());
    const auto kUntil = std::chrono::steady_clock::now() + grace;
    while (!child.exited().has_value() && std::chrono::steady_clock::now() < kUntil) {
        std::this_thread::sleep_for(kPoll);
    }
    if (!child.exited().has_value()) {
        std::fputs("rawframe-play: the server did not stop within its grace; it is killed\n", stderr);
        child.kill();
    }
    return child.exited().value_or(-1);
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path file;
    if (argc == 3 && std::string_view{argv[1]} == "--config") {
        file = argv[2];
    } else if (argc == 1) {
        std::error_code error;
        file = std::filesystem::canonical(process::ownExecutable(), error).parent_path() / "play.conf";
    } else {
        std::fputs("usage: rawframe-play [--config <file>]\n", stderr);
        return 2;
    }
    std::error_code error;
    const std::filesystem::path kBase = std::filesystem::absolute(file, error).parent_path();
    std::string text;
    if (error || !readFile(file, text)) {
        std::fprintf(stderr, "rawframe-play: cannot read %s\n", file.string().c_str());
        return 2;
    }
    const auto kConfiguration = composition::Configuration::parse(text, kBase.string());
    if (!kConfiguration.has_value()) {
        std::fprintf(stderr, "rawframe-play: %s\n", std::string{kConfiguration.error().description()}.c_str());
        return 2;
    }
    const composition::Configuration& configuration = *kConfiguration;
    const auto kServer = configuration.path("play.server");
    const auto kServerConfig = configuration.path("play.server_config");
    const auto kClient = configuration.path("play.client");
    const auto kClientConfig = configuration.path("play.client_config");
    const auto kFingerprint = configuration.path("play.fingerprint");
    const auto kWait = configuration.unsignedInteger("play.wait_ms", 30'000);
    const auto kGrace = configuration.unsignedInteger("play.grace_ms", 10'000);
    if (!kServer || !kServerConfig || !kClient || !kClientConfig || !kFingerprint || !kWait.has_value() ||
        !kGrace.has_value()) {
        std::fputs("rawframe-play: the configuration names the server, the client, their configurations, and the "
                   "fingerprint\n",
                   stderr);
        return 2;
    }
    const std::chrono::milliseconds kGraceTime{*kGrace};
    bridgeStops();

    // A fingerprint left from an earlier run is not this server's.
    std::filesystem::remove(*kFingerprint, error);
    auto server = process::Child::start({.program = *kServer,
                                         .arguments = {"--config", *kServerConfig},
                                         .output = configuration.path("play.server_log").value_or("")});
    if (!server.has_value()) {
        std::fprintf(stderr, "rawframe-play: %s\n", std::string{server.error().description()}.c_str());
        return 1;
    }
    const auto kUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds{*kWait};
    while (!written(*kFingerprint)) {
        if (server->exited().has_value() || stopAsked.load(std::memory_order_acquire) ||
            std::chrono::steady_clock::now() > kUntil) {
            std::fputs("rawframe-play: the server did not listen\n", stderr);
            stopWithin(*server, kGraceTime);
            return 1;
        }
        std::this_thread::sleep_for(kPoll);
    }

    auto client = process::Child::start({.program = *kClient,
                                         .arguments = {"--config", *kClientConfig},
                                         .output = configuration.path("play.client_log").value_or("")});
    if (!client.has_value()) {
        std::fprintf(stderr, "rawframe-play: %s\n", std::string{client.error().description()}.c_str());
        stopWithin(*server, kGraceTime);
        return 1;
    }
    // Until the client ends, the server ends, or a stop is asked.
    while (!client->exited().has_value() && !server->exited().has_value() &&
           !stopAsked.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(kPoll);
    }
    const bool kServerFirst = server->exited().has_value() && !client->exited().has_value();
    const int kClientCode = stopWithin(*client, kGraceTime);
    const int kServerCode = stopWithin(*server, kGraceTime);
    if (kServerFirst) {
        std::fprintf(stderr, "rawframe-play: the server ended first, with %d\n", kServerCode);
        return 1;
    }
    return kClientCode;
}
