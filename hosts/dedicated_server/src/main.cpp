// The dedicated server process entry (ADR-0017): validates its launch, reads
// its configuration, bridges OS stop signals, and runs one Host whose
// composition is the server closure. It owns no engine semantics.
//
//   rawframe-server [--config <file>]

#include "rawframe/composition/configuration.h"
#include "rawframe/composition/registrar.h"
#include "rawframe/host/host.h"
#include "rawframe/world_runtime/registrar.h"

#include <array>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <string>
#include <string_view>

namespace {

// The one process-wide value in the engine: a signal handler can reach nothing
// else. It is only ever set, and only read by the Host loop.
std::atomic<bool> stopRequested{false};

extern "C" void requestStop(int) {
    stopRequested.store(true, std::memory_order_release);
}

void installStopBridge() {
#if defined(__unix__) || defined(__APPLE__)
    struct sigaction action{};
    action.sa_handler = &requestStop;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
#else
    std::signal(SIGINT, &requestStop);
    std::signal(SIGTERM, &requestStop);
#endif
}

bool writeStandardOutput(void*, std::span<const char> bytes) noexcept {
    const bool kWritten = std::fwrite(bytes.data(), 1, bytes.size(), stdout) == bytes.size();
    return std::fflush(stdout) == 0 && kWritten;
}

// The server closure. Kest, networking, and replication join as they land.
constexpr std::array<rawframe::composition::RegistrarEntry, 1> kRegistrars = {
    rawframe::composition::RegistrarEntry{
        "world_runtime", &rawframe::world_runtime::registerParticipants, rawframe::world_runtime::kScopes},
};

// Launch failures happen before diagnostics exist, so they go to stderr as
// plain text and exit with a code of their own.
constexpr int kUsageExit = 64;

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

int main(int argc, char** argv) {
    std::string configurationText;
    for (int index = 1; index < argc; ++index) {
        const std::string_view kArgument{argv[index]};
        if (kArgument == "--config" && index + 1 < argc) {
            if (!readFile(argv[++index], configurationText)) {
                std::fprintf(stderr, "rawframe-server: cannot read the configuration file\n");
                return kUsageExit;
            }
        } else {
            std::fprintf(stderr, "usage: rawframe-server [--config <file>]\n");
            return kUsageExit;
        }
    }
    const auto kConfiguration = rawframe::composition::Configuration::parse(configurationText);
    if (!kConfiguration.has_value()) {
        std::fprintf(stderr,
                     "rawframe-server: %.*s\n",
                     static_cast<int>(kConfiguration.error().description().size()),
                     kConfiguration.error().description().data());
        return kUsageExit;
    }

    installStopBridge();
    const rawframe::host::HostExit kExit = rawframe::host::runHost(rawframe::host::HostRequest{
        .role = rawframe::composition::TargetRole::DedicatedServer,
        .registrars = kRegistrars,
        .configuration = &*kConfiguration,
        .log = {.write = &writeStandardOutput, .context = nullptr},
        .stopRequested = &stopRequested,
    });
    return static_cast<int>(kExit);
}
