// A desktop client (D249): one window, and a Host playing a Kest game on a
// server over QUIC as the process's own player, whose input comes from the
// window. Heard, not yet seen: nothing is drawn until the renderer exists.
//
//   rawframe-client [--config <file>]
//
// The window system owns the loop (SPEC-0025, D248), so the Host is driven
// from its frames: each frame hands the window's raw input to the player's
// devices, runs every Host iteration due, then waits for the next one. The
// configuration plays the player with `bots.player = true`; closing the
// window ends the run the way a stop request does.

#include "rawframe/host/main.h"

#include "rawframe/composition/registrar.h"
#include "rawframe/execution/time.h"
#include "rawframe/game_content/registrar.h"
#include "rawframe/host/host.h"
#include "rawframe/input/feed.h"
#include "rawframe/input_kest/registrar.h"
#include "rawframe/input_kest/sources.h"
#include "rawframe/input_window/bridge.h"
#include "rawframe/network_quic/registrar.h"
#include "rawframe/window/windows.h"
#include "rawframe/world_audio/registrar.h"
#include "rawframe/world_kest/registrar.h"
#include "rawframe/world_localization/registrar.h"
#include "rawframe/world_replication/registrar.h"
#include "rawframe/world_runtime/registrar.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <optional>
#include <thread>

namespace {

using namespace rawframe;

constexpr std::array<composition::RegistrarEntry, 8> kRegistrars = {
    composition::RegistrarEntry{"game_content", &game_content::registerParticipants, game_content::kScopes},
    composition::RegistrarEntry{"network_quic", &network_quic::registerParticipants, network_quic::kScopes},
    composition::RegistrarEntry{"input_kest", &input_kest::registerParticipants, input_kest::kScopes},
    composition::RegistrarEntry{"world_audio", &world_audio::registerParticipants, world_audio::kScopes},
    composition::RegistrarEntry{"world_kest", &world_kest::registerParticipants, world_kest::kScopes},
    composition::RegistrarEntry{
        "world_localization", &world_localization::registerParticipants, world_localization::kScopes},
    composition::RegistrarEntry{
        "world_replication", &world_replication::registerParticipants, world_replication::kScopes},
    composition::RegistrarEntry{"world_runtime", &world_runtime::registerParticipants, world_runtime::kScopes},
};

// The most iterations one frame runs to catch up, as the web client's.
constexpr int kMostIterationsPerFrame = 4;
// The longest a frame waits for the Host: input waits no longer than this
// to be seen, and a frame this long still answers the window system.
constexpr auto kLongestWait = std::chrono::milliseconds{4};

class Client final : public window::Program {
public:
    explicit Client(const host::HostRequest& request) : request_(request) {
        request_.lent = lent_;
    }

    result::Status start(window::Windows& windows) override {
        RAWFRAME_TRY_ASSIGN(window_, windows.create(window::WindowSettings{.title = "Rawframe"}));
        bridge_.emplace(feed_);
        host_ = std::make_unique<host::Host>(request_);
        return {};
    }

    window::FrameOutcome frame(window::Windows& windows) override {
        bool closing = false;
        while (std::optional<window::Event> event = windows.next()) {
            closing = closing || event->kind == window::EventKind::CloseRequested;
            bridge_->take(*event);
        }
        if (closing) {
            return end();
        }
        for (int ran = 0; ran < kMostIterationsPerFrame && clock_.now() >= host_->due(); ++ran) {
            if (!host_->iterate()) {
                return end();
            }
        }
        const execution::MonotonicDuration kUntilDue = host_->due() - clock_.now();
        const auto kWait =
            std::min<std::chrono::nanoseconds>(std::chrono::nanoseconds{kUntilDue.nanoseconds}, kLongestWait);
        if (kWait.count() > 0) {
            std::this_thread::sleep_for(kWait);
        }
        return window::FrameOutcome::Continue;
    }

    void stop(window::Windows& /*windows*/, const result::Status& /*status*/) override {
        if (host_ != nullptr && !exit_.has_value()) {
            exit_ = host_->stop();
        }
    }

    /// How the Host ended, or nothing when it never ran.
    [[nodiscard]] std::optional<host::HostExit> exit() const noexcept {
        return exit_;
    }

private:
    window::FrameOutcome end() {
        exit_ = host_->stop();
        return window::FrameOutcome::Stop;
    }

    host::HostRequest request_;
    input::Feed feed_;
    std::array<composition::LentCapability, 1> lent_{
        composition::LentCapability{input_kest::kFeed.name, composition::provideAs(feed_)}};
    std::optional<input_window::Bridge> bridge_;
    window::WindowId window_;
    std::unique_ptr<host::Host> host_;
    execution::SteadyClock clock_;
    std::optional<host::HostExit> exit_;
};

host::HostExit play(const host::HostRequest& request) noexcept {
    Client client{request};
    const result::Status kRan = window::run(client, window::RunSettings{});
    if (client.exit().has_value()) {
        return *client.exit();
    }
    // No Host ran: the window system could not be reached, or refused.
    if (!kRan.has_value()) {
        std::fprintf(stderr,
                     "rawframe-client: %.*s\n",
                     static_cast<int>(kRan.error().description().size()),
                     kRan.error().description().data());
    }
    return host::HostExit::UnsupportedConfiguration;
}

} // namespace

int main(int argc, char** argv) {
    return host::hostMain(argc,
                          argv,
                          {.name = "rawframe-client",
                           .role = composition::TargetRole::Client,
                           .registrars = kRegistrars,
                           .drive = &play});
}
