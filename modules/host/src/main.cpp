#include "rawframe/host/main.h"

#if RAWFRAME_THREADS && RAWFRAME_FILE_SYSTEM

#include "rawframe/composition/configuration.h"
#include "rawframe/host/host.h"

#include <array>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <string>

namespace rawframe::host {

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

int hostMain(int argc, char** argv, const ProcessEntry& entry) {
    const auto kName = static_cast<int>(entry.name.size());
    std::string configurationText;
    for (int index = 1; index < argc; ++index) {
        const std::string_view kArgument{argv[index]};
        if (kArgument == "--config" && index + 1 < argc) {
            if (!readFile(argv[++index], configurationText)) {
                std::fprintf(stderr, "%.*s: cannot read the configuration file\n", kName, entry.name.data());
                return kUsageExit;
            }
        } else {
            std::fprintf(stderr, "usage: %.*s [--config <file>]\n", kName, entry.name.data());
            return kUsageExit;
        }
    }
    const auto kConfiguration = composition::Configuration::parse(configurationText);
    if (!kConfiguration.has_value()) {
        std::fprintf(stderr,
                     "%.*s: %.*s\n",
                     kName,
                     entry.name.data(),
                     static_cast<int>(kConfiguration.error().description().size()),
                     kConfiguration.error().description().data());
        return kUsageExit;
    }
    installStopBridge();
    const HostExit kExit = runHost(HostRequest{
        .role = entry.role,
        .registrars = entry.registrars,
        .configuration = &*kConfiguration,
        .log = {.write = &writeStandardOutput, .context = nullptr},
        .stopRequested = &stopRequested,
    });
    return static_cast<int>(kExit);
}

} // namespace rawframe::host
#endif
