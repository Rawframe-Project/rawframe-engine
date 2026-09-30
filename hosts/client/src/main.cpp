// A desktop client (D249): one window, and a Host playing a Kest game on a
// server over QUIC as the process's own player, whose input comes from the
// window. Where Maul RHI is built, its canvas is drawn on the one device and
// shown in the window (D280); elsewhere it is heard, not yet seen.
//
//   rawframe-client [--config <file>]
//
// The window system owns the loop (SPEC-0025, D248), so the Host is driven
// from its frames (`window_host`, D250): each frame hands the window's raw
// input to the player's devices, runs every Host iteration due, then waits
// for the next one. The configuration plays the player with
// `bots.player = true`; closing the window ends the run the way a stop
// request does.

#include "rawframe/host/main.h"

#include "rawframe/composition/registrar.h"
#include "rawframe/game_content/registrar.h"
#include "rawframe/input_kest/registrar.h"
#include "rawframe/network_quic/registrar.h"
#include "rawframe/render_canvas/registrar.h"
#include "rawframe/window/windows.h"
#include "rawframe/window_host/window_host.h"
#include "rawframe/world_animation/registrar.h"
#include "rawframe/world_audio/registrar.h"
#include "rawframe/world_kest/registrar.h"
#include "rawframe/world_localization/registrar.h"
#include "rawframe/world_replication/registrar.h"
#include "rawframe/world_runtime/registrar.h"

#if RAWFRAME_CLIENT_DRAWS
#include "rawframe/render/registrar.h"
#include "rawframe/render_canvas_gpu/registrar.h"
#endif

#include <array>
#include <cstdio>

namespace {

using namespace rawframe;

#if RAWFRAME_CLIENT_DRAWS
constexpr std::size_t kDrawing = 2;
#else
constexpr std::size_t kDrawing = 0;
#endif

constexpr std::array<composition::RegistrarEntry, 10 + kDrawing> kRegistrars = {
    composition::RegistrarEntry{"game_content", &game_content::registerParticipants, game_content::kScopes},
    composition::RegistrarEntry{"network_quic", &network_quic::registerParticipants, network_quic::kScopes},
    composition::RegistrarEntry{"input_kest", &input_kest::registerParticipants, input_kest::kScopes},
    composition::RegistrarEntry{"render_canvas", &render_canvas::registerParticipants, render_canvas::kScopes},
    composition::RegistrarEntry{"world_animation", &world_animation::registerParticipants, world_animation::kScopes},
    composition::RegistrarEntry{"world_audio", &world_audio::registerParticipants, world_audio::kScopes},
    composition::RegistrarEntry{"world_kest", &world_kest::registerParticipants, world_kest::kScopes},
    composition::RegistrarEntry{
        "world_localization", &world_localization::registerParticipants, world_localization::kScopes},
    composition::RegistrarEntry{
        "world_replication", &world_replication::registerParticipants, world_replication::kScopes},
    composition::RegistrarEntry{"world_runtime", &world_runtime::registerParticipants, world_runtime::kScopes},
#if RAWFRAME_CLIENT_DRAWS
    composition::RegistrarEntry{"render", &render::registerParticipants, render::kScopes},
    composition::RegistrarEntry{
        "render_canvas_gpu", &render_canvas_gpu::registerParticipants, render_canvas_gpu::kScopes},
#endif
};

host::HostExit play(const host::HostRequest& request) noexcept {
    window_host::WindowHost client{request};
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
