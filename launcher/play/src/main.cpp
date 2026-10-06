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
// A game that follows a release channel (D434) is updated before it plays:
// with `play.installer`, the launcher first runs that program (an
// rawframe-install) as `follow <library> <subject> <channel> <origin>`,
// which installs the channel's Release if the pointer moved past the one
// the library last followed and verifies. Whatever it ends with (an
// update, nothing new, an origin out of reach, a refusal), the game then
// plays what the library has active, as the server and the client read
// it from `content.library` alone.
//
//   play.installer           the install program (optional)
//   play.library             the library it updates
//   play.follow_subject      the game's subject, `<publisher>/<name>`
//   play.follow_channel      its channel (stable)
//   play.follow_origin       its mirror: an http or https URL, or a
//                            directory under the file's
//   play.follow_authorities  a PEM file of the authorities an https
//                            mirror's certificate is verified against,
//                            instead of the system's (optional)
//   play.follow_log          where the install program's output goes
//   play.follow_ms           how long it has (60000)
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
#include <vector>

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
int stopWithin(process::Child& child, std::chrono::milliseconds grace, std::string_view name = "server") {
    if (const auto kEnded = child.exited()) {
        return *kEnded;
    }
    static_cast<void>(child.requestStop());
    const auto kUntil = std::chrono::steady_clock::now() + grace;
    while (!child.exited().has_value() && std::chrono::steady_clock::now() < kUntil) {
        std::this_thread::sleep_for(kPoll);
    }
    if (!child.exited().has_value()) {
        std::fprintf(stderr,
                     "rawframe-play: the %.*s did not stop within its grace; it is killed\n",
                     static_cast<int>(name.size()),
                     name.data());
        child.kill();
    }
    return child.exited().value_or(-1);
}

/// What following a channel needs, read from the configuration.
struct Follow {
    std::string installer;
    std::string library;
    std::string subject;
    std::string channel;
    std::string origin;
    std::string authorities;
    std::string log;
    std::chrono::milliseconds within{};
};

/// The follow the configuration asks for, if any; false for one it names
/// only in part.
bool followOf(const composition::Configuration& configuration,
              const std::filesystem::path& base,
              std::optional<Follow>& follow) {
    const auto kInstaller = configuration.path("play.installer");
    if (!kInstaller.has_value()) {
        return true;
    }
    const auto kLibrary = configuration.path("play.library");
    const auto kSubject = configuration.text("play.follow_subject");
    const auto kOrigin = configuration.text("play.follow_origin");
    const auto kWithin = configuration.unsignedInteger("play.follow_ms", 60'000);
    if (!kLibrary.has_value() || !kSubject.has_value() || !kOrigin.has_value() || !kWithin.has_value()) {
        return false;
    }
    // A URL as it is; a directory under the file's, as every other path.
    const std::string_view kOriginText = *kOrigin;
    const bool kUrl = kOriginText.starts_with("http://") || kOriginText.starts_with("https://");
    follow = Follow{.installer = std::string{*kInstaller},
                    .library = std::string{*kLibrary},
                    .subject = std::string{*kSubject},
                    .channel = std::string{configuration.text("play.follow_channel").value_or("stable")},
                    .origin = kUrl ? std::string{kOriginText} : (base / std::string{kOriginText}).string(),
                    .authorities = std::string{configuration.path("play.follow_authorities").value_or("")},
                    .log = std::string{configuration.path("play.follow_log").value_or("")},
                    .within = std::chrono::milliseconds{*kWithin}};
    return true;
}

/// Runs the install program's follow, within its time; false when a stop
/// was asked meanwhile. What it ends with is said, never a reason not to
/// play.
bool followed(const Follow& follow, std::chrono::milliseconds grace) {
    std::vector<std::string> arguments{"follow", follow.library, follow.subject, follow.channel, follow.origin};
    if (!follow.authorities.empty()) {
        arguments.insert(arguments.end(), {"--authorities", follow.authorities});
    }
    auto child =
        process::Child::start({.program = follow.installer, .arguments = std::move(arguments), .output = follow.log});
    if (!child.has_value()) {
        std::fprintf(stderr,
                     "rawframe-play: %s; playing what the library has\n",
                     std::string{child.error().description()}.c_str());
        return true;
    }
    const auto kUntil = std::chrono::steady_clock::now() + follow.within;
    while (!child->exited().has_value() && !stopAsked.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < kUntil) {
        std::this_thread::sleep_for(kPoll);
    }
    const bool kInTime = child->exited().has_value();
    const int kCode = stopWithin(*child, grace, "install program");
    if (stopAsked.load(std::memory_order_acquire)) {
        return false;
    }
    if (!kInTime) {
        std::fputs("rawframe-play: following the channel took too long; playing what the library has\n", stderr);
    } else if (kCode != 0) {
        std::fprintf(
            stderr, "rawframe-play: following the channel ended with %d; playing what the library has\n", kCode);
    }
    return true;
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
    std::optional<Follow> follow;
    if (!followOf(configuration, kBase, follow)) {
        std::fputs("rawframe-play: a follow names the installer, the library, the subject, and the origin\n", stderr);
        return 2;
    }
    bridgeStops();
    if (follow.has_value() && !followed(*follow, kGraceTime)) {
        return 1;
    }

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
