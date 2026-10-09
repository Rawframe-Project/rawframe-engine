// The Android client (ADR-0084's first mobile target, D551): the client as
// the shared library a Maul Window activity starts. On Android the platform
// starts a program, not a `main`: the activity asks the window module for
// one when it is created, and the window module asks this library
// (`window::androidStart`). The program plays the process's own player as
// the desktop client does, from the window's input and frames.
//
// Its files are the app's own: the configuration is `client.conf` in the
// app's files directory (`/data/data/<package>/files`), whose relative paths
// are under it (D188), and the run's NDJSON diagnostics go to `client.log`
// beside it, since an app has no standard output anyone reads. Nothing is
// read from the package's assets yet.

#include "rawframe/composition/configuration.h"
#include "rawframe/composition/registrar.h"
#include "rawframe/game_content/registrar.h"
#include "rawframe/host/host.h"
#include "rawframe/input_kest/registrar.h"
#include "rawframe/network_quic/registrar.h"
#include "rawframe/render/registrar.h"
#include "rawframe/render_canvas/registrar.h"
#include "rawframe/render_canvas_gpu/registrar.h"
#include "rawframe/render_scene/registrar.h"
#include "rawframe/render_scene_gpu/registrar.h"
#include "rawframe/window/windows.h"
#include "rawframe/window_host/window_host.h"
#include "rawframe/world_animation/registrar.h"
#include "rawframe/world_audio/registrar.h"
#include "rawframe/world_kest/registrar.h"
#include "rawframe/world_localization/registrar.h"
#include "rawframe/world_replication/registrar.h"
#include "rawframe/world_runtime/registrar.h"
#include "rawframe/world_ui/registrar.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace {

using namespace rawframe;

constexpr std::array<composition::RegistrarEntry, 15> kRegistrars = {
    composition::RegistrarEntry{"game_content", &game_content::registerParticipants, game_content::kScopes},
    composition::RegistrarEntry{"network_quic", &network_quic::registerParticipants, network_quic::kScopes},
    composition::RegistrarEntry{"input_kest", &input_kest::registerParticipants, input_kest::kScopes},
    composition::RegistrarEntry{"render_canvas", &render_canvas::registerParticipants, render_canvas::kScopes},
    composition::RegistrarEntry{"world_ui", &world_ui::registerParticipants, world_ui::kScopes},
    composition::RegistrarEntry{"render_scene", &render_scene::registerParticipants, render_scene::kScopes},
    composition::RegistrarEntry{"world_animation", &world_animation::registerParticipants, world_animation::kScopes},
    composition::RegistrarEntry{"world_audio", &world_audio::registerParticipants, world_audio::kScopes},
    composition::RegistrarEntry{"world_kest", &world_kest::registerParticipants, world_kest::kScopes},
    composition::RegistrarEntry{
        "world_localization", &world_localization::registerParticipants, world_localization::kScopes},
    composition::RegistrarEntry{
        "world_replication", &world_replication::registerParticipants, world_replication::kScopes},
    composition::RegistrarEntry{"world_runtime", &world_runtime::registerParticipants, world_runtime::kScopes},
    composition::RegistrarEntry{"render", &render::registerParticipants, render::kScopes},
    composition::RegistrarEntry{
        "render_canvas_gpu", &render_canvas_gpu::registerParticipants, render_canvas_gpu::kScopes},
    composition::RegistrarEntry{"render_scene_gpu", &render_scene_gpu::registerParticipants, render_scene_gpu::kScopes},
};

/// The app's files directory, from the process's name, which Android sets to
/// the package's (a process of its own named `<package>:<name>` keeps the
/// package's directory).
std::optional<std::string> filesDirectory() {
    std::FILE* file = std::fopen("/proc/self/cmdline", "rb");
    if (file == nullptr) {
        return std::nullopt;
    }
    std::array<char, 256> name{};
    const std::size_t kRead = std::fread(name.data(), 1, name.size() - 1, file);
    std::fclose(file);
    std::string package{name.data(), std::min(kRead, std::char_traits<char>::length(name.data()))};
    package = package.substr(0, package.find(':'));
    if (package.empty() || package.find('/') != std::string::npos) {
        return std::nullopt;
    }
    return "/data/data/" + package + "/files";
}

std::optional<std::string> readText(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return std::nullopt;
    }
    std::string text;
    std::array<char, 4096> chunk{};
    for (std::size_t read = 0; (read = std::fread(chunk.data(), 1, chunk.size(), file)) > 0;) {
        text.append(chunk.data(), read);
    }
    std::fclose(file);
    return text;
}

bool writeLog(void* context, std::span<const char> bytes) noexcept {
    auto* log = static_cast<std::FILE*>(context);
    const bool kWritten = std::fwrite(bytes.data(), 1, bytes.size(), log) == bytes.size();
    return std::fflush(log) == 0 && kWritten;
}

/// The process's client, made when an activity asks and no program runs,
/// and kept while it runs, as the program outlives its activities.
struct AndroidClient {
    std::FILE* log = nullptr;
    std::optional<composition::Configuration> configuration;
    std::atomic<bool> stopRequested{false};
    std::unique_ptr<window_host::WindowHost> player;

    /// The player, or none when the configuration cannot be read, said in
    /// the log where there is one.
    window::Program* prepare() {
        // An activity made after a run ended, the player having gone back
        // and come again in the same process, plays anew (D577): Maul
        // Window asks only when no program runs, and frees the last run's
        // hold on its player when that run stops.
        player.reset();
        configuration.reset();
        stopRequested.store(false);
        if (log != nullptr) {
            std::fclose(log);
            log = nullptr;
        }
        const std::optional<std::string> kFiles = filesDirectory();
        if (!kFiles.has_value()) {
            return nullptr;
        }
        log = std::fopen((*kFiles + "/client.log").c_str(), "wb");
        const std::optional<std::string> kText = readText(*kFiles + "/client.conf");
        if (!kText.has_value()) {
            say("cannot read the configuration file client.conf");
            return nullptr;
        }
        auto parsed = composition::Configuration::parse(*kText, *kFiles);
        if (!parsed.has_value()) {
            say(parsed.error().description());
            return nullptr;
        }
        configuration = std::move(*parsed);
        player = std::make_unique<window_host::WindowHost>(
            host::HostRequest{.role = composition::TargetRole::Client,
                              .platform = composition::Platform::Android,
                              .registrars = kRegistrars,
                              .configuration = &*configuration,
                              .log = {.write = log != nullptr ? &writeLog : nullptr, .context = log},
                              .stopRequested = &stopRequested,
                              .defaultShutdownBudgetMs = 5500});
        return player.get();
    }

    /// A launch failure, before diagnostics exist: plain text, as a desktop
    /// process says it on standard error.
    void say(std::string_view why) const {
        if (log != nullptr) {
            std::fprintf(log, "rawframe-client: %.*s\n", static_cast<int>(why.size()), why.data());
            std::fflush(log);
        }
    }
};

} // namespace

namespace rawframe::window {

AndroidStart androidStart() noexcept {
    static AndroidClient client;
    return AndroidStart{.program = client.prepare(), .settings = {}};
}

} // namespace rawframe::window
