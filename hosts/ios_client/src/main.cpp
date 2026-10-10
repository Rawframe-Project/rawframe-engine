// The iOS client (ADR-0084's second mobile target, D586): the client as an
// application UIKit runs. Maul Window's run hands the main thread to UIKit
// and never returns (mwin-0025), so the client ends the process itself once
// its Host has stopped, with the exit code a desktop client's main returns.
// The program plays the process's own player as the desktop client does,
// from the window's input and frames.
//
// Its files are the application's own: the configuration is `client.conf`
// in its Documents directory (`$HOME/Documents`, HOME being the
// application's container), whose relative paths are under it (D188), and
// the run's NDJSON diagnostics go to `client.log` beside it, since an
// application has no standard output anyone reads. Nothing is read from the
// application's bundle yet.

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

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
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

/// A launch failure, before diagnostics exist: plain text, as a desktop
/// process says it on standard error.
void say(std::FILE* log, std::string_view why) {
    if (log != nullptr) {
        std::fprintf(log, "rawframe-client: %.*s\n", static_cast<int>(why.size()), why.data());
        std::fflush(log);
    }
}

/// The player, run as the window system's program, and the process's end
/// once it has stopped: nothing returns from UIKit's run to end it.
class Ending final : public window::Program {
public:
    explicit Ending(window_host::WindowHost& player) noexcept : player_(&player) {
    }

    [[nodiscard]] result::Status start(window::Windows& windows) override {
        return player_->start(windows);
    }

    [[nodiscard]] window::FrameOutcome frame(window::Windows& windows) override {
        return player_->frame(windows);
    }

    void stop(window::Windows& windows, const result::Status& status) override {
        player_->stop(windows, status);
        std::exit(host::exitCode(player_->exit().value_or(host::HostExit::UnsupportedConfiguration)));
    }

private:
    window_host::WindowHost* player_;
};

} // namespace

int main() {
    const char* const kHome = std::getenv("HOME");
    if (kHome == nullptr) {
        return host::exitCode(host::HostExit::InvalidInvocation);
    }
    const std::string kFiles = std::string{kHome} + "/Documents";
    std::FILE* const kLog = std::fopen((kFiles + "/client.log").c_str(), "wb");
    const std::optional<std::string> kText = readText(kFiles + "/client.conf");
    if (!kText.has_value()) {
        say(kLog, "cannot read the configuration file client.conf");
        return host::exitCode(host::HostExit::InvalidLaunchDescriptor);
    }
    auto configuration = composition::Configuration::parse(*kText, kFiles);
    if (!configuration.has_value()) {
        say(kLog, configuration.error().description());
        return host::exitCode(host::HostExit::InvalidLaunchDescriptor);
    }
    std::atomic<bool> stopRequested{false};
    window_host::WindowHost player{
        host::HostRequest{.role = composition::TargetRole::Client,
                          .platform = composition::Platform::Ios,
                          .registrars = kRegistrars,
                          .configuration = &*configuration,
                          .log = {.write = kLog != nullptr ? &writeLog : nullptr, .context = kLog},
                          .stopRequested = &stopRequested,
                          .defaultShutdownBudgetMs = 5500}};
    Ending program{player};
    // Returns only where the window system could not run.
    const result::Status kRan = window::run(program, window::RunSettings{});
    if (!kRan.has_value()) {
        say(kLog, kRan.error().description());
    }
    return host::exitCode(host::HostExit::UnsupportedConfiguration);
}
